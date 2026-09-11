#pragma once

#include <stdint.h>

#include "display_model.h"

enum {
    WALLE_BUTTON_REC = 1U << 0,
    WALLE_BUTTON_PLAY = 1U << 1,
    WALLE_BUTTON_STOP = 1U << 2,
    WALLE_BUTTON_ENGINEERING = 1U << 3,
};

enum {
    WALLE_LED_REC = 1U << 0,
    WALLE_LED_PLAY = 1U << 1,
    WALLE_LED_STOP = 1U << 2,
    WALLE_LED_SOLAR = 1U << 3,
    WALLE_LED_ENGINEERING = 1U << 4,
};

uint8_t walle_panel_led_mask(uint8_t buttons,
                             walle_display_mode_t requested_mode,
                             walle_display_status_t status);
