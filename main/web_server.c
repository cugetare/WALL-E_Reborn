#include "web_server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_app_desc.h"
#include "freertos/task.h"
#include "nvs.h"
#include "calibration.h"
#include "command_protocol.h"
#include "safety.h"
#include "servo_bus.h"
#include "media_link.h"
#include "profile_catalog.h"
#include "automation.h"
#include "wifi_manager.h"
#include "ota_update.h"
#include "hardware.h"
#include "sdkconfig.h"
#include "choreography.h"
static QueueHandle_t commands;
static walle_profile_catalog_t profiles;
static walle_automation_engine_t automations;
static walle_controller_state_t controller;
static uint32_t controller_ms;
static char access_key[33],owner[48];
static uint32_t owner_ms;
static bool seen[254],scan_valid;
static volatile bool scan_busy,wifi_busy;
static walle_wifi_network_t networks[50];
static size_t network_count;
static int wifi_scan_result;
static portMUX_TYPE scan_lock=portMUX_INITIALIZER_UNLOCKED;
static uint32_t clock_ms(void) { return (uint32_t)(esp_timer_get_time()/1000); }
static esp_err_t fail(httpd_req_t *r,int code,const char *message) {
    const char *status=code==401?"401 Unauthorized":code==409?"409 Conflict":code==503?"503 Service Unavailable":"400 Bad Request";
    httpd_resp_set_status(r,status); httpd_resp_set_type(r,"text/plain; charset=utf-8");
    return httpd_resp_sendstr(r,message);
}
static esp_err_t json_send(httpd_req_t *r,cJSON *j) {
    if(!j) return fail(r,503,"Out of memory");
    char *text=cJSON_PrintUnformatted(j); cJSON_Delete(j);
    if(!text) return fail(r,503,"Out of memory");
    httpd_resp_set_type(r,"application/json; charset=utf-8");
    httpd_resp_set_hdr(r,"Cache-Control","no-store");
    esp_err_t e=httpd_resp_sendstr(r,text); free(text); return e;
}
static cJSON *body(httpd_req_t *r) {
    if(r->content_len<=0 || r->content_len>4096) return NULL;
    char *text=calloc(1,(size_t)r->content_len+1); if(!text) return NULL;
    int have=0;
    while(have<r->content_len) {
        int n=httpd_req_recv(r,text+have,r->content_len-have);
        if(n<=0) { free(text); return NULL; } have+=n;
    }
    unsigned depth=0; bool quoted=false,escaped=false,valid=true;
    for(int i=0;i<have;i++) {
        char c=text[i];
        if(!c) { valid=false; break; }
        if(quoted) { if(escaped) escaped=false; else if(c=='\\') escaped=true; else if(c=='"') quoted=false; }
        else if(c=='"') quoted=true;
        else if(c=='{'||c=='[') { if(++depth>8) { valid=false; break; } }
        else if(c=='}'||c==']') { if(!depth) { valid=false; break; } --depth; }
    }
    cJSON *j=valid&&!depth&&!quoted?cJSON_ParseWithLengthOpts(text,have+1,NULL,true):NULL;
    free(text); return j;
}
static const char *str(cJSON *j,const char *key) {
    cJSON *v=cJSON_GetObjectItemCaseSensitive(j,key); return cJSON_IsString(v)?v->valuestring:"";
}
static bool number(cJSON *j,const char *key,double low,double high,double *out) {
    cJSON *v=cJSON_GetObjectItemCaseSensitive(j,key);
    if(!cJSON_IsNumber(v)||!isfinite(v->valuedouble)||v->valuedouble<low||v->valuedouble>high) return false;
    *out=v->valuedouble; return true;
}
static bool integer(cJSON *j,const char *key,unsigned low,unsigned high,unsigned *out) {
    double v; if(!number(j,key,low,high,&v)||floor(v)!=v) return false; *out=(unsigned)v; return true;
}
static bool authenticated(httpd_req_t *r) {
    char key[40];
    return httpd_req_get_hdr_value_str(r,"X-WALLE-Key",key,sizeof(key))==ESP_OK && !strcmp(key,access_key);
}
static bool owns(httpd_req_t *r,bool claim) {
    char client[48];
    if(httpd_req_get_hdr_value_str(r,"X-WALLE-Client",client,sizeof(client))!=ESP_OK || !client[0]) return false;
    uint32_t now=clock_ms();
    if(strcmp(owner,client) && owner[0] && (uint32_t)(now-owner_ms)<3000) return false;
    if(claim || !strcmp(owner,client)) { snprintf(owner,sizeof(owner),"%s",client); owner_ms=now; return true; }
    return false;
}
static bool bus_complete(const walle_calibration_profile_t *p) {
    bool valid; taskENTER_CRITICAL(&scan_lock);
    valid=scan_valid && !seen[1] && walle_calibration_commissioning_complete(p);
    for(size_t i=0;i<WALLE_JOINT_COUNT && valid;i++) valid=seen[p->joints[i].id];
    taskEXIT_CRITICAL(&scan_lock); return valid;
}
static void restart_task(void *arg) { (void)arg; vTaskDelay(pdMS_TO_TICKS(1200)); esp_restart(); }
static void scan_task(void *arg) {
    (void)arg; bool result[254]={0};
    for(unsigned id=1;id<254;id++) result[id]=servo_bus_ping(id)==0;
    taskENTER_CRITICAL(&scan_lock);
    memcpy(seen,result,sizeof(seen)); scan_valid=true;
    taskEXIT_CRITICAL(&scan_lock);
    walle_calibration_profile_t p; walle_calibration_snapshot(&p);
    safety_verified(bus_complete(&p),walle_calibration_ready(&p));
    scan_busy=false; vTaskDelete(NULL);
}
static void wifi_scan_task(void *arg) {
    (void)arg;
    wifi_scan_result=walle_wifi_scan(networks,50,&network_count);
    wifi_busy=false; vTaskDelete(NULL);
}
static esp_err_t status(httpd_req_t *r) {
    walle_calibration_profile_t p; walle_calibration_snapshot(&p);
    walle_safety_t s=safety_snapshot(); walle_wifi_status_t wifi; walle_wifi_get_status(&wifi);
    cJSON *j=cJSON_CreateObject(),*axes=cJSON_AddArrayToObject(j,"joints");
    cJSON_AddBoolToObject(j,"stopped",s.stopped); cJSON_AddBoolToObject(j,"service",s.service);
    cJSON_AddNumberToObject(j,"epoch",s.epoch);
    cJSON_AddBoolToObject(j,"physicalStop",s.physical_stop); cJSON_AddBoolToObject(j,"ota",s.ota);
    cJSON_AddBoolToObject(j,"batteryOk",s.battery_ok); cJSON_AddBoolToObject(j,"busVerified",s.bus_verified);
    cJSON_AddBoolToObject(j,"ready",s.calibrated); cJSON_AddBoolToObject(j,"mediaOnline",media_link_online());
    cJSON_AddBoolToObject(j,"playing",choreography_playing());
    cJSON_AddNumberToObject(j,"assignedMask",p.assigned_mask); cJSON_AddNumberToObject(j,"calibratedMask",p.calibrated_mask);
    cJSON_AddBoolToObject(j,"scanBusy",scan_busy);
    cJSON_AddStringToObject(j,"ip",wifi.ip); cJSON_AddStringToObject(j,"ssid",wifi.ssid);
    cJSON_AddStringToObject(j,"wifiMode",wifi.mode==WALLE_WIFI_STATION?"station":wifi.mode==WALLE_WIFI_AP?"ap":"connecting");
    cJSON_AddStringToObject(j,"version",esp_app_get_description()->version);
    cJSON_AddNumberToObject(j,"uptimeMs",clock_ms());
    cJSON_AddNumberToObject(j,"heap",esp_get_free_heap_size());
    cJSON_AddNumberToObject(j,"httpStackFree",uxTaskGetStackHighWaterMark(NULL));
    cJSON_AddBoolToObject(j,"controllerConnected",(uint32_t)(clock_ms()-controller_ms)<1000 && controller_ms!=0);
    for(size_t i=0;i<WALLE_JOINT_COUNT;i++) {
        cJSON *a=cJSON_CreateObject(); cJSON_AddItemToArray(axes,a);
        cJSON_AddNumberToObject(a,"id",p.joints[i].id);
        cJSON_AddNumberToObject(a,"min",p.joints[i].min_position);
        cJSON_AddNumberToObject(a,"rest",p.joints[i].center_position);
        cJSON_AddNumberToObject(a,"max",p.joints[i].max_position);
        cJSON_AddBoolToObject(a,"inverted",p.joints[i].inverted);
        taskENTER_CRITICAL(&scan_lock); bool present=scan_valid&&seen[p.joints[i].id]; taskEXIT_CRITICAL(&scan_lock);
        cJSON_AddBoolToObject(a,"present",present);
    }
    return json_send(r,j);
}
static bool queue_command(walle_command_t *c) {
    c->epoch=safety_snapshot().epoch; return xQueueSend(commands,c,0)==pdTRUE;
}
static esp_err_t command_handler(httpd_req_t *r,cJSON *j) {
    const char *text=str(j,"command");
    if(!strcmp(text,"stop")) { safety_stop(); choreography_stop(); media_link_request_stop(); xQueueReset(commands); return httpd_resp_sendstr(r,"Stopped; explicit rearm required"); }
    if(!owns(r,true)) return fail(r,409,"Another browser owns control. Stop it or wait for its lease to expire.");
    if(!strcmp(text,"heartbeat")) { safety_heartbeat(clock_ms()); return httpd_resp_sendstr(r,"OK"); }
    if(!strcmp(text,"service")) { safety_service(true); choreography_stop(); walle_calibration_set_active(true); return httpd_resp_sendstr(r,"Service selected; rearm to jog"); }
    if(!strcmp(text,"play-mode")) { safety_service(false); walle_calibration_set_active(false); return httpd_resp_sendstr(r,"Play selected; verify bus, battery and rearm"); }
    if(!strcmp(text,"arm")) {
        if(scan_busy||wifi_busy) return fail(r,409,"Wait for scanning to finish");
        walle_calibration_profile_t p; walle_calibration_snapshot(&p);
        safety_verified(bus_complete(&p),walle_calibration_ready(&p));
        return safety_arm(clock_ms())?httpd_resp_sendstr(r,"Armed"):fail(r,409,"STOP, battery, calibration or bus interlock");
    }
    walle_command_t c;
    unsigned epoch;
    if(!integer(j,"epoch",0,UINT32_MAX,&epoch)||epoch!=safety_snapshot().epoch)
        return fail(r,409,"Stale control session; refresh and rearm");
    if(!walle_command_parse(text,strlen(text),&c)) return fail(r,400,"Invalid command");
    bool calibration=c.kind==WALLE_CMD_CALIB_MOVE;
    if(c.kind!=WALLE_CMD_DRIVE&&c.kind!=WALLE_CMD_JOINT&&!calibration&&c.kind!=WALLE_CMD_SOUND)
        return fail(r,400,"Use the explicit calibration actions");
    if(!safety_allowed(calibration)) return fail(r,409,"Motion locked; select mode and rearm");
    if(choreography_playing()) return fail(r,409,"Stop the choreography before manual control");
    if(c.kind==WALLE_CMD_SOUND)
        return media_link_play(c.index)?fail(r,503,"Media did not acknowledge audio"):httpd_resp_sendstr(r,"Audio acknowledged");
    return queue_command(&c)?httpd_resp_sendstr(r,"Accepted"):fail(r,503,"Command queue full");
}
static esp_err_t servo_handler(httpd_req_t *r,const char *action,cJSON *j) {
    if(!owns(r,true)) return fail(r,409,"Another browser owns service");
    walle_calibration_profile_t p; walle_calibration_snapshot(&p);
    if(!safety_snapshot().service || walle_ota_is_active()) return fail(r,409,"Select service mode first");
    if(!strcmp(action,"scan")) {
        if(scan_busy) return httpd_resp_sendstr(r,"Scanning");
        if(!safety_snapshot().stopped) return fail(r,409,"STOP before scanning");
        scan_busy=true;
        if(xTaskCreate(scan_task,"servo_scan",4096,NULL,4,NULL)!=pdPASS) { scan_busy=false; return fail(r,503,"Cannot start scan"); }
        return httpd_resp_sendstr(r,"Scan started: IDs 1-253");
    }
    if(scan_busy) return fail(r,409,"Scan in progress");
    unsigned joint;
    if(!integer(j,"joint",0,8,&joint)) return fail(r,400,"Invalid joint");
    if(!strcmp(action,"assign")) {
        unsigned source,target;
        if(!safety_snapshot().stopped || !integer(j,"source",1,253,&source)||!integer(j,"target",2,253,&target))
            return fail(r,409,"STOP and select valid IDs");
        walle_calibration_profile_t check=p;
        if(!walle_calibration_assign_servo(&check,joint,target)) return fail(r,400,"ID already assigned to another movement");
        if(servo_bus_change_id(source,target)||walle_calibration_save_assignment(joint,target))
            return fail(r,503,"Assignment not confirmed; rescan before retry");
        scan_valid=false; safety_verified(false,false);
        return httpd_resp_sendstr(r,"Assigned. Scan again to verify the complete bus.");
    }
    if(!(p.assigned_mask & (1U<<joint))) return fail(r,409,"Joint not assigned");
    if(!strcmp(action,"telemetry")||!strcmp(action,"capture")) {
        uint16_t position,load;
        if(servo_bus_read_telemetry(p.joints[joint].id,&position,&load) || position>1023) {
            safety_stop(); safety_verified(false,safety_snapshot().calibrated);
            return fail(r,503,"No valid servo feedback; motion stopped");
        }
        if((load&1023)>800) safety_stop();
        cJSON *result=cJSON_CreateObject();
        cJSON_AddNumberToObject(result,"position",position); cJSON_AddNumberToObject(result,"load",load&1023);
        cJSON_AddNumberToObject(result,"direction",(load&1024)?-1:1);
        return json_send(r,result);
    }
    if(!strcmp(action,"center")) {
        if(!safety_allowed(true)) return fail(r,409,"Rearm service before centering");
        walle_command_t c={.kind=WALLE_CMD_CALIB_MOVE,.index=joint,.rest=512};
        return queue_command(&c)?httpd_resp_sendstr(r,"Center requested with slew limit"):fail(r,503,"Queue full");
    }
    if(!strcmp(action,"save")) {
        unsigned min,rest,max;
        if(!integer(j,"min",0,1023,&min)||!integer(j,"rest",0,1023,&rest)||!integer(j,"max",0,1023,&max)||
           !(min<rest&&rest<max)||!bus_complete(&p)) return fail(r,400,"Verify all assigned servos and MIN < REST < MAX");
        safety_stop();
        if(walle_calibration_save_joint(joint,min,rest,max,cJSON_IsTrue(cJSON_GetObjectItem(j,"inverted"))))
            return fail(r,503,"Calibration persistence failed");
        return httpd_resp_sendstr(r,"Calibration saved; rearm before another move");
    }
    return fail(r,400,"Unknown servo action");
}
static esp_err_t catalog(httpd_req_t *r) {
    cJSON *j=cJSON_CreateObject(),*list=cJSON_AddArrayToObject(j,"profiles");
    for(size_t i=0;i<profiles.count;i++) {
        cJSON *p=cJSON_CreateObject(); cJSON_AddItemToArray(list,p);
        cJSON_AddStringToObject(p,"id",profiles.entries[i].id);
        cJSON_AddStringToObject(p,"name",profiles.entries[i].name);
        cJSON_AddNumberToObject(p,"size",profiles.entries[i].size);
    }
    list=cJSON_AddArrayToObject(j,"automations");
    for(size_t i=0;i<automations.count;i++) {
        const walle_automation_t *a=&automations.rules[i]; cJSON *v=cJSON_CreateObject(); cJSON_AddItemToArray(list,v);
        cJSON_AddNumberToObject(v,"id",a->id); cJSON_AddStringToObject(v,"name",a->name);
        cJSON_AddStringToObject(v,"profile",a->profile_id);
        cJSON_AddNumberToObject(v,"buttons",a->buttons); cJSON_AddNumberToObject(v,"lt",a->left_trigger_min);
        cJSON_AddNumberToObject(v,"rt",a->right_trigger_min); cJSON_AddNumberToObject(v,"hold",a->hold_ms);
        cJSON_AddNumberToObject(v,"cooldown",a->cooldown_ms); cJSON_AddBoolToObject(v,"neutral",a->require_neutral_sticks);
        cJSON_AddBoolToObject(v,"enabled",a->enabled);
    }
    return json_send(r,j);
}
static esp_err_t profile_upload(httpd_req_t *r) {
    if(!safety_snapshot().stopped || r->content_len<=0||r->content_len>65536) return fail(r,409,"STOP first; profile maximum is 64 KiB");
    char id[32],name[48];
    if(httpd_req_get_hdr_value_str(r,"X-Profile-ID",id,sizeof(id))||
       httpd_req_get_hdr_value_str(r,"X-Profile-Name",name,sizeof(name))||!walle_profile_id_valid(id))
        return fail(r,400,"Invalid profile metadata");
    walle_profile_catalog_t proposed=profiles;
    walle_profile_info_t info={.size=r->content_len};
    snprintf(info.id,sizeof(info.id),"%s",id); snprintf(info.name,sizeof(info.name),"%s",name);
    if(!walle_profile_catalog_set(&proposed,&info)) return fail(r,400,"Catalog full or invalid metadata");
    char *data=malloc((size_t)r->content_len+1); if(!data) return fail(r,503,"Out of memory");
    size_t have=0;
    while(have<(size_t)r->content_len) {
        int n=httpd_req_recv(r,data+have,r->content_len-have);
        if(n<=0) { free(data); return fail(r,400,"Upload interrupted"); } have+=n;
    }
    data[have]=0; char error[96];
    if(!choreography_validate(data,have,error,sizeof(error))) { free(data); return fail(r,400,error); }
    int result=media_link_profile_begin(id,have);
    for(size_t offset=0;!result&&offset<have;) {
        size_t n=have-offset>1024?1024:have-offset;
        result=media_link_profile_write(data+offset,n); offset+=n;
    }
    info.crc32=walle_crc32_update(0,data,have); free(data);
    if(result) { media_link_profile_abort(); return fail(r,503,"Media/SD transfer not acknowledged"); }
    if(media_link_profile_finish(info.crc32)) return fail(r,503,"SD commit or CRC not acknowledged");
    walle_profile_catalog_set(&proposed,&info);
    if(walle_profile_catalog_save(&proposed)) return fail(r,503,"SD saved; catalog persistence failed");
    profiles=proposed; return httpd_resp_sendstr(r,"Validated and committed to SD");
}
static void url_decode(char *s) {
    char *out=s;
    for(char *p=s;*p;p++,out++) {
        if(*p=='+' ) *out=' ';
        else if(*p=='%' && p[1] && p[2]) {
            char h[3]={p[1],p[2],0}; *out=(char)strtoul(h,NULL,16); p+=2;
        } else *out=*p;
    }
    *out=0;
}
static bool file_query_path(httpd_req_t *r,char *path,size_t capacity) {
    char query[160];
    if(httpd_req_get_url_query_str(r,query,sizeof(query))!=ESP_OK) return false;
    if(httpd_query_key_value(query,"path",path,capacity)!=ESP_OK) return false;
    url_decode(path);
    return media_link_path_valid(path);
}
static esp_err_t file_list(httpd_req_t *r) {
    char path[96];
    if(!file_query_path(r,path,sizeof(path))) return fail(r,400,"Invalid file path");
    char *json=NULL; size_t length=0;
    if(media_link_file_list(path,&json,&length)) return fail(r,503,"Media SD did not return a directory listing");
    httpd_resp_set_type(r,"application/json; charset=utf-8");
    httpd_resp_set_hdr(r,"Cache-Control","no-store");
    esp_err_t result=httpd_resp_send(r,json,length);
    free(json); return result;
}
static esp_err_t file_download(httpd_req_t *r) {
    char path[96];
    if(!file_query_path(r,path,sizeof(path))) return fail(r,400,"Invalid file path");
    char *data=NULL; size_t length=0;
    if(media_link_file_read(path,&data,&length)) return fail(r,503,"Media SD did not return the file");
    httpd_resp_set_type(r,"application/octet-stream");
    httpd_resp_set_hdr(r,"Cache-Control","no-store");
    esp_err_t result=httpd_resp_send(r,data,length);
    free(data); return result;
}
static esp_err_t file_upload(httpd_req_t *r) {
    if(!safety_snapshot().stopped || r->content_len<=0 || r->content_len>4194304)
        return fail(r,409,"STOP first; file maximum is 4 MiB");
    char path[96];
    if(httpd_req_get_hdr_value_str(r,"X-File-Path",path,sizeof(path))||!media_link_path_valid(path))
        return fail(r,400,"Invalid destination path");
    safety_stop(); choreography_stop(); media_link_request_stop();
    if(media_link_file_begin(path,(size_t)r->content_len)) return fail(r,503,"Media SD did not accept the upload");
    uint8_t *chunk=malloc(1024); if(!chunk) { media_link_file_abort(); return fail(r,503,"Out of memory"); }
    int remaining_len=r->content_len; uint32_t crc=0; bool ok=true;
    while(remaining_len>0) {
        int want=remaining_len>1024?1024:remaining_len;
        int n=httpd_req_recv(r,(char*)chunk,want);
        if(n<=0||media_link_file_write(chunk,(size_t)n)) { ok=false; break; }
        crc=walle_crc32_update(crc,chunk,(size_t)n); remaining_len-=n;
    }
    free(chunk);
    if(!ok) { media_link_file_abort(); return fail(r,400,"Upload interrupted; temporary file discarded"); }
    if(media_link_file_finish(crc)) return fail(r,503,"Media SD commit or CRC not acknowledged");
    return httpd_resp_sendstr(r,"File committed to media SD");
}
static esp_err_t file_delete(httpd_req_t *r,cJSON *j) {
    const char *path=str(j,"path");
    if(!safety_snapshot().stopped) return fail(r,409,"STOP before deleting files");
    if(!media_link_path_valid(path)) return fail(r,400,"Invalid file path");
    if(media_link_file_delete(path)) return fail(r,503,"Media SD deletion not acknowledged");
    return httpd_resp_sendstr(r,"Deleted from media SD");
}
static esp_err_t file_play(httpd_req_t *r,cJSON *j) {
    const char *path=str(j,"path");
    if(!media_link_path_valid(path)||strncmp(path,"sounds/",7)) return fail(r,400,"Select a sound asset");
    return media_link_asset_play(path)?fail(r,503,"Media did not acknowledge playback"):httpd_resp_sendstr(r,"Playback started");
}
static esp_err_t automation_save(httpd_req_t *r,cJSON *j) {
    unsigned id,buttons,lt,rt,hold,cooldown;
    if(!integer(j,"id",1,65535,&id)||!integer(j,"buttons",0,0xFFFF,&buttons)||
       !integer(j,"lt",0,1000,&lt)||!integer(j,"rt",0,1000,&rt)||
       !integer(j,"hold",0,5000,&hold)||!integer(j,"cooldown",0,60000,&cooldown))
        return fail(r,400,"Invalid automation values");
    walle_automation_t a={.id=id,.buttons=buttons,.left_trigger_min=lt,.right_trigger_min=rt,
        .hold_ms=hold,.cooldown_ms=cooldown,.enabled=cJSON_IsTrue(cJSON_GetObjectItem(j,"enabled")),
        .require_neutral_sticks=cJSON_IsTrue(cJSON_GetObjectItem(j,"neutral"))};
    if(strlen(str(j,"name"))>=sizeof(a.name)||!walle_profile_catalog_contains(&profiles,str(j,"profile")))
        return fail(r,400,"Invalid name or profile");
    snprintf(a.name,sizeof(a.name),"%s",str(j,"name")); snprintf(a.profile_id,sizeof(a.profile_id),"%s",str(j,"profile"));
    walle_automation_engine_t proposed=automations;
    if(!walle_automation_set(&proposed,&a)||walle_automation_save(&proposed)) return fail(r,400,"Automation not saved");
    automations=proposed; return httpd_resp_sendstr(r,"Automation saved");
}
void walle_web_controller_update(const walle_controller_state_t *s,uint32_t now,bool ready) {
    if(!s) return;
    controller=*s; controller_ms=now;
    const walle_automation_t *a=walle_automation_process(&automations,s,now,ready);
    if(a&&!choreography_playing()) choreography_start(a->profile_id);
}
static esp_err_t controller_handler(httpd_req_t *r,cJSON *j) {
    if(!owns(r,false)) return fail(r,409,"Acquire control first");
    unsigned buttons,lt,rt; double lx,ly,rx,ry;
    if(!integer(j,"buttons",0,65535,&buttons)||!integer(j,"lt",0,1000,&lt)||!integer(j,"rt",0,1000,&rt)||
       !number(j,"lx",-1,1,&lx)||!number(j,"ly",-1,1,&ly)||!number(j,"rx",-1,1,&rx)||!number(j,"ry",-1,1,&ry))
        return fail(r,400,"Invalid controller report");
    walle_controller_state_t s={.buttons=buttons,.left_trigger=lt,.right_trigger=rt,
        .left_x=lx*32767,.left_y=ly*32767,.right_x=rx*32767,.right_y=ry*32767};
    safety_heartbeat(clock_ms()); walle_web_controller_update(&s,clock_ms(),safety_allowed(false));
    return httpd_resp_sendstr(r,"Controller report accepted");
}
static esp_err_t ota_upload(httpd_req_t *r) {
    if(!safety_snapshot().stopped||r->content_len<256) return fail(r,409,"STOP before OTA");
    safety_stop(); choreography_stop(); media_link_request_stop();
    if(walle_ota_begin(r->content_len)) return fail(r,400,"OTA cannot start");
    uint8_t *chunk=malloc(4096); if(!chunk) { walle_ota_abort(); return fail(r,503,"Out of memory"); }
    int remaining=r->content_len; bool ok=true;
    while(remaining>0) {
        int n=httpd_req_recv(r,(char*)chunk,remaining>4096?4096:remaining);
        if(n<=0||walle_ota_write(chunk,n)) { ok=false; break; } remaining-=n;
    }
    free(chunk);
    if(!ok) { walle_ota_abort(); return fail(r,400,"OTA interrupted; previous image retained"); }
    if(walle_ota_finish()) return fail(r,400,"Invalid firmware; previous image retained");
    httpd_resp_sendstr(r,"Installed; restarting. New image must pass 30-second health window.");
    xTaskCreate(restart_task,"restart",2048,NULL,3,NULL); return ESP_OK;
}
static esp_err_t api(httpd_req_t *r) {
    const char *path=r->uri;
    if(!strcmp(path,"/api/status")&&r->method==HTTP_GET) return status(r);
    if(!authenticated(r)) return fail(r,401,"Enter the device key printed on USB serial at startup");
    if(walle_ota_is_active()) return fail(r,409,"OTA in progress");
    if(r->method==HTTP_GET) {
        if(!strcmp(path,"/api/calibration/backup")) {
            walle_calibration_profile_t p; walle_calibration_snapshot(&p);
            cJSON *root=cJSON_CreateObject(),*axes=cJSON_AddArrayToObject(root,"joints");
            cJSON_AddNumberToObject(root,"version",p.version);
            cJSON_AddNumberToObject(root,"calibratedMask",p.calibrated_mask);
            for(unsigned i=0;i<9;i++) {
                cJSON *a=cJSON_CreateObject(); cJSON_AddItemToArray(axes,a);
                cJSON_AddNumberToObject(a,"id",p.joints[i].id);
                cJSON_AddNumberToObject(a,"min",p.joints[i].min_position);
                cJSON_AddNumberToObject(a,"rest",p.joints[i].center_position);
                cJSON_AddNumberToObject(a,"max",p.joints[i].max_position);
                cJSON_AddBoolToObject(a,"inverted",p.joints[i].inverted);
            }
            return json_send(r,root);
        }
        if(!strcmp(path,"/api/catalog")) return catalog(r);
        if(!strcmp(path,"/api/files")) return file_list(r);
        if(!strcmp(path,"/api/files/download")) return file_download(r);
        if(!strcmp(path,"/api/scan")) {
            cJSON *j=cJSON_CreateObject(),*ids=cJSON_AddArrayToObject(j,"ids");
            cJSON_AddBoolToObject(j,"busy",scan_busy);
            taskENTER_CRITICAL(&scan_lock); bool copy[254]; memcpy(copy,seen,sizeof(copy)); taskEXIT_CRITICAL(&scan_lock);
            for(unsigned i=1;i<254;i++) if(copy[i]) cJSON_AddItemToArray(ids,cJSON_CreateNumber(i));
            return json_send(r,j);
        }
        if(!strcmp(path,"/api/networks")) {
            cJSON *j=cJSON_CreateObject(),*list=cJSON_AddArrayToObject(j,"networks");
            cJSON_AddBoolToObject(j,"busy",wifi_busy); cJSON_AddNumberToObject(j,"result",wifi_scan_result);
            if(!wifi_busy) for(size_t i=0;i<network_count;i++) {
                cJSON *n=cJSON_CreateObject(); cJSON_AddItemToArray(list,n);
                cJSON_AddStringToObject(n,"ssid",networks[i].ssid); cJSON_AddNumberToObject(n,"rssi",networks[i].rssi);
            }
            return json_send(r,j);
        }
        if(!strcmp(path,"/api/controller")) {
            cJSON *j=cJSON_CreateObject(); cJSON_AddNumberToObject(j,"buttons",controller.buttons);
            cJSON_AddNumberToObject(j,"lt",controller.left_trigger); cJSON_AddNumberToObject(j,"rt",controller.right_trigger);
            cJSON_AddBoolToObject(j,"connected",controller_ms && (uint32_t)(clock_ms()-controller_ms)<1000);
            return json_send(r,j);
        }
        return fail(r,400,"Unknown endpoint");
    }
    if(strcmp(path,"/api/command") && !owns(r,true)) return fail(r,409,"Another browser owns this robot");
    if(!strcmp(path,"/api/ota")) return ota_upload(r);
    if(!strcmp(path,"/api/profiles/upload")) return profile_upload(r);
    if(!strcmp(path,"/api/files/upload")) return file_upload(r);
    cJSON *j=body(r); if(!j) return fail(r,400,"Expected JSON body");
    esp_err_t result=ESP_OK;
    if(!strcmp(path,"/api/command")) result=command_handler(r,j);
    else if(!strncmp(path,"/api/servos/",12)) result=servo_handler(r,path+12,j);
    else if(!strcmp(path,"/api/controller")) result=controller_handler(r,j);
    else if(!strcmp(path,"/api/calibration/restore")) {
        walle_calibration_profile_t p; walle_calibration_defaults(&p);
        cJSON *axes=cJSON_GetObjectItemCaseSensitive(j,"joints");
        unsigned version,mask;
        bool valid=safety_snapshot().stopped && integer(j,"version",2,2,&version) &&
            integer(j,"calibratedMask",0,511,&mask) && cJSON_IsArray(axes)&&cJSON_GetArraySize(axes)==9;
        for(unsigned i=0;i<9&&valid;i++) {
            cJSON *a=cJSON_GetArrayItem(axes,i); unsigned id,min,rest,max;
            valid=integer(a,"id",0,253,&id)&&id!=1&&integer(a,"min",0,1023,&min)&&
                integer(a,"rest",0,1023,&rest)&&integer(a,"max",0,1023,&max)&&min<rest&&rest<max;
            if(valid) {
                if(id) valid=walle_calibration_assign_servo(&p,i,id);
                p.joints[i].min_position=min;p.joints[i].center_position=rest;p.joints[i].max_position=max;
                p.joints[i].inverted=cJSON_IsTrue(cJSON_GetObjectItem(a,"inverted"));
            }
        }
        if(valid) { p.calibrated_mask=mask; valid=(mask & ~p.assigned_mask)==0; }
        if(!valid||walle_calibration_restore(&p)) result=fail(r,400,"Invalid backup; existing calibration retained");
        else { safety_service(true);safety_verified(false,false);scan_valid=false;result=httpd_resp_sendstr(r,"Restored. Scan and validate before rearming."); }
    } else if(!strcmp(path,"/api/calibration/finish")) {
        walle_calibration_profile_t p; walle_calibration_snapshot(&p); safety_stop();
        if(!bus_complete(&p)||walle_calibration_finish()) result=fail(r,409,"Verify bus and save all nine calibrations first");
        else { safety_verified(true,true); result=httpd_resp_sendstr(r,"Calibration complete. Select Play and rearm."); }
    } else if(!strcmp(path,"/api/calibration/reset")) {
        safety_service(true); scan_valid=false; safety_verified(false,false);
        result=walle_calibration_reset()?fail(r,503,"Reset failed"):httpd_resp_sendstr(r,"Assignments and calibration reset");
    } else if(!strcmp(path,"/api/wifi/scan")) {
        if(!safety_snapshot().stopped) result=fail(r,409,"STOP before network scan");
        else if(wifi_busy) result=fail(r,409,"Already scanning");
        else {
            wifi_busy=true; network_count=0;
            if(xTaskCreate(wifi_scan_task,"wifi_scan",4096,NULL,4,NULL)!=pdPASS) { wifi_busy=false; result=fail(r,503,"Cannot scan"); }
            else result=httpd_resp_sendstr(r,"Scanning networks");
        }
    } else if(!strcmp(path,"/api/wifi")||!strcmp(path,"/api/wifi/forget")) {
        safety_stop(); choreography_stop();
        int e=!strcmp(path,"/api/wifi")?walle_wifi_save_credentials(str(j,"ssid"),str(j,"password")):walle_wifi_forget_credentials();
        if(e) result=fail(r,400,"Invalid credentials or persistence failure");
        else { result=httpd_resp_sendstr(r,"Saved; restarting. If connection fails, setup AP returns."); xTaskCreate(restart_task,"restart",2048,NULL,3,NULL); }
    } else if(!strcmp(path,"/api/automations")) result=automation_save(r,j);
    else if(!strcmp(path,"/api/files/delete")) result=file_delete(r,j);
    else if(!strcmp(path,"/api/files/play")) result=file_play(r,j);
    else if(!strcmp(path,"/api/automations/delete")) {
        unsigned id; walle_automation_engine_t proposed=automations;
        if(!integer(j,"id",1,65535,&id)||!walle_automation_remove(&proposed,id)||walle_automation_save(&proposed))
            result=fail(r,400,"Delete failed");
        else { automations=proposed; result=httpd_resp_sendstr(r,"Deleted"); }
    } else if(!strcmp(path,"/api/profiles/play")) {
        if(!owns(r,false)||!safety_allowed(false)||!walle_profile_catalog_contains(&profiles,str(j,"id"))||choreography_start(str(j,"id")))
            result=fail(r,409,"Not ready, profile unavailable or media did not acknowledge");
        else result=httpd_resp_sendstr(r,"Playback started");
    } else if(!strcmp(path,"/api/profiles/delete")) {
        const char *id=str(j,"id"); bool referenced=false;
        for(size_t i=0;i<automations.count;i++) if(!strcmp(automations.rules[i].profile_id,id)) referenced=true;
        walle_profile_catalog_t proposed=profiles;
        if(!safety_snapshot().stopped||referenced) result=fail(r,409,"STOP and remove referencing automations first");
        else if(!walle_profile_catalog_remove(&proposed,id)||media_link_profile_delete(id)||walle_profile_catalog_save(&proposed))
            result=fail(r,503,"Deletion not confirmed");
        else { profiles=proposed; result=httpd_resp_sendstr(r,"Deleted from SD and catalog"); }
    } else result=fail(r,400,"Unknown action");
    cJSON_Delete(j); return result;
}
extern const unsigned char html_start[] asm("_binary_index_html_start");
extern const unsigned char html_end[] asm("_binary_index_html_end");
extern const unsigned char js_start[] asm("_binary_app_js_start");
extern const unsigned char js_end[] asm("_binary_app_js_end");
extern const unsigned char css_start[] asm("_binary_style_css_start");
extern const unsigned char css_end[] asm("_binary_style_css_end");
static esp_err_t asset(httpd_req_t *r) {
    httpd_resp_set_hdr(r,"Cache-Control","no-store");
    httpd_resp_set_hdr(r,"X-Content-Type-Options","nosniff");
    httpd_resp_set_hdr(r,"Content-Security-Policy","default-src 'self'; script-src 'self'; style-src 'self'; connect-src 'self'; frame-ancestors 'none'");
    if(!strcmp(r->uri,"/app.js")) { httpd_resp_set_type(r,"text/javascript; charset=utf-8"); return httpd_resp_send(r,(const char*)js_start,js_end-js_start); }
    if(!strcmp(r->uri,"/style.css")) { httpd_resp_set_type(r,"text/css; charset=utf-8"); return httpd_resp_send(r,(const char*)css_start,css_end-css_start); }
    httpd_resp_set_type(r,"text/html; charset=utf-8"); return httpd_resp_send(r,(const char*)html_start,html_end-html_start);
}
int walle_web_start(QueueHandle_t queue) {
    commands=queue;
    nvs_handle_t nvs; ESP_ERROR_CHECK(nvs_open("security",NVS_READWRITE,&nvs));
    size_t length=sizeof(access_key);
    if(nvs_get_str(nvs,"key",access_key,&length)!=ESP_OK) {
        for(unsigned i=0;i<4;i++) snprintf(access_key+i*8,9,"%08lx",(unsigned long)esp_random());
        ESP_ERROR_CHECK(nvs_set_str(nvs,"key",access_key)); ESP_ERROR_CHECK(nvs_commit(nvs));
    }
    nvs_close(nvs);
    ESP_LOGW("pairing","DEVICE KEY: %s",access_key);
    if(walle_profile_catalog_load(&profiles)) walle_profile_catalog_init(&profiles);
    if(walle_automation_load(&automations)) walle_automation_engine_init(&automations);
    choreography_init(commands);
    httpd_config_t cfg=HTTPD_DEFAULT_CONFIG(); cfg.stack_size=12288; cfg.max_uri_handlers=8;
    cfg.lru_purge_enable=true; cfg.uri_match_fn=httpd_uri_match_wildcard;
    httpd_handle_t server; ESP_ERROR_CHECK(httpd_start(&server,&cfg));
    const httpd_uri_t routes[]={
        {.uri="/api/*",.method=HTTP_GET,.handler=api},
        {.uri="/api/*",.method=HTTP_POST,.handler=api},
        {.uri="/*",.method=HTTP_GET,.handler=asset}};
    for(size_t i=0;i<sizeof(routes)/sizeof(routes[0]);i++) ESP_ERROR_CHECK(httpd_register_uri_handler(server,&routes[i]));
    return 0;
}
