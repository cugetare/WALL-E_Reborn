#include "safety.h"
#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
#define ENTER() taskENTER_CRITICAL(&lock)
#define LEAVE() taskEXIT_CRITICAL(&lock)
#else
#define ENTER() ((void)0)
#define LEAVE() ((void)0)
#endif
static walle_safety_t state = {.stopped=true, .service=true, .epoch=1};
void safety_init(void) { ENTER(); state=(walle_safety_t){.stopped=true,.service=true,.epoch=1}; LEAVE(); }
walle_safety_t safety_snapshot(void) { ENTER(); walle_safety_t s=state; LEAVE(); return s; }
static void stop_locked(void) { if (!state.stopped) ++state.epoch; state.stopped=true; }
void safety_stop(void) { ENTER(); stop_locked(); LEAVE(); }
void safety_service(bool service) { ENTER(); stop_locked(); ++state.epoch; state.service=service; LEAVE(); }
void safety_verified(bool bus, bool calibrated) { ENTER(); state.bus_verified=bus; state.calibrated=calibrated; if(!bus) stop_locked(); LEAVE(); }
void safety_inputs(bool physical, bool ota, bool battery_ok, uint32_t now) {
    ENTER(); state.physical_stop=physical; state.ota=ota; state.battery_ok=battery_ok;
    if(physical || ota || (!state.service && !battery_ok) ||
       (!state.stopped && (uint32_t)(now-state.heartbeat_ms)>300)) stop_locked();
    LEAVE();
}
bool safety_arm(uint32_t now) {
    ENTER();
    bool ok=!state.physical_stop && !state.ota &&
        (state.service || (state.bus_verified && state.calibrated && state.battery_ok));
    if(ok) { state.stopped=false; state.heartbeat_ms=now; ++state.epoch; }
    LEAVE(); return ok;
}
void safety_heartbeat(uint32_t now) { ENTER(); state.heartbeat_ms=now; LEAVE(); }
bool safety_policy_allowed(const walle_safety_t *s, bool calibration) {
    return s && !s->stopped && !s->physical_stop && !s->ota &&
        (calibration ? s->service : (!s->service && s->bus_verified && s->calibrated && s->battery_ok));
}
bool safety_allowed(bool calibration) { walle_safety_t s=safety_snapshot(); return safety_policy_allowed(&s,calibration); }
