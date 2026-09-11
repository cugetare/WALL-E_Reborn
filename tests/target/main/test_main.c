#include <math.h>
#include <stdio.h>
#include <string.h>

#include "calibration.h"
#include "automation.h"
#include "command_protocol.h"
#include "display_model.h"
#include "nvs_flash.h"
#include "ota_policy.h"
#include "panel_io_model.h"
#include "profile_catalog.h"
#include "servo_bus.h"
#include "status_led.h"
#include "walle_core.h"
#include "wifi_manager.h"
#include "safety.h"
#include "choreography.h"

static unsigned s_failures;
static unsigned s_checks;
#define CHECK(condition) do { ++s_checks; if (!(condition)) { \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); ++s_failures; \
} } while (0)

static void test_track_mixer_and_watchdog(void)
{
    float left, right;
    walle_mix_tracks(1.0f, 1.0f, &left, &right);
    CHECK(fabsf(left) < 0.001f);
    CHECK(fabsf(right - 1.0f) < 0.001f);

    walle_servo_calibration_t cal[WALLE_JOINT_COUNT] = {0};
    for (size_t i = 0; i < WALLE_JOINT_COUNT; ++i) {
        cal[i].id = (uint8_t)(i + 1);
        cal[i].min_position = 100;
        cal[i].center_position = 500;
        cal[i].max_position = 900;
    }
    walle_control_t control;
    walle_outputs_t out;
    walle_control_init(&control);
    walle_control_set_drive(&control, 1.0f, 0.0f, 1000);
    walle_control_step(&control, cal, 1100, 0.1f, &out);
    CHECK(out.left_track > 0.24f && out.left_track < 0.26f);
    walle_control_step(&control, cal, 1401, 0.1f, &out);
    CHECK(out.watchdog_stopped_drive);
    CHECK(!control.drive_armed);
    CHECK(out.left_track == 0 && out.right_track == 0);
    control.current_left=1;control.current_right=-1;
    control.joint_current[0]=.2f;control.joint_target[0]=.9f;
    walle_control_stop(&control);
    CHECK(control.current_left==0 && control.current_right==0);
    CHECK(control.joint_target[0]==control.joint_current[0]);
}

static void test_commands(void)
{
    walle_command_t command;
    CHECK(walle_command_parse("drive,-0.5,0.25", 15, &command));
    CHECK(command.kind == WALLE_CMD_DRIVE);
    CHECK(fabsf(command.a + 0.5f) < 0.001f);
    CHECK(walle_command_parse("joint,7,1", 9, &command));
    CHECK(command.kind == WALLE_CMD_JOINT && command.index == 7);
    CHECK(walle_command_parse("sound,17", 8, &command));
    CHECK(command.kind == WALLE_CMD_SOUND && command.index == 17);
    CHECK(!walle_command_parse("drive,2,0", 9, &command));
    CHECK(!walle_command_parse("drive,nan,0",11,&command));
    CHECK(!walle_command_parse("drive,inf,0",11,&command));
    CHECK(!walle_command_parse("joint,0,nan",11,&command));
    CHECK(!walle_command_parse("stop\0junk",9,&command));
    CHECK(walle_command_parse("joint,8,0.5", 11, &command));
    CHECK(!walle_command_parse("joint,9,0.5", 11, &command));
    CHECK(walle_command_parse("calib,move,3,512", 16, &command));
    CHECK(command.kind == WALLE_CMD_CALIB_MOVE && command.index == 3 && command.rest == 512);
    CHECK(walle_command_parse("calib,set,3,100,510,900,1", 25, &command));
    CHECK(command.kind == WALLE_CMD_CALIB_SET && command.minimum == 100 &&
          command.rest == 510 && command.maximum == 900 && command.inverted);
    CHECK(walle_command_parse("calib,finish", 12, &command));
    CHECK(command.kind == WALLE_CMD_CALIB_FINISH);
    CHECK(walle_command_parse("calib,move,2,1001", 17, &command));
    CHECK(!walle_command_parse("calib,move,2,1024", 17, &command));
}

