#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define WALLE_AUTOMATION_MAX 16
#define WALLE_AUTOMATION_NAME_MAX 32
#define WALLE_PROFILE_ID_MAX 32

typedef struct {
    uint32_t buttons;
    uint16_t left_trigger;
    uint16_t right_trigger;
    int16_t left_x, left_y, right_x, right_y;
} walle_controller_state_t;

typedef struct {
    uint32_t id;
    char name[WALLE_AUTOMATION_NAME_MAX];
    char profile_id[WALLE_PROFILE_ID_MAX];
    uint32_t buttons;
    uint16_t left_trigger_min;
    uint16_t right_trigger_min;
    uint16_t hold_ms;
    uint16_t cooldown_ms;
    bool require_neutral_sticks;
    bool enabled;
} walle_automation_t;

typedef struct {
    walle_automation_t rules[WALLE_AUTOMATION_MAX];
    uint32_t active_since[WALLE_AUTOMATION_MAX];
    uint32_t last_fired[WALLE_AUTOMATION_MAX];
    uint16_t count;
    uint16_t armed_mask;
} walle_automation_engine_t;

void walle_automation_engine_init(walle_automation_engine_t *engine);
bool walle_automation_rule_valid(const walle_automation_t *rule);
bool walle_automation_set(walle_automation_engine_t *engine,
                          const walle_automation_t *rule);
bool walle_automation_remove(walle_automation_engine_t *engine, uint32_t id);
const walle_automation_t *walle_automation_process(
    walle_automation_engine_t *engine, const walle_controller_state_t *state,
    uint32_t now_ms, bool motion_ready);
int walle_automation_load(walle_automation_engine_t *engine);
int walle_automation_save(const walle_automation_engine_t *engine);

