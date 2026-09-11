#pragma once

#include <stdint.h>

typedef enum {
    WALLE_STATUS_LED_OFF = 0,
    WALLE_STATUS_LED_AP,
    WALLE_STATUS_LED_WIFI,
} walle_status_led_state_t;

typedef struct {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
} walle_status_led_rgb_t;

walle_status_led_rgb_t walle_status_led_color(walle_status_led_state_t state);
int walle_status_led_init(void);
int walle_status_led_set(walle_status_led_state_t state);