static void test_protocol_frames(void)
{
    walle_servo_calibration_t cal[WALLE_JOINT_COUNT] = {0};
    uint16_t positions[WALLE_JOINT_COUNT] = {0};
    for (size_t i = 0; i < WALLE_JOINT_COUNT; ++i) {
        cal[i].id = (uint8_t)(i + 1);
        cal[i].max_speed = 300;
        cal[i].center_position = 512;
        positions[i] = (uint16_t)(400 + i);
    }
    uint8_t packet[WALLE_SERVO_PACKET_MAX];
    const size_t length = servo_build_sync_position_packet(cal, positions, 20,
                                                            packet, sizeof(packet));
    CHECK(length == 71); /* all nine joints are serial bus servos */
    CHECK(packet[0] == 0xFF && packet[1] == 0xFF && packet[2] == 0xFE);
    CHECK(packet[4] == 0x83 && packet[5] == 42 && packet[6] == 6);
    CHECK(packet[8]==0x01 && packet[9]==0x90); /* 400, big-endian SC */
    CHECK(packet[10]==0 && packet[11]==20);
    CHECK(packet[12]==0x01 && packet[13]==0x2C); /* speed 300 */
    unsigned checksum_sum = 0;
    for (size_t i = 2; i < length; ++i) checksum_sum += packet[i];
    CHECK((checksum_sum & 0xFFU) == 0xFFU);

    uint8_t ping[6];
    CHECK(servo_build_ping_packet(1, ping, sizeof(ping)) == 6);
    CHECK(ping[2] == 1 && ping[3] == 2 && ping[4] == 1);
    uint8_t write_id[8];
    CHECK(servo_build_write_byte_packet(1, 5, 2, write_id, sizeof(write_id)) == 8);
    CHECK(write_id[2] == 1 && write_id[4] == 3 && write_id[5] == 5 && write_id[6] == 2);
    uint8_t status_frame[] = {0xFF,0xFF,2,4,0,0x34,0x12,0};
    unsigned status_sum = 0;
    for (size_t i = 2; i < sizeof(status_frame) - 1; ++i) status_sum += status_frame[i];
    status_frame[7] = (uint8_t)~status_sum;
    walle_servo_status_t status;
    CHECK(servo_parse_status_packet(status_frame, sizeof(status_frame), &status));
    CHECK(status.id == 2 && status.parameter_count == 2 && status.parameters[0] == 0x34);
}

static void test_command_to_actuator_integration(void)
{
    walle_command_t command;
    CHECK(walle_command_parse("joint,3,0.75", 12, &command));
    walle_control_t control;
    walle_control_init(&control);
    CHECK(walle_control_set_joint(&control, command.index, command.a));
    walle_servo_calibration_t cal[WALLE_JOINT_COUNT] = {0};
    for (size_t i = 0; i < WALLE_JOINT_COUNT; ++i) {
        cal[i].id = (uint8_t)(i + 1);
        cal[i].min_position = 100;
        cal[i].center_position = 500;
        cal[i].max_position = 900;
        cal[i].max_speed = 300;
    }
    walle_outputs_t output;
    for (unsigned i = 0; i < 100; ++i) {
        walle_control_step(&control, cal, i * 10, 0.01f, &output);
    }
    CHECK(output.servo_position[3] == 700);
    uint8_t packet[WALLE_SERVO_PACKET_MAX];
    CHECK(servo_build_sync_position_packet(cal, output.servo_position, 20,
                                            packet, sizeof(packet)) > 0);
}

static void test_assisted_calibration_lifecycle(void)
{
    walle_calibration_profile_t profile;
    walle_calibration_defaults(&profile);
    CHECK(!profile.enabled);
    CHECK(profile.assigned_mask == 0);
    CHECK(profile.calibrated_mask == 0);
    CHECK(profile.joints[WALLE_JOINT_ARM_LEFT].center_position == 512);
    CHECK(profile.joints[WALLE_JOINT_HEAD_TILT].center_position == 512);
    CHECK(walle_calibration_profile_valid(&profile));
    CHECK(!walle_calibration_finish_profile(&profile));
    CHECK(!walle_calibration_set_joint_value(&profile, 0, 600, 500, 800, false));

    for (size_t i = 0; i < WALLE_JOINT_COUNT; ++i) {
        CHECK(walle_calibration_assign_servo(&profile, i, (uint8_t)(i + 2)));
        CHECK(walle_calibration_set_joint_value(&profile, i, 100, 500,
                                                900, (i & 1U) != 0));
    }
    CHECK(profile.calibrated_mask == WALLE_CALIBRATION_ALL_MASK);
    CHECK(!profile.enabled);
    CHECK(walle_calibration_finish_profile(&profile));
    CHECK(profile.enabled);
    CHECK(walle_calibration_ready(&profile));

    walle_control_t control;
    walle_outputs_t output;
    walle_control_init(&control);
    walle_control_step(&control, profile.joints, 0, 0.01f, &output);
    for (size_t i = 0; i < WALLE_JOINT_COUNT; ++i)
        CHECK(output.servo_position[i] == 500); /* First boot/rest position. */
}

