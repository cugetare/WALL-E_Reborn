#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WALLE_JOINT_COUNT 9
#define WALLE_COMMAND_TIMEOUT_MS 300U

typedef enum {
    WALLE_JOINT_ARM_LEFT = 0,
    WALLE_JOINT_ARM_RIGHT,
    WALLE_JOINT_NECK_TILT,
    WALLE_JOINT_HEAD_TILT,
    WALLE_JOINT_HEAD_PAN,
    WALLE_JOINT_EYE_LEFT,
    WALLE_JOINT_EYE_RIGHT,
    WALLE_JOINT_BROW_LEFT,
    WALLE_JOINT_BROW_RIGHT,
} walle_joint_t;

typedef struct {
    uint8_t id;
    uint16_t min_position;
    uint16_t max_position;
    uint16_t center_position;
    uint16_t max_speed;
    bool inverted;
} walle_servo_calibration_t;

typedef struct {
    float throttle;
    float steering;
    float left_track;
    float right_track;
    float current_left;
    float current_right;
    float joint_target[WALLE_JOINT_COUNT];
    float joint_current[WALLE_JOINT_COUNT];
    uint32_t last_drive_command_ms;
    bool drive_armed;
} walle_control_t;

typedef struct {
    float left_track;
    float right_track;
    uint16_t servo_position[WALLE_JOINT_COUNT];
    bool watchdog_stopped_drive;
} walle_outputs_t;

void walle_control_init(walle_control_t *control);
void walle_control_set_drive(walle_control_t *control, float throttle, float steering,
                             uint32_t now_ms);
bool walle_control_set_joint(walle_control_t *control, size_t joint, float normalized);
void walle_control_stop(walle_control_t *control);
void walle_control_step(walle_control_t *control,
                        const walle_servo_calibration_t calibration[WALLE_JOINT_COUNT],
                        uint32_t now_ms, float dt_seconds, walle_outputs_t *outputs);
float walle_clampf(float value, float low, float high);
void walle_mix_tracks(float throttle, float steering, float *left, float *right);
uint8_t walle_battery_percent(float voltage, unsigned cell_count);

#ifdef __cplusplus
}
#endif
