#include "walle_core.h"

#include <math.h>
#include <string.h>

static float slew(float current, float target, float max_delta)
{
    const float delta = target - current;
    if (delta > max_delta) return current + max_delta;
    if (delta < -max_delta) return current - max_delta;
    return target;
}

float walle_clampf(float value, float low, float high)
{
    if (!isfinite(value)) return 0.0f;
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

void walle_mix_tracks(float throttle, float steering, float *left, float *right)
{
    float l = walle_clampf(throttle - steering, -1.0f, 1.0f);
    float r = walle_clampf(throttle + steering, -1.0f, 1.0f);
    const float magnitude = fmaxf(fabsf(l), fabsf(r));
    if (magnitude > 1.0f) {
        l /= magnitude;
        r /= magnitude;
    }
    *left = l;
    *right = r;
}

void walle_control_init(walle_control_t *control)
{
    memset(control, 0, sizeof(*control));
    for (size_t i = 0; i < WALLE_JOINT_COUNT; ++i) {
        control->joint_target[i] = 0.5f;
        control->joint_current[i] = 0.5f;
    }
}

void walle_control_set_drive(walle_control_t *control, float throttle, float steering,
                             uint32_t now_ms)
{
    control->throttle = walle_clampf(throttle, -1.0f, 1.0f);
    control->steering = walle_clampf(steering, -1.0f, 1.0f);
    control->last_drive_command_ms = now_ms;
    control->drive_armed = true;
}

bool walle_control_set_joint(walle_control_t *control, size_t joint, float normalized)
{
    if (joint >= WALLE_JOINT_COUNT || !isfinite(normalized)) return false;
    control->joint_target[joint] = walle_clampf(normalized, 0.0f, 1.0f);
    return true;
}

void walle_control_stop(walle_control_t *control)
{
    control->throttle = 0.0f;
    control->steering = 0.0f;
    control->left_track = 0.0f;
    control->right_track = 0.0f;
    control->drive_armed = false;
    control->current_left = control->current_right = 0.0f;
    for (size_t i = 0; i < WALLE_JOINT_COUNT; ++i)
        control->joint_target[i] = control->joint_current[i];
}

void walle_control_step(walle_control_t *control,
                        const walle_servo_calibration_t calibration[WALLE_JOINT_COUNT],
                        uint32_t now_ms, float dt_seconds, walle_outputs_t *outputs)
{
    const bool expired = control->drive_armed &&
        ((uint32_t)(now_ms - control->last_drive_command_ms) > WALLE_COMMAND_TIMEOUT_MS);
    if (expired) walle_control_stop(control);

    walle_mix_tracks(control->throttle, control->steering,
                     &control->left_track, &control->right_track);

    const float drive_step = walle_clampf(dt_seconds * 2.5f, 0.0f, 1.0f);
    control->current_left = slew(control->current_left, control->left_track, drive_step);
    control->current_right = slew(control->current_right, control->right_track, drive_step);
    outputs->left_track = control->current_left;
    outputs->right_track = control->current_right;
    outputs->watchdog_stopped_drive = expired;

    const float joint_step = walle_clampf(dt_seconds * 1.5f, 0.0f, 1.0f);
    for (size_t i = 0; i < WALLE_JOINT_COUNT; ++i) {
        control->joint_current[i] = slew(control->joint_current[i],
                                         control->joint_target[i], joint_step);
        float n = calibration[i].inverted ? 1.0f - control->joint_current[i]
                                          : control->joint_current[i];
        float raw;
        if (n <= 0.5f) {
            const float span = (float)calibration[i].center_position -
                               calibration[i].min_position;
            raw = calibration[i].min_position + (n * 2.0f) * span;
        } else {
            const float span = (float)calibration[i].max_position -
                               calibration[i].center_position;
            raw = calibration[i].center_position + ((n - 0.5f) * 2.0f) * span;
        }
        outputs->servo_position[i] = (uint16_t)lroundf(raw);
    }
}

uint8_t walle_battery_percent(float voltage, unsigned cell_count)
{
    if (cell_count < 2 || cell_count > 5 || !isfinite(voltage)) return 0;
    const float per_cell = voltage / (float)cell_count;
    /* Conservative Li-ion resting-voltage approximation; never used as the BMS cutoff. */
    const float percent = (per_cell - 3.30f) * (100.0f / 0.90f);
    return (uint8_t)lroundf(walle_clampf(percent, 0.0f, 100.0f));
}
