#include "automation.h"

#include <stdlib.h>
#include <string.h>

static bool text_valid(const char *text, size_t capacity)
{
    const size_t length = strnlen(text, capacity);
    if (length == 0 || length >= capacity) return false;
    for (size_t i = 0; i < length; ++i) {
        const char c = text[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '_' || c == ' ')) return false;
    }
    return true;
}

void walle_automation_engine_init(walle_automation_engine_t *engine)
{
    memset(engine, 0, sizeof(*engine));
    engine->armed_mask = 0xFFFFU;
}

bool walle_automation_rule_valid(const walle_automation_t *rule)
{
    return rule && rule->id != 0 && text_valid(rule->name, sizeof(rule->name)) &&
           text_valid(rule->profile_id, sizeof(rule->profile_id)) &&
           (rule->buttons != 0 || rule->left_trigger_min != 0 ||
            rule->right_trigger_min != 0) &&
           rule->left_trigger_min <= 1000 && rule->right_trigger_min <= 1000 &&
           rule->hold_ms <= 5000 && rule->cooldown_ms <= 60000;
}

bool walle_automation_set(walle_automation_engine_t *engine,
                          const walle_automation_t *rule)
{
    if (!engine || !walle_automation_rule_valid(rule)) return false;
    size_t slot = engine->count;
    for (size_t i = 0; i < engine->count; ++i)
        if (engine->rules[i].id == rule->id) { slot = i; break; }
    if (slot == engine->count) {
        if (engine->count >= WALLE_AUTOMATION_MAX) return false;
        ++engine->count;
    }
    engine->rules[slot] = *rule;
    engine->active_since[slot] = 0;
    engine->last_fired[slot] = 0;
    engine->armed_mask |= (uint16_t)(1U << slot);
    return true;
}

bool walle_automation_remove(walle_automation_engine_t *engine, uint32_t id)
{
    if (!engine) return false;
    for (size_t i = 0; i < engine->count; ++i) {
        if (engine->rules[i].id != id) continue;
        const size_t tail = engine->count - i - 1;
        if (tail) {
            memmove(&engine->rules[i], &engine->rules[i + 1], tail * sizeof(engine->rules[0]));
            memmove(&engine->active_since[i], &engine->active_since[i + 1], tail * sizeof(uint32_t));
            memmove(&engine->last_fired[i], &engine->last_fired[i + 1], tail * sizeof(uint32_t));
        }
        --engine->count;
        engine->armed_mask = 0xFFFFU;
        return true;
    }
    return false;
}

static bool neutral(const walle_controller_state_t *s)
{
    const int deadzone = 5000;
    return abs(s->left_x) < deadzone && abs(s->left_y) < deadzone &&
           abs(s->right_x) < deadzone && abs(s->right_y) < deadzone;
}

const walle_automation_t *walle_automation_process(
    walle_automation_engine_t *engine, const walle_controller_state_t *state,
    uint32_t now_ms, bool motion_ready)
{
    if (!engine || !state) return NULL;
    if(!motion_ready) {
        memset(engine->active_since,0,sizeof(engine->active_since));
        /* Require a release after STOP before a held chord can trigger again. */
        engine->armed_mask=0;
        return NULL;
    }
    for (size_t i = 0; i < engine->count; ++i) {
        const walle_automation_t *r = &engine->rules[i];
        const bool matched = r->enabled &&
            (state->buttons & r->buttons) == r->buttons &&
            state->left_trigger >= r->left_trigger_min &&
            state->right_trigger >= r->right_trigger_min &&
            (!r->require_neutral_sticks || neutral(state));
        if (!matched) {
            engine->active_since[i] = 0;
            engine->armed_mask |= (uint16_t)(1U << i);
            continue;
        }
        if (engine->active_since[i] == 0) engine->active_since[i] = now_ms ? now_ms : 1;
        const bool held = (uint32_t)(now_ms - engine->active_since[i]) >= r->hold_ms;
        const bool cooled = engine->last_fired[i] == 0 ||
            (uint32_t)(now_ms - engine->last_fired[i]) >= r->cooldown_ms;
        if (held && cooled && (engine->armed_mask & (1U << i))) {
            engine->armed_mask &= (uint16_t)~(1U << i);
            engine->last_fired[i] = now_ms ? now_ms : 1;
            return r;
        }
    }
    return NULL;
}

#if defined(ESP_PLATFORM) && !defined(WALLE_PROTOCOL_ONLY)
#include "nvs.h"
#include "nvs_flash.h"
int walle_automation_load(walle_automation_engine_t *engine)
{
    walle_automation_engine_init(engine);
    nvs_handle_t nvs;
    if (nvs_open("automations", NVS_READONLY, &nvs) != ESP_OK) return 0;
    size_t size = sizeof(engine->rules);
    esp_err_t error = nvs_get_blob(nvs, "rules", engine->rules, &size);
    nvs_close(nvs);
    if (error == ESP_ERR_NVS_NOT_FOUND) return 0;
    if (error != ESP_OK || size % sizeof(engine->rules[0]) != 0) return -1;
    engine->count = (uint16_t)(size / sizeof(engine->rules[0]));
    if (engine->count > WALLE_AUTOMATION_MAX) return -1;
    for (size_t i = 0; i < engine->count; ++i)
        if (!walle_automation_rule_valid(&engine->rules[i])) return -1;
    return 0;
}
int walle_automation_save(const walle_automation_engine_t *engine)
{
    nvs_handle_t nvs;
    if (nvs_open("automations", NVS_READWRITE, &nvs) != ESP_OK) return -1;
    const size_t size = engine->count * sizeof(engine->rules[0]);
    esp_err_t error = size ? nvs_set_blob(nvs, "rules", engine->rules, size) :
                             nvs_erase_key(nvs, "rules");
    if (error == ESP_ERR_NVS_NOT_FOUND) error = ESP_OK;
    if (error == ESP_OK) error = nvs_commit(nvs);
    nvs_close(nvs);
    return error == ESP_OK ? 0 : -1;
}
#else
int walle_automation_load(walle_automation_engine_t *engine) { walle_automation_engine_init(engine); return 0; }
int walle_automation_save(const walle_automation_engine_t *engine) { (void)engine; return 0; }
#endif
