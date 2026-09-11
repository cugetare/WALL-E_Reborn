#include "calibration.h"

#include <string.h>

#if defined(ESP_PLATFORM)
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "nvs.h"
#endif

static walle_calibration_profile_t s_profile;
static bool s_active;

#if defined(ESP_PLATFORM)
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
#define LOCK() taskENTER_CRITICAL(&s_lock)
#define UNLOCK() taskEXIT_CRITICAL(&s_lock)
#else
#define LOCK() ((void)0)
#define UNLOCK() ((void)0)
#endif

void walle_calibration_defaults(walle_calibration_profile_t *profile)
{
    memset(profile, 0, sizeof(*profile));
    profile->version = WALLE_CALIBRATION_VERSION;
    const walle_servo_calibration_t defaults[WALLE_JOINT_COUNT] = {
        {.min_position = 0, .max_position = 1023, .center_position = 512, .max_speed = 300},
        {.min_position = 0, .max_position = 1023, .center_position = 512, .max_speed = 300, .inverted = true},
        {.min_position = 0, .max_position = 1023, .center_position = 512, .max_speed = 180},
        {.min_position = 0, .max_position = 1023, .center_position = 512, .max_speed = 220},
        {.min_position = 0, .max_position = 1023, .center_position = 512, .max_speed = 250},
        {.min_position = 0, .max_position = 1023, .center_position = 512, .max_speed = 350},
        {.min_position = 0, .max_position = 1023, .center_position = 512, .max_speed = 350, .inverted = true},
        {.min_position = 0, .max_position = 1023, .center_position = 512, .max_speed = 300},
        {.min_position = 0, .max_position = 1023, .center_position = 512, .max_speed = 300, .inverted = true},
    };
    memcpy(profile->joints, defaults, sizeof(defaults));
}

static uint16_t joint_limit(size_t joint) { (void)joint; return 1023U; }

bool walle_calibration_commissioning_complete(
    const walle_calibration_profile_t *profile)
{
    return profile && profile->assigned_mask == WALLE_CALIBRATION_ALL_MASK;
}

bool walle_calibration_ready(const walle_calibration_profile_t *profile)
{
    return profile && profile->enabled &&
        walle_calibration_commissioning_complete(profile) &&
        profile->calibrated_mask == WALLE_CALIBRATION_ALL_MASK;
}

bool walle_calibration_assign_servo(walle_calibration_profile_t *profile,
                                    size_t joint, uint8_t id)
{
    if (!profile || joint >= WALLE_JOINT_COUNT || id < 2U || id > 253U)
        return false;
    for (size_t i = 0; i < WALLE_JOINT_COUNT; ++i) {
        if (i != joint && (profile->assigned_mask & (1U << i)) != 0U &&
            profile->joints[i].id == id) return false;
    }
    if (profile->joints[joint].id != id)
        profile->calibrated_mask &= (uint16_t)~(1U << joint);
    profile->joints[joint].id = id;
    profile->assigned_mask |= (uint16_t)(1U << joint);
    profile->enabled = false;
    return true;
}

bool walle_calibration_profile_valid(const walle_calibration_profile_t *profile)
{
    if (!profile || profile->version != WALLE_CALIBRATION_VERSION ||
        (profile->assigned_mask & ~WALLE_CALIBRATION_ALL_MASK) ||
        (profile->calibrated_mask & ~WALLE_CALIBRATION_ALL_MASK)) return false;
    if (profile->enabled &&
        (!walle_calibration_commissioning_complete(profile) ||
         profile->calibrated_mask != WALLE_CALIBRATION_ALL_MASK))
        return false;
    for (size_t i = 0; i < WALLE_JOINT_COUNT; ++i) {
        const walle_servo_calibration_t *joint = &profile->joints[i];
        if (joint->min_position >= joint->center_position ||
            joint->center_position >= joint->max_position ||
            joint->max_position > joint_limit(i)) return false;
        const bool assigned = (profile->assigned_mask & (1U << i)) != 0U;
        if (assigned != (joint->id >= 2U && joint->id <= 253U)) return false;
        if (assigned) {
            for (size_t j = i + 1; j < WALLE_JOINT_COUNT; ++j) {
                if ((profile->assigned_mask & (1U << j)) != 0U &&
                    profile->joints[j].id == joint->id) return false;
            }
        }
    }
    return true;
}