static void test_calibration_nvs_integration(void)
{
    CHECK(walle_calibration_reset() == 0);
    CHECK(walle_calibration_save_joint(WALLE_JOINT_HEAD_PAN, 123, 501, 877,
                                       true) == 0);
    CHECK(walle_calibration_init() == 0); /* Reload the persisted blob. */
    walle_calibration_profile_t loaded;
    walle_calibration_snapshot(&loaded);
    CHECK((loaded.calibrated_mask & (1U << WALLE_JOINT_HEAD_PAN)) != 0);
    CHECK(loaded.joints[WALLE_JOINT_HEAD_PAN].min_position == 123);
    CHECK(loaded.joints[WALLE_JOINT_HEAD_PAN].center_position == 501);
    CHECK(loaded.joints[WALLE_JOINT_HEAD_PAN].max_position == 877);
    CHECK(loaded.joints[WALLE_JOINT_HEAD_PAN].inverted);
    CHECK(walle_calibration_reset() == 0);
}

static void test_ota_policy(void)
{
    CHECK(walle_ota_validate_request(0, 0x400000) == WALLE_OTA_REQUEST_EMPTY);
    CHECK(walle_ota_validate_request(0x400001, 0x400000) == WALLE_OTA_REQUEST_TOO_LARGE);
    CHECK(walle_ota_validate_request(0x400000, 0x400000) == WALLE_OTA_REQUEST_OK);
    CHECK(walle_ota_progress_percent(0, 1000) == 0);
    CHECK(walle_ota_progress_percent(499, 1000) == 49);
    CHECK(walle_ota_progress_percent(1000, 1000) == 100);
    CHECK(walle_ota_progress_percent(1200, 1000) == 100);
    CHECK(walle_ota_motion_allowed(false));
    CHECK(!walle_ota_motion_allowed(true));
}

static void test_ota_motion_interlock_integration(void)
{
    walle_command_t command;
    walle_control_t control;
    walle_control_init(&control);
    CHECK(walle_command_parse("drive,1,0", 9, &command));

    const bool update_active = true;
    if (walle_ota_motion_allowed(update_active))
        walle_control_set_drive(&control, command.a, command.b, 1000);
    CHECK(!control.drive_armed);

    walle_servo_calibration_t calibration[WALLE_JOINT_COUNT] = {0};
    walle_outputs_t output;
    walle_control_step(&control, calibration, 1010, 0.01f, &output);
    CHECK(output.left_track == 0.0f && output.right_track == 0.0f);
}

static void test_st7789_status_screen_model(void)
{
    /* Header colour changes with firmware state. */
    CHECK(walle_display_pixel(10, 10, 50, false, WALLE_DISPLAY_BOOT,
                              WALLE_DISPLAY_ENGINEERING) !=
          walle_display_pixel(10, 10, 50, false, WALLE_DISPLAY_ERROR,
                              WALLE_DISPLAY_ENGINEERING));
    CHECK(walle_display_pixel(10, 10, 50, false, WALLE_DISPLAY_OTA,
                              WALLE_DISPLAY_ENGINEERING) !=
          walle_display_pixel(10, 10, 50, false, WALLE_DISPLAY_READY,
                              WALLE_DISPLAY_ENGINEERING));
    /* The same interior pixel is empty at 0% and filled at 100%. */
    CHECK(walle_display_pixel(100, 100, 0, false, WALLE_DISPLAY_READY,
                              WALLE_DISPLAY_ENGINEERING) !=
          walle_display_pixel(100, 100, 100, false, WALLE_DISPLAY_READY,
                              WALLE_DISPLAY_ENGINEERING));
    /* Motion state changes the bottom indicator. */
    CHECK(walle_display_pixel(100, 240, 50, false, WALLE_DISPLAY_READY,
                              WALLE_DISPLAY_ENGINEERING) !=
          walle_display_pixel(100, 240, 50, true, WALLE_DISPLAY_READY,
                              WALLE_DISPLAY_ENGINEERING));
    /* Play mode has a yellow sun and battery bar, but no motion indicator. */
    CHECK(walle_display_pixel(62, 108, 50, false, WALLE_DISPLAY_READY,
                              WALLE_DISPLAY_PLAY) == 0xFFE0U);
    CHECK(walle_display_pixel(14, 18, 50, false, WALLE_DISPLAY_READY,
                              WALLE_DISPLAY_PLAY) == 0xFFE0U);
    CHECK(walle_display_pixel(100, 220, 0, false, WALLE_DISPLAY_READY,
                              WALLE_DISPLAY_PLAY) !=
          walle_display_pixel(100, 220, 100, false, WALLE_DISPLAY_READY,
                              WALLE_DISPLAY_PLAY));
    CHECK(walle_display_pixel(52, 220, 1, false, WALLE_DISPLAY_READY,
                              WALLE_DISPLAY_PLAY) == 0xFFE0U);
    CHECK(walle_display_pixel(52, 220, 100, false, WALLE_DISPLAY_READY,
                              WALLE_DISPLAY_PLAY) == 0xFFE0U);
    CHECK(walle_display_pixel(100, 240, 50, false, WALLE_DISPLAY_READY,
                              WALLE_DISPLAY_PLAY) ==
          walle_display_pixel(100, 240, 50, true, WALLE_DISPLAY_READY,
                              WALLE_DISPLAY_PLAY));

    CHECK(!walle_display_battery_warning(8.0f, 50));
    CHECK(walle_display_battery_warning(6.6f, 0));
    CHECK(walle_display_battery_warning(NAN, 50));
    CHECK(walle_display_battery_warning(0.0f, 50));
}

