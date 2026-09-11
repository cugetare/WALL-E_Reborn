#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "display_model.h"
#include "panel_io_model.h"

int hardware_init(void);
void hardware_set_tracks(float left, float right);
void hardware_apply_tracks(void);
void hardware_display_diagnostics(float voltage, bool stopped, bool bus, bool media);
float hardware_read_battery_voltage(void);
uint8_t hardware_read_buttons(void);
void hardware_set_panel_leds(uint8_t led_mask);
void hardware_display_status(uint8_t percentage, bool drive_armed,
                             walle_display_status_t status,
                             walle_display_mode_t mode);
