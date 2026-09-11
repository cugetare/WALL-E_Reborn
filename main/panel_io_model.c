#include "panel_io_model.h"

uint8_t walle_panel_led_mask(uint8_t buttons,
                             walle_display_mode_t requested_mode,
                             walle_display_status_t status)
{
    uint8_t leds = WALLE_LED_SOLAR; /* Confirms that switched power is alive. */
    if ((buttons & WALLE_BUTTON_REC) != 0U) leds |= WALLE_LED_REC;
    if ((buttons & WALLE_BUTTON_STOP) != 0U || status == WALLE_DISPLAY_ERROR)
        leds |= WALLE_LED_STOP;

    const bool engineering_visible = requested_mode == WALLE_DISPLAY_ENGINEERING ||
        status == WALLE_DISPLAY_CALIBRATION || status == WALLE_DISPLAY_OTA ||
        status == WALLE_DISPLAY_ERROR;
    if (engineering_visible) {
        leds |= WALLE_LED_ENGINEERING;
    } else if (status == WALLE_DISPLAY_READY) {
        leds |= WALLE_LED_PLAY;
    }
    return leds;
}