static void test_panel_led_policy(void)
{
    CHECK(walle_panel_led_mask(0, WALLE_DISPLAY_PLAY, WALLE_DISPLAY_READY) ==
          (WALLE_LED_SOLAR | WALLE_LED_PLAY));
    CHECK(walle_panel_led_mask(WALLE_BUTTON_REC, WALLE_DISPLAY_PLAY,
                               WALLE_DISPLAY_READY) ==
          (WALLE_LED_SOLAR | WALLE_LED_PLAY | WALLE_LED_REC));
    CHECK(walle_panel_led_mask(0, WALLE_DISPLAY_ENGINEERING,
                               WALLE_DISPLAY_READY) ==
          (WALLE_LED_SOLAR | WALLE_LED_ENGINEERING));
    CHECK(walle_panel_led_mask(WALLE_BUTTON_STOP, WALLE_DISPLAY_PLAY,
                               WALLE_DISPLAY_READY) ==
          (WALLE_LED_SOLAR | WALLE_LED_PLAY | WALLE_LED_STOP));
    CHECK(walle_panel_led_mask(0, WALLE_DISPLAY_PLAY, WALLE_DISPLAY_ERROR) ==
          (WALLE_LED_SOLAR | WALLE_LED_STOP | WALLE_LED_ENGINEERING));
    CHECK(walle_panel_led_mask(0, WALLE_DISPLAY_PLAY, WALLE_DISPLAY_OTA) ==
          (WALLE_LED_SOLAR | WALLE_LED_ENGINEERING));
}

static void test_choreography_automation_engine(void)
{
    walle_automation_engine_t engine;
    walle_automation_engine_init(&engine);
    walle_automation_t triggers = {
        .id = 1, .name = "Both triggers", .profile_id = "hello_eve",
        .left_trigger_min = 920, .right_trigger_min = 920,
        .hold_ms = 600, .cooldown_ms = 1000, .enabled = true,
    };
    CHECK(walle_automation_set(&engine, &triggers));
    walle_controller_state_t state = {.left_trigger = 1000, .right_trigger = 1000};
    CHECK(walle_automation_process(&engine, &state, 1000, true) == NULL);
    CHECK(walle_automation_process(&engine, &state, 1599, true) == NULL);
    const walle_automation_t *fired = walle_automation_process(&engine, &state, 1600, true);
    CHECK(fired && strcmp(fired->profile_id, "hello_eve") == 0);
    CHECK(walle_automation_process(&engine, &state, 3000, true) == NULL); /* Must release. */
    state.left_trigger = state.right_trigger = 0;
    CHECK(walle_automation_process(&engine, &state, 3010, true) == NULL);
    state.left_trigger = state.right_trigger = 1000;
    CHECK(walle_automation_process(&engine, &state, 3020, false) == NULL); /* Safety gate. */
    CHECK(walle_automation_process(&engine, &state, 3020, true) == NULL);
    CHECK(walle_automation_process(&engine, &state, 3620, true) == NULL);
    state.left_trigger=state.right_trigger=0;
    CHECK(walle_automation_process(&engine,&state,3630,true)==NULL);
    state.left_trigger=state.right_trigger=1000;
    CHECK(walle_automation_process(&engine,&state,3640,true)==NULL);
    CHECK(walle_automation_process(&engine,&state,4240,true)!=NULL);

    walle_automation_t l3r3 = {
        .id = 2, .name = "Stick clicks", .profile_id = "dance_1",
        .buttons = 0x300, .hold_ms = 350, .cooldown_ms = 500,
        .require_neutral_sticks = true, .enabled = true,
    };
    CHECK(walle_automation_set(&engine, &l3r3));
    state = (walle_controller_state_t){.buttons = 0x300, .left_x = 7000};
    CHECK(walle_automation_process(&engine, &state, 5000, true) == NULL);
    state.left_x = 0;
    CHECK(walle_automation_process(&engine, &state, 5100, true) == NULL);
    fired = walle_automation_process(&engine, &state, 5450, true);
    CHECK(fired && fired->id == 2);
    CHECK(walle_automation_remove(&engine, 1));
    CHECK(engine.count == 1 && engine.rules[0].id == 2);
}

