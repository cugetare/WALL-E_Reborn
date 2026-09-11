#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "walle_core.h"

#define WALLE_CALIBRATION_VERSION 2U
#define WALLE_CALIBRATION_ALL_MASK ((1U << WALLE_JOINT_COUNT) - 1U)

typedef struct {
    uint32_t version;
    uint16_t assigned_mask;
    uint16_t calibrated_mask;
    bool enabled;
    walle_servo_calibration_t joints[WALLE_JOINT_COUNT];
} walle_calibration_profile_t;

void walle_calibration_defaults(walle_calibration_profile_t *profile);
bool walle_calibration_profile_valid(const walle_calibration_profile_t *profile);
bool walle_calibration_assign_servo(walle_calibration_profile_t *profile,
                                    size_t joint, uint8_t id);
bool walle_calibration_commissioning_complete(
    const walle_calibration_profile_t *profile);
bool walle_calibration_ready(const walle_calibration_profile_t *profile);
bool walle_calibration_set_joint_value(walle_calibration_profile_t *profile,
                                       size_t joint, uint16_t minimum,
                                       uint16_t rest, uint16_t maximum,
                                       bool inverted);
bool walle_calibration_finish_profile(walle_calibration_profile_t *profile);

/* Thread-safe runtime store. NVS is used in production and RAM in protocol tests. */
int walle_calibration_init(void);
void walle_calibration_snapshot(walle_calibration_profile_t *profile);
int walle_calibration_save_assignment(size_t joint, uint8_t id);
int walle_calibration_save_joint(size_t joint, uint16_t minimum, uint16_t rest,
                                 uint16_t maximum, bool inverted);
int walle_calibration_finish(void);
int walle_calibration_reset(void);
int walle_calibration_restore(const walle_calibration_profile_t *profile);
void walle_calibration_set_active(bool active);
bool walle_calibration_is_active(void);
