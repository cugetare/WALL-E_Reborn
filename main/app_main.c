#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "calibration.h"
#include "command_protocol.h"
#include "hardware.h"
#include "media_link.h"
#include "ota_update.h"
#include "panel_io_model.h"
#include "safety.h"
#include "servo_bus.h"
#include "web_server.h"
#include "wifi_manager.h"
#include "sdkconfig.h"
static QueueHandle_t commands;
static volatile bool battery_ok;
static volatile uint32_t motion_alive;
static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time()/1000); }
/* Never wait for HTTP, flash, media or the servo bus in the safety task. */
static void safety_task(void *arg) {
    (void)arg; TickType_t wake=xTaskGetTickCount();
    for (;;) {
        uint32_t now=now_ms();
        safety_inputs((hardware_read_buttons() & WALLE_BUTTON_STOP)!=0,
                      walle_ota_is_active(),battery_ok,now);
        if ((uint32_t)(now-motion_alive)>100) safety_stop();
        if (safety_snapshot().stopped) hardware_set_tracks(0,0);
        hardware_apply_tracks();
        xTaskDelayUntil(&wake,pdMS_TO_TICKS(5));
    }
}
static void control_task(void *arg) {
    (void)arg;
    walle_control_t control; walle_control_init(&control);
    walle_calibration_profile_t profile;
    uint16_t current[WALLE_JOINT_COUNT]={0},target[WALLE_JOINT_COUNT]={0},initialized=0;
    float fractional[WALLE_JOINT_COUNT]={0},step[WALLE_JOINT_COUNT]={0};
    uint32_t epoch=0,tick=0;
    bool was_stopped=true;
    walle_display_mode_t display_mode=WALLE_DISPLAY_PLAY;
    uint8_t previous_buttons=0;
    uint8_t stable_buttons=0;
    uint32_t buttons_changed=0;
    TickType_t wake=xTaskGetTickCount();
    for (;;) {
        motion_alive=now_ms();
        walle_calibration_snapshot(&profile);
        walle_safety_t safe=safety_snapshot();
        if(safe.epoch!=epoch) {
            epoch=safe.epoch; walle_control_stop(&control);
            for(size_t i=0;i<WALLE_JOINT_COUNT;i++) target[i]=current[i];
            initialized=0;
        }
        if(safe.stopped) {
            walle_control_stop(&control);
            if(!was_stopped) {
                /* Hold actual position, do not release a load-bearing neck. */
                for(size_t i=0;i<WALLE_JOINT_COUNT;i++) {
                    if(!(profile.assigned_mask & (1U<<i))) continue;
                    uint16_t position,load;
                    if(!servo_bus_read_telemetry(profile.joints[i].id,&position,&load) && position<=1023)
                        servo_bus_write_single_position(profile.joints[i].id,position,0,100);
                    motion_alive=now_ms();
                }
                media_link_request_stop();
            }
        }
        was_stopped=safe.stopped;
        walle_command_t command;
        for(unsigned n=0;n<4 && xQueueReceive(commands,&command,0)==pdTRUE;n++) {
            if(command.epoch!=safe.epoch || safe.stopped) continue;
            bool service=command.kind==WALLE_CMD_CALIB_MOVE;
            if(!safety_allowed(service)) continue;
            if(command.kind==WALLE_CMD_DRIVE)
                walle_control_set_drive(&control,command.a,command.b,now_ms());
            else if(command.kind==WALLE_CMD_JOINT || service) {
                size_t i=command.index;
                if(i>=WALLE_JOINT_COUNT || !(profile.assigned_mask & (1U<<i))) continue;
                if(!(initialized & (1U<<i))) {
                    uint16_t load;
                    if(servo_bus_read_telemetry(profile.joints[i].id,&current[i],&load) || current[i]>1023)
                        { safety_stop(); continue; }
                    initialized|=(uint16_t)(1U<<i);
                    fractional[i]=current[i];
                }
                if(service) target[i]=command.rest;
                else {
                    float p=profile.joints[i].inverted ? 1-command.a : command.a;
                    const walle_servo_calibration_t *c=&profile.joints[i];
                    target[i]=(uint16_t)lroundf(p<=.5f ? c->min_position+p*2*(c->center_position-c->min_position) :
                        c->center_position+(p-.5f)*2*(c->max_position-c->center_position));
                }
                step[i]=command.duration_ms?fminf(3.0f,fabsf((float)target[i]-current[i])*20.0f/command.duration_ms):3.0f;
                if(step[i]<.01f) step[i]=.01f;
            }
        }
        if(!safe.service && safety_allowed(false)) {
            walle_outputs_t out;
            walle_control_step(&control,profile.joints,now_ms(),.01f,&out);
            hardware_set_tracks(out.left_track,out.right_track);
            if(out.watchdog_stopped_drive) safety_stop();
        } else hardware_set_tracks(0,0);
        if(!(tick%2) && safety_allowed(safe.service)) {
            walle_servo_calibration_t selected[WALLE_JOINT_COUNT]={0};
            for(size_t i=0;i<WALLE_JOINT_COUNT;i++) if(initialized & (1U<<i)) {
                float delta=(float)target[i]-fractional[i];
                float increment=fminf(step[i],safe.service?1.6f:3.0f);
                fractional[i]+=fmaxf(-increment,fminf(increment,delta));
                current[i]=(uint16_t)lroundf(fractional[i]);
                selected[i]=profile.joints[i]; selected[i].max_speed=safe.service?80:150;
            }
            if(initialized && safety_allowed(safe.service) &&
               servo_bus_write_positions(selected,current,0)) safety_stop();
        }
        if(!(tick%5) && initialized && safety_allowed(safe.service)) {
            unsigned i=(tick/5)%WALLE_JOINT_COUNT;
            if(initialized & (1U<<i)) {
                uint16_t position,load;
                if(servo_bus_read_telemetry(profile.joints[i].id,&position,&load) ||
                   position>1023 || (load&1023)>800) {
                    safety_stop(); safety_verified(false,safe.calibrated);
                }
            }
        }
        uint8_t buttons=hardware_read_buttons();
        if(buttons!=previous_buttons) buttons_changed=now_ms();
        if((uint32_t)(now_ms()-buttons_changed)>=30 && buttons!=stable_buttons) {
            if((buttons & WALLE_BUTTON_ENGINEERING) && !(stable_buttons & WALLE_BUTTON_ENGINEERING))
                display_mode=display_mode==WALLE_DISPLAY_PLAY?WALLE_DISPLAY_ENGINEERING:WALLE_DISPLAY_PLAY;
            stable_buttons=buttons;
        }
        previous_buttons=buttons;
        if(!(tick%100)) {
            float voltage=hardware_read_battery_voltage();
            battery_ok=isfinite(voltage) && voltage>=CONFIG_WALLE_BATTERY_CELLS*3.3f &&
                voltage<=CONFIG_WALLE_BATTERY_CELLS*4.25f;
            walle_display_status_t status=!battery_ok?WALLE_DISPLAY_ERROR:
                safe.service?WALLE_DISPLAY_CALIBRATION:WALLE_DISPLAY_READY;
            hardware_display_status(walle_battery_percent(voltage,CONFIG_WALLE_BATTERY_CELLS),
                !safe.stopped,status,display_mode);
            hardware_display_diagnostics(voltage,safe.stopped,safe.bus_verified,media_link_online());
            hardware_set_panel_leds(WALLE_LED_SOLAR |
                ((display_mode==WALLE_DISPLAY_ENGINEERING || !battery_ok || safe.service)?WALLE_LED_ENGINEERING:0));
        }
        ++tick; motion_alive=now_ms();
        xTaskDelayUntil(&wake,pdMS_TO_TICKS(10));
    }
}
static void validate_boot_task(void *arg) {
    (void)arg; vTaskDelay(pdMS_TO_TICKS(30000));
    if((uint32_t)(now_ms()-motion_alive)<100) walle_ota_confirm_running_image();
    vTaskDelete(NULL);
}
void app_main(void) {
    /* An NVS error must never silently destroy calibration or credentials. */
    ESP_ERROR_CHECK(nvs_flash_init());
    safety_init();
    ESP_ERROR_CHECK(walle_calibration_init());
    ESP_ERROR_CHECK(hardware_init());
    ESP_ERROR_CHECK(servo_bus_init());
    ESP_ERROR_CHECK(media_link_init());
    commands=xQueueCreate(24,sizeof(walle_command_t)); configASSERT(commands);
    motion_alive=now_ms();
    configASSERT(xTaskCreatePinnedToCore(safety_task,"safety",3072,NULL,20,NULL,1)==pdPASS);
    configASSERT(xTaskCreatePinnedToCore(control_task,"motion",6144,NULL,12,NULL,1)==pdPASS);
    ESP_ERROR_CHECK(walle_wifi_start());
    ESP_ERROR_CHECK(walle_web_start(commands));
    configASSERT(xTaskCreate(validate_boot_task,"boot_health",3072,NULL,3,NULL)==pdPASS);
}