static void test_profile_catalog_and_crc(void)
{
    walle_profile_catalog_t catalog;
    walle_profile_catalog_init(&catalog);
    CHECK(walle_crc32_update(0, "123456789", 9) == 0xCBF43926U);
    uint32_t crc = walle_crc32_update(0, "1234", 4);
    crc = walle_crc32_update(crc, "56789", 5);
    CHECK(crc == 0xCBF43926U);
    walle_profile_info_t profile = {
        .id = "hello_eve", .name = "Hello EVE", .size = 1234, .crc32 = crc,
    };
    CHECK(walle_profile_catalog_set(&catalog, &profile));
    CHECK(walle_profile_catalog_contains(&catalog, "hello_eve"));
    profile.size = 2345;
    CHECK(walle_profile_catalog_set(&catalog, &profile));
    CHECK(catalog.count == 1 && catalog.entries[0].size == 2345);
    CHECK(!walle_profile_id_valid("../bad"));
    CHECK(walle_profile_catalog_remove(&catalog, "hello_eve"));
    CHECK(catalog.count == 0);
}

static void test_wifi_credentials_validation(void)
{
    CHECK(walle_wifi_credentials_valid("Home", "password"));
    CHECK(walle_wifi_credentials_valid("Open network", ""));
    CHECK(!walle_wifi_credentials_valid("", "password"));
    CHECK(!walle_wifi_credentials_valid("Home", "short"));
    CHECK(!walle_wifi_credentials_valid(NULL, "password"));
    char long_ssid[34];
    memset(long_ssid, 'x', sizeof(long_ssid));
    long_ssid[sizeof(long_ssid) - 1] = 0;
    CHECK(!walle_wifi_credentials_valid(long_ssid, "password"));
}

static void test_network_status_led_colors(void)
{
    walle_status_led_rgb_t rgb = walle_status_led_color(WALLE_STATUS_LED_AP);
    CHECK(rgb.red == 0 && rgb.green == 0 && rgb.blue > 0);
    rgb = walle_status_led_color(WALLE_STATUS_LED_WIFI);
    CHECK(rgb.red == 0 && rgb.green > 0 && rgb.blue == 0);
    rgb = walle_status_led_color(WALLE_STATUS_LED_OFF);
    CHECK(rgb.red == 0 && rgb.green == 0 && rgb.blue == 0);
}

static void test_safety_supervisor(void) {
    safety_init();
    CHECK(safety_snapshot().stopped);
    CHECK(!safety_allowed(false));
    CHECK(safety_arm(1000)); /* USB-only assembly/service is allowed. */
    CHECK(safety_allowed(true));
    uint32_t epoch=safety_snapshot().epoch;
    safety_stop();
    CHECK(safety_snapshot().epoch!=epoch);
    safety_heartbeat(1100);
    CHECK(!safety_allowed(true)); /* Heartbeat is not rearm. */
    CHECK(safety_arm(1200));
    safety_inputs(false,false,false,1501);
    CHECK(safety_snapshot().stopped);
    safety_service(false);
    CHECK(!safety_arm(1600));
    safety_verified(true,true);
    safety_inputs(false,false,true,1700);
    CHECK(safety_arm(1700));
    CHECK(safety_allowed(false));
    CHECK(!safety_allowed(true));
    safety_inputs(true,false,true,1701);
    CHECK(!safety_allowed(false));
    CHECK(!safety_arm(1702));
    safety_inputs(false,false,true,1703);
    CHECK(safety_snapshot().stopped);
    CHECK(safety_arm(1704));
    safety_inputs(false,true,true,1705);
    CHECK(safety_snapshot().stopped);
    CHECK(!safety_arm(1706));
    safety_inputs(false,false,true,1707);
    CHECK(safety_arm(1708));
    safety_verified(false,true);
    CHECK(safety_snapshot().stopped);
    CHECK(!safety_arm(1709));
    safety_verified(true,true);
    CHECK(safety_arm(1800));
    safety_inputs(false,false,false,1801);
    CHECK(safety_snapshot().stopped);
    CHECK(walle_battery_percent(12.6f,3)==100);
    CHECK(walle_battery_percent(9.9f,3)==0);
    for(unsigned y=0;y<280;y++) for(unsigned x=0;x<240;x++)
        if(walle_display_pixel(x,y,80,false,WALLE_DISPLAY_READY,WALLE_DISPLAY_PLAY)==0xF800) {
            CHECK(false); return;
        }
    CHECK(true); /* No red pixels anywhere in Play. */
}

