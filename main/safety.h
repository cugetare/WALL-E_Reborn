#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    bool stopped, service, physical_stop, ota, bus_verified, calibrated, battery_ok;
    uint32_t epoch, heartbeat_ms;
} walle_safety_t;
void safety_init(void);
walle_safety_t safety_snapshot(void);
void safety_stop(void);
void safety_service(bool service);
void safety_inputs(bool physical, bool ota, bool battery_ok, uint32_t now);
void safety_verified(bool bus, bool calibrated);
bool safety_arm(uint32_t now);
void safety_heartbeat(uint32_t now);
bool safety_allowed(bool calibration);
bool safety_policy_allowed(const walle_safety_t *s, bool calibration);
