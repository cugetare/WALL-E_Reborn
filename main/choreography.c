#include "choreography.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "calibration.h"
#include "command_protocol.h"
#include "media_link.h"
#include "profile_catalog.h"
#include "safety.h"
#include "esp_timer.h"
#include "freertos/task.h"
typedef enum { JOINT,DRIVE,AUDIO,REST } event_kind;
typedef struct { event_kind kind; uint32_t at,duration; unsigned joint; float a,b; char asset[80]; } event_t;
typedef struct { size_t count; uint32_t duration; event_t events[256]; } program_t;
static QueueHandle_t output;
static portMUX_TYPE lock=portMUX_INITIALIZER_UNLOCKED;
static bool playing,cancelled;
static char selected[32];
static const char *joint_names[]={"arm_left","arm_right","neck_tilt","head_tilt","head_pan","eye_left","eye_right","brow_left","brow_right"};
static const char *text(cJSON *j,const char *key) {
    cJSON *v=cJSON_GetObjectItemCaseSensitive(j,key); return cJSON_IsString(v)?v->valuestring:"";
}
static bool value(cJSON *j,const char *key,double min,double max,double *n) {
    cJSON *v=cJSON_GetObjectItemCaseSensitive(j,key);
    if(!cJSON_IsNumber(v)||!isfinite(v->valuedouble)||v->valuedouble<min||v->valuedouble>max) return false;
    *n=v->valuedouble; return true;
}
static bool time_value(cJSON *j,const char *key,uint32_t *n) {
    double v; if(!value(j,key,0,120000,&v)||floor(v)!=v) return false; *n=(uint32_t)v; return true;
}
static bool bounded_json(const char *data,size_t size) {
    if(!data||!size||size>65536||memchr(data,0,size)) return false;
    unsigned depth=0; bool quoted=false,escape=false;
    for(size_t i=0;i<size;i++) {
        char c=data[i];
        if(quoted) { if(escape) escape=false; else if(c=='\\') escape=true; else if(c=='"') quoted=false; }
        else if(c=='"') quoted=true;
        else if(c=='{'||c=='[') { if(++depth>8) return false; }
        else if(c=='}'||c==']') { if(!depth) return false; --depth; }
    }
    return !quoted&&!depth;
}
static program_t *compile(const char *data,size_t size,char *error,size_t capacity) {
    snprintf(error,capacity,"Invalid choreography schema, timing or event");
    if(!bounded_json(data,size)) return NULL;
    const char *end=NULL;
    cJSON *j=cJSON_ParseWithLengthOpts(data,size,&end,false);
    while(end && end<data+size && (*end==' '||*end=='\r'||*end=='\n'||*end=='\t')) ++end;
    if(j && end!=data+size) { cJSON_Delete(j); j=NULL; }
    if(!j) return NULL;
    program_t *p=calloc(1,sizeof(*p));
    cJSON *events=cJSON_GetObjectItemCaseSensitive(j,"events");
    bool ok=p && !strcmp(text(j,"schema"),"walle.choreography/v1") &&
        time_value(j,"durationMs",&p->duration) && p->duration>0 && cJSON_IsArray(events) &&
        cJSON_GetArraySize(events)>0 && cJSON_GetArraySize(events)<=256;
    uint32_t last=0,ends[10]={0};
    cJSON *item;
    cJSON_ArrayForEach(item,events) {
        if(!ok) break;
        event_t *e=&p->events[p->count++];
        ok=time_value(item,"atMs",&e->at)&&e->at>=last&&e->at<p->duration;
        last=e->at;
        const char *type=text(item,"type");
        if(!strcmp(type,"audio")) {
            e->kind=AUDIO;
            const char *asset=text(item,"asset");
            ok=ok&&strlen(asset)>0&&strlen(asset)<sizeof(e->asset)&&asset[0]!='/'&&!strstr(asset,"..");
            for(const char *s=asset;*s;s++) if(!((*s>='a'&&*s<='z')||(*s>='A'&&*s<='Z')||
                (*s>='0'&&*s<='9')||strchr("/_.-",*s))) ok=false;
            snprintf(e->asset,sizeof(e->asset),"%s",asset);
        } else {
            ok=ok&&time_value(item,"durationMs",&e->duration)&&e->duration>0&&e->at+e->duration<=p->duration;
            double a=0,b=0;
            if(!strcmp(type,"joint")) {
                e->kind=JOINT; e->joint=9;
                for(unsigned k=0;k<9;k++) if(!strcmp(text(item,"joint"),joint_names[k])) e->joint=k;
                ok=ok&&e->joint<9&&value(item,"value",0,1,&a);
                if(ok) { ok=e->at>=ends[e->joint]; ends[e->joint]=e->at+e->duration; }
            } else if(!strcmp(type,"drive")) {
                e->kind=DRIVE; ok=ok&&value(item,"throttle",-1,1,&a)&&value(item,"steering",-1,1,&b)&&e->at>=ends[9];
                ends[9]=e->at+e->duration;
            } else if(!strcmp(type,"rest")) {
                e->kind=REST; const char *group=text(item,"group");
                e->joint=!strcmp(group,"head")?2:!strcmp(group,"arms")?0:!strcmp(group,"all")?9:99;
                ok=ok&&e->joint!=99;
                unsigned first=e->joint==2?2:0,last_joint=e->joint==0?2:9;
                for(unsigned k=first;k<last_joint;k++) { if(e->at<ends[k]) ok=false; ends[k]=e->at+e->duration; }
            } else ok=false;
            e->a=a; e->b=b;
        }
    }
    cJSON_Delete(j);
    if(!ok) { free(p); return NULL; }
    error[0]=0; return p;
}
bool choreography_validate(const char *data,size_t size,char *error,size_t capacity) {
    program_t *p=compile(data,size,error,capacity); bool ok=p!=NULL; free(p); return ok;
}
bool choreography_playing(void) { taskENTER_CRITICAL(&lock); bool p=playing; taskEXIT_CRITICAL(&lock); return p; }
void choreography_stop(void) { taskENTER_CRITICAL(&lock); cancelled=true; taskEXIT_CRITICAL(&lock); media_link_request_stop(); }
static bool running(uint32_t epoch) {
    taskENTER_CRITICAL(&lock); bool ok=!cancelled; taskEXIT_CRITICAL(&lock);
    return ok&&safety_allowed(false)&&safety_snapshot().epoch==epoch;
}
static bool send(walle_command_kind_t kind,unsigned joint,float a,float b,uint32_t epoch,uint32_t duration) {
    walle_command_t c={.kind=kind,.index=joint,.a=a,.b=b,.epoch=epoch,.duration_ms=duration};
    return xQueueSend(output,&c,0)==pdTRUE;
}
static void player(void *arg) {
    (void)arg; char *data=NULL,error[96]; size_t size=0;
    uint32_t epoch=safety_snapshot().epoch;
    program_t *p=NULL;
    if(!media_link_profile_read(selected,&data,&size)) p=compile(data,size,error,sizeof(error));
    free(data);
    bool ok=p&&running(epoch); size_t next=0;
    uint32_t drive_end=0; float throttle=0,steering=0;
    int64_t start=esp_timer_get_time();
    while(ok) {
        uint32_t elapsed=(uint32_t)((esp_timer_get_time()-start)/1000);
        if(!running(epoch)) break;
        if(elapsed>=p->duration) break;
        while(next<p->count && p->events[next].at<=elapsed && ok) {
            event_t *e=&p->events[next++];
            if(e->kind==AUDIO) ok=!media_link_asset_play(e->asset);
            else if(e->kind==JOINT) ok=send(WALLE_CMD_JOINT,e->joint,e->a,0,epoch,e->duration);
            else if(e->kind==DRIVE) { throttle=e->a; steering=e->b; drive_end=e->at+e->duration; }
            else {
                unsigned first=e->joint==2?2:0,last_joint=e->joint==0?2:9;
                for(unsigned k=first;k<last_joint&&ok;k++) ok=send(WALLE_CMD_JOINT,k,.5f,0,epoch,e->duration);
            }
        }
        if(elapsed>=drive_end) throttle=steering=0;
        if(ok) ok=send(WALLE_CMD_DRIVE,0,throttle,steering,epoch,0);
        vTaskDelay(pdMS_TO_TICKS(40));
    }
    if(!ok) safety_stop();
    if(running(epoch)) send(WALLE_CMD_DRIVE,0,0,0,epoch,0);
    media_link_request_stop(); free(p);
    taskENTER_CRITICAL(&lock); playing=false; taskEXIT_CRITICAL(&lock);
    vTaskDelete(NULL);
}
void choreography_init(QueueHandle_t commands) { output=commands; }
int choreography_start(const char *id) {
    if(!walle_profile_id_valid(id)||!safety_allowed(false)||!media_link_online()) return -1;
    taskENTER_CRITICAL(&lock);
    if(playing) { taskEXIT_CRITICAL(&lock); return -1; }
    playing=true; cancelled=false; snprintf(selected,sizeof(selected),"%s",id);
    taskEXIT_CRITICAL(&lock);
    if(xTaskCreate(player,"choreography",6144,NULL,5,NULL)!=pdPASS) {
        taskENTER_CRITICAL(&lock); playing=false; taskEXIT_CRITICAL(&lock); return -1;
    }
    return 0;
}