static void test_choreography_validation(void) {
    char error[96];
    const char *valid="{\"schema\":\"walle.choreography/v1\",\"durationMs\":2000,\"events\":[{\"atMs\":0,\"type\":\"joint\",\"joint\":\"head_pan\",\"value\":0.6,\"durationMs\":1000}]}";
    CHECK(choreography_validate(valid,strlen(valid),error,sizeof(error)));
    const char *bad="{\"schema\":\"walle.choreography/v1\",\"durationMs\":2000,\"events\":[{\"atMs\":0,\"type\":\"joint\",\"joint\":\"head_pan\",\"value\":1.6,\"durationMs\":1000}]}";
    CHECK(!choreography_validate(bad,strlen(bad),error,sizeof(error)));
    bad="{\"schema\":\"walle.choreography/v1\",\"durationMs\":100,\"events\":[{\"atMs\":0,\"type\":\"drive\",\"throttle\":0.1,\"steering\":0,\"durationMs\":200}]}";
    CHECK(!choreography_validate(bad,strlen(bad),error,sizeof(error)));
    bad="{\"schema\":\"walle.choreography/v1\",\"durationMs\":100,\"events\":[{\"atMs\":0,\"type\":\"audio\",\"asset\":\"../secret.wav\"}]}";
    CHECK(!choreography_validate(bad,strlen(bad),error,sizeof(error)));
    CHECK(!choreography_validate("{\"events\":[]}",13,error,sizeof(error)));
    CHECK(!choreography_validate("[[[[[[[[[[]]]]]]]]]]",20,error,sizeof(error)));
    bad="{\"schema\":\"walle.choreography/v1\",\"durationMs\":2000,\"events\":[{\"atMs\":0,\"type\":\"joint\",\"joint\":\"head_pan\",\"value\":0.6,\"durationMs\":1000},{\"atMs\":500,\"type\":\"joint\",\"joint\":\"head_pan\",\"value\":0.7,\"durationMs\":1000}]}";
    CHECK(!choreography_validate(bad,strlen(bad),error,sizeof(error)));
}

void app_main(void)
{
    puts("WALL-E Reborn target tests");
    esp_err_t nvs = nvs_flash_init();
    if (nvs == ESP_ERR_NVS_NO_FREE_PAGES || nvs == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        CHECK(nvs_flash_erase() == ESP_OK);
        nvs = nvs_flash_init();
    }
    CHECK(nvs == ESP_OK);
    CHECK(walle_calibration_init() == 0);
    test_track_mixer_and_watchdog();
    test_commands();
    test_protocol_frames();
    test_command_to_actuator_integration();
    test_assisted_calibration_lifecycle();
    test_calibration_nvs_integration();
    test_ota_policy();
    test_ota_motion_interlock_integration();
    test_st7789_status_screen_model();
    test_panel_led_policy();
    test_choreography_automation_engine();
    test_profile_catalog_and_crc();
    test_wifi_credentials_validation();
    test_network_status_led_colors();
    test_safety_supervisor();
    test_choreography_validation();
    CHECK(walle_battery_percent(9.9f, 3) == 0);
    CHECK(walle_battery_percent(12.6f, 3) == 100);
    printf("CHECKS: %u\n",s_checks);
    if (s_failures == 0) puts("ALL TESTS PASSED");
    else printf("TESTS FAILED: %u\n", s_failures);
    fflush(stdout);
    for (;;) { }
}