bool walle_calibration_set_joint_value(walle_calibration_profile_t *profile,
                                       size_t joint, uint16_t minimum,
                                       uint16_t rest, uint16_t maximum,
                                       bool inverted)
{
    if (!profile || joint >= WALLE_JOINT_COUNT || minimum >= rest ||
        rest >= maximum || maximum > joint_limit(joint)) return false;
    profile->joints[joint].min_position = minimum;
    profile->joints[joint].center_position = rest;
    profile->joints[joint].max_position = maximum;
    profile->joints[joint].inverted = inverted;
    profile->calibrated_mask |= (uint16_t)(1U << joint);
    profile->enabled = false;
    return true;
}

bool walle_calibration_finish_profile(walle_calibration_profile_t *profile)
{
    if (!profile || profile->calibrated_mask != WALLE_CALIBRATION_ALL_MASK ||
        !walle_calibration_commissioning_complete(profile) ||
        !walle_calibration_profile_valid(profile)) return false;
    profile->enabled = true;
    return true;
}

#if defined(ESP_PLATFORM)
static int persist(const walle_calibration_profile_t *profile)
{
    nvs_handle_t handle;
    esp_err_t error = nvs_open("walle", NVS_READWRITE, &handle);
    if (error != ESP_OK) return error;
    error = nvs_set_blob(handle, "cal_v2", profile, sizeof(*profile));
    if (error == ESP_OK) error = nvs_commit(handle);
    nvs_close(handle);
    return error;
}
#else
static int persist(const walle_calibration_profile_t *profile)
{
    (void)profile;
    return 0;
}
#endif

int walle_calibration_init(void)
{
    walle_calibration_profile_t loaded;
    walle_calibration_defaults(&loaded);
#if defined(ESP_PLATFORM)
    nvs_handle_t handle;
    if (nvs_open("walle", NVS_READONLY, &handle) == ESP_OK) {
        size_t size = sizeof(loaded);
        if (nvs_get_blob(handle, "cal_v2", &loaded, &size) != ESP_OK ||
            size != sizeof(loaded) || !walle_calibration_profile_valid(&loaded)) {
            walle_calibration_defaults(&loaded);
        }
        nvs_close(handle);
    }
#endif
    LOCK();
    s_profile = loaded;
    s_active = !walle_calibration_ready(&loaded);
    UNLOCK();
    return 0;
}

int walle_calibration_save_assignment(size_t joint, uint8_t id)
{
    walle_calibration_profile_t updated;
    LOCK();
    updated = s_profile;
    UNLOCK();
    if (!walle_calibration_assign_servo(&updated, joint, id)) return -1;
    const int error = persist(&updated);
    if (error) return error;
    LOCK();
    s_profile = updated;
    s_active = true;
    UNLOCK();
    return 0;
}

void walle_calibration_snapshot(walle_calibration_profile_t *profile)
{
    if (!profile) return;
    LOCK();
    *profile = s_profile;
    UNLOCK();
}

int walle_calibration_save_joint(size_t joint, uint16_t minimum, uint16_t rest,
                                 uint16_t maximum, bool inverted)
{
    walle_calibration_profile_t updated;
    LOCK();
    updated = s_profile;
    UNLOCK();
    if (!walle_calibration_set_joint_value(&updated, joint, minimum, rest,
                                           maximum, inverted)) return -1;
    const int error = persist(&updated);
    if (error) return error;
    LOCK();
    s_profile = updated;
    s_active = true;
    UNLOCK();
    return 0;
}

int walle_calibration_finish(void)
{
    walle_calibration_profile_t updated;
    LOCK();
    updated = s_profile;
    UNLOCK();
    if (!walle_calibration_finish_profile(&updated)) return -1;
    const int error = persist(&updated);
    if (error) return error;
    LOCK();
    s_profile = updated;
    s_active = false;
    UNLOCK();
    return 0;
}

int walle_calibration_reset(void)
{
    walle_calibration_profile_t reset;
    walle_calibration_defaults(&reset);
    const int error = persist(&reset);
    if (error) return error;
    LOCK();
    s_profile = reset;
    s_active = true;
    UNLOCK();
    return 0;
}

void walle_calibration_set_active(bool active)
{
    LOCK();
    s_active = active;
    UNLOCK();
}

int walle_calibration_restore(const walle_calibration_profile_t *profile) {
    if(!walle_calibration_profile_valid(profile)) return -1;
    walle_calibration_profile_t copy=*profile;
    copy.enabled=false;
    int error=persist(&copy);
    if(error) return error;
    LOCK(); s_profile=copy; s_active=true; UNLOCK();
    return 0;
}

bool walle_calibration_is_active(void)
{
    bool active;
    LOCK();
    active = s_active;
    UNLOCK();
    return active;
}
