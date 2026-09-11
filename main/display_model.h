#pragma once

#include <stdbool.h>
#include <stdint.h>

#define WALLE_LCD_WIDTH 240
#define WALLE_LCD_HEIGHT 280

typedef enum {
    WALLE_DISPLAY_BOOT = 0,
    WALLE_DISPLAY_CALIBRATION,
    WALLE_DISPLAY_READY,
    WALLE_DISPLAY_OTA,
    WALLE_DISPLAY_ERROR,
} walle_display_status_t;

typedef enum {
    WALLE_DISPLAY_PLAY = 0,
    WALLE_DISPLAY_ENGINEERING,
} walle_display_mode_t;

bool walle_display_battery_warning(float voltage, uint8_t battery_percent);
bool walle_display_text_pixel(const char *text,unsigned x,unsigned y,unsigned left,unsigned top);

uint16_t walle_display_pixel(unsigned x, unsigned y, uint8_t battery_percent,
                             bool drive_armed, walle_display_status_t status,
                             walle_display_mode_t mode);
