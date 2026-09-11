#include "media_link.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "profile_catalog.h"
#include "driver/uart.h"
#include "esp_timer.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"
static SemaphoreHandle_t mutex;
static uint32_t sequence;
static uint32_t last_ack;
static bool stop_requested;
static bool uploading;
static bool file_uploading;
static size_t remaining;
static uint32_t upload_crc;
static const uart_port_t port=UART_NUM_2;
static int line(char *out,size_t capacity,unsigned timeout) {
    size_t n=0; int64_t deadline=esp_timer_get_time()+timeout*1000LL;
    while(esp_timer_get_time()<deadline) {
        char c;
        if(uart_read_bytes(port,&c,1,pdMS_TO_TICKS(10))!=1) continue;
        if(c=='\n') { out[n]=0; return 0; }
        if(n+1>=capacity) return -1;
        out[n++]=c;
    }
    return -1;
}
static int request(const char *verb,const char *args,const void *data,size_t size) {
    char header[160],reply[96]; uint32_t id=++sequence;
    int n=snprintf(header,sizeof(header),"REQ %lu %s %s\n",(unsigned long)id,verb,args?args:"");
    uart_flush_input(port);
    if(n<=0 || n>=(int)sizeof(header) || uart_write_bytes(port,header,n)!=n) return -1;
    if(size && uart_write_bytes(port,data,size)!=(int)size) return -1;
    if(line(reply,sizeof(reply),1500)) { __atomic_store_n(&last_ack,0,__ATOMIC_RELAXED); return -1; }
    char expected[48]; snprintf(expected,sizeof(expected),"ACK %lu OK",(unsigned long)id);
    if(strcmp(reply,expected)) { __atomic_store_n(&last_ack,0,__ATOMIC_RELAXED); return -1; }
    __atomic_store_n(&last_ack,(uint32_t)(esp_timer_get_time()/1000),__ATOMIC_RELAXED); return 0;
}
bool media_link_online(void) { uint32_t ack=__atomic_load_n(&last_ack,__ATOMIC_RELAXED); return ack && (uint32_t)((uint32_t)(esp_timer_get_time()/1000)-ack)<5000; }
void media_link_request_stop(void) { __atomic_store_n(&stop_requested,true,__ATOMIC_RELAXED); }
static void monitor_task(void *arg) {
    (void)arg;
    for(;;) {
        if(xSemaphoreTake(mutex,pdMS_TO_TICKS(20))==pdTRUE) {
            if(__atomic_exchange_n(&stop_requested,false,__ATOMIC_RELAXED)) { if(request("STOP","",NULL,0)) media_link_request_stop(); }
            else request("HELLO","1",NULL,0); /* ACK only when protocol v1 and SD are ready. */
            xSemaphoreGive(mutex);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
int media_link_init(void) {
    const uart_config_t cfg={.baud_rate=921600,.data_bits=UART_DATA_8_BITS,.parity=UART_PARITY_DISABLE,
        .stop_bits=UART_STOP_BITS_1,.flow_ctrl=UART_HW_FLOWCTRL_DISABLE,.source_clk=UART_SCLK_DEFAULT};
    ESP_ERROR_CHECK(uart_driver_install(port,4096,2048,0,NULL,0));
    ESP_ERROR_CHECK(uart_param_config(port,&cfg));
    ESP_ERROR_CHECK(uart_set_pin(port,CONFIG_WALLE_MEDIA_UART_TX,CONFIG_WALLE_MEDIA_UART_RX,-1,-1));
    mutex=xSemaphoreCreateMutex();
    return mutex && xTaskCreate(monitor_task,"media_health",3072,NULL,3,NULL)==pdPASS?0:-1;
}
static int simple(const char *verb,const char *args) {
    if(xSemaphoreTake(mutex,pdMS_TO_TICKS(100))!=pdTRUE) return -1;
    int result=request(verb,args,NULL,0); xSemaphoreGive(mutex); return result;
}
int media_link_play(uint16_t track) { char arg[16]; snprintf(arg,sizeof(arg),"%u",track); return simple("PLAY",arg); }
int media_link_stop(void) { media_link_request_stop(); return 0; }
int media_link_asset_play(const char *asset) {
    if(!asset||!*asset||strlen(asset)>79||strstr(asset,"..")||strchr(asset,'\n')||strchr(asset,' ')) return -1;
    return simple("ASSET",asset);
}
bool media_link_path_valid(const char *path) {
    if(!path||!*path||strlen(path)>95||path[0]=='/'||strstr(path,"..")||strchr(path,'\\')||
       strchr(path,' ')||strchr(path,'\n')||strchr(path,'\r')||strchr(path,':')) return false;
    const char *slash=strchr(path,'/');
    size_t root_len=slash?(size_t)(slash-path):strlen(path);
    if(!root_len || (slash && !slash[1])) return false;
    const char *roots[]={"sounds","profiles","recordings","firmware","tmp"};
    bool root_ok=false;
    for(size_t i=0;i<sizeof(roots)/sizeof(roots[0]);i++)
        if(strlen(roots[i])==root_len && !strncmp(path,roots[i],root_len)) root_ok=true;
    if(!root_ok) return false;
    for(const char *p=path;*p;p++)
        if(!((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')||*p=='/'||*p=='_'||*p=='-'||*p=='.')) return false;
    return true;
}
static int read_blob(const char *verb,const char *arg,char **data,size_t *length,size_t max_size) {
    if(!data||!length) return -1;
    *data=NULL; *length=0;
    if(xSemaphoreTake(mutex,pdMS_TO_TICKS(100))!=pdTRUE) return -1;
    char header[96]; unsigned size=0; unsigned long crc=0;
    int result=request(verb,arg,NULL,0);
    if(!result && (!line(header,sizeof(header),1000)) &&
       sscanf(header,"DATA %u %lx",&size,&crc)==2 && size<=max_size) {
        char *buffer=malloc(size+1); size_t have=0;
        if(buffer) {
            while(have<size) {
                int n=uart_read_bytes(port,buffer+have,size-have,pdMS_TO_TICKS(1000));
                if(n<=0) break;
                have+=(size_t)n;
            }
            if(have==size && walle_crc32_update(0,buffer,size)==(uint32_t)crc) {
                buffer[size]=0; *data=buffer; *length=size; result=0;
            } else { free(buffer); result=-1; }
        } else result=-1;
    } else result=-1;
    xSemaphoreGive(mutex); return result;
}
int media_link_file_list(const char *path,char **json,size_t *length) {
    return media_link_path_valid(path)?read_blob("FS_LIST",path,json,length,8192):-1;
}
int media_link_file_read(const char *path,char **data,size_t *length) {
    return media_link_path_valid(path)?read_blob("FS_GET",path,data,length,262144):-1;
}
int media_link_file_begin(const char *path,size_t size) {
    if(!media_link_path_valid(path)||!size||size>4194304||!media_link_online()) return -1;
    if(xSemaphoreTake(mutex,pdMS_TO_TICKS(100))!=pdTRUE) return -1;
    char args[128]; snprintf(args,sizeof(args),"%s %u",path,(unsigned)size);
    if(request("FS_PUT",args,NULL,0)) { xSemaphoreGive(mutex); return -1; }
    remaining=size; upload_crc=0; file_uploading=true; return 0;
}
int media_link_file_write(const void *data,size_t size) {
    if(!file_uploading||!size||size>remaining||size>1024) return -1;
    char arg[20]; snprintf(arg,sizeof(arg),"%u",(unsigned)size);
    if(request("FS_CHUNK",arg,data,size)) return -1;
    remaining-=size; upload_crc=walle_crc32_update(upload_crc,data,size); return 0;
}
int media_link_file_finish(uint32_t crc) {
    if(!file_uploading) return -1;
    char arg[20]; snprintf(arg,sizeof(arg),"%08lX",(unsigned long)crc);
    int result=remaining||crc!=upload_crc?-1:request("FS_COMMIT",arg,NULL,0);
    if(result) request("FS_ABORT","",NULL,0);
    file_uploading=false; xSemaphoreGive(mutex); return result;
}
void media_link_file_abort(void) {
    if(!file_uploading) return;
    request("FS_ABORT","",NULL,0); file_uploading=false; xSemaphoreGive(mutex);
}
int media_link_file_delete(const char *path) {
    return media_link_path_valid(path)?simple("FS_DELETE",path):-1;
}
int media_link_profile_begin(const char *id,size_t size) {
    if(!walle_profile_id_valid(id)||!size||size>65536||!media_link_online()) return -1;
    if(xSemaphoreTake(mutex,pdMS_TO_TICKS(100))!=pdTRUE) return -1;
    char args[64]; snprintf(args,sizeof(args),"%s %u",id,(unsigned)size);
    if(request("PUT",args,NULL,0)) { xSemaphoreGive(mutex); return -1; }
    remaining=size; upload_crc=0; uploading=true; return 0;
}
int media_link_profile_write(const void *data,size_t size) {
    if(!uploading||!size||size>remaining||size>1024) return -1;
    char arg[20]; snprintf(arg,sizeof(arg),"%u",(unsigned)size);
    if(request("CHUNK",arg,data,size)) return -1;
    remaining-=size; upload_crc=walle_crc32_update(upload_crc,data,size); return 0;
}
int media_link_profile_finish(uint32_t crc) {
    if(!uploading) return -1;
    char arg[20]; snprintf(arg,sizeof(arg),"%08lX",(unsigned long)crc);
    int result=remaining||crc!=upload_crc?-1:request("COMMIT",arg,NULL,0);
    if(result) request("ABORT","",NULL,0);
    uploading=false; xSemaphoreGive(mutex); return result;
}
void media_link_profile_abort(void) {
    if(!uploading) return;
    request("ABORT","",NULL,0); uploading=false; xSemaphoreGive(mutex);
}
int media_link_profile_play(const char *id) {
    return walle_profile_id_valid(id)?simple("PROFILE_PLAY",id):-1;
}
int media_link_profile_delete(const char *id) {
    return walle_profile_id_valid(id)?simple("DELETE",id):-1;
}
int media_link_profile_read(const char *id,char **data,size_t *length) {
    return walle_profile_id_valid(id)?read_blob("GET",id,data,length,65536):-1;
}
