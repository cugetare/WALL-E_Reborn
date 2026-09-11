#include "status_led.h"

walle_status_led_rgb_t walle_status_led_color(walle_status_led_state_t state)
{
    /* Deliberately dim: the onboard WS2812 is a status indicator, not lighting. */
    switch (state) {
    case WALLE_STATUS_LED_AP:
        return (walle_status_led_rgb_t){.blue = 16};
    case WALLE_STATUS_LED_WIFI:
        return (walle_status_led_rgb_t){.green = 16};
    default:
        return (walle_status_led_rgb_t){0};
    }
}

#if defined(ESP_PLATFORM) && !defined(WALLE_PROTOCOL_ONLY)
#include "driver/rmt_encoder.h"
#include "driver/rmt_tx.h"
#include "esp_rom_sys.h"
#include "sdkconfig.h"

#define STATUS_LED_RMT_RESOLUTION_HZ 10000000

static rmt_channel_handle_t s_channel;
static rmt_encoder_handle_t s_encoder;

int walle_status_led_init(void)
{
    if (s_channel) return 0;

    const rmt_tx_channel_config_t channel_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = CONFIG_WALLE_STATUS_LED_GPIO,
        .resolution_hz = STATUS_LED_RMT_RESOLUTION_HZ,
        .mem_block_symbols = 48,
        .trans_queue_depth = 1,
    };
    esp_err_t error = rmt_new_tx_channel(&channel_config, &s_channel);
    if (error != ESP_OK) return -1;

    const rmt_bytes_encoder_config_t encoder_config = {
        .bit0 = {
            .level0 = 1, .duration0 = 3,
            .level1 = 0, .duration1 = 9,
        },
        .bit1 = {
            .level0 = 1, .duration0 = 9,
            .level1 = 0, .duration1 = 3,
        },
        .flags.msb_first = true,
    };
    error = rmt_new_bytes_encoder(&encoder_config, &s_encoder);
    if (error != ESP_OK || rmt_enable(s_channel) != ESP_OK) {
        if (s_encoder) rmt_del_encoder(s_encoder);
        rmt_del_channel(s_channel);
        s_encoder = NULL;
        s_channel = NULL;
        return -1;
    }
    return walle_status_led_set(WALLE_STATUS_LED_OFF);
}

int walle_status_led_set(walle_status_led_state_t state)
{
    if (!s_channel || !s_encoder) return -1;
    const walle_status_led_rgb_t rgb = walle_status_led_color(state);
    /*
     * Waveshare's ESP32-S3-DEV-KIT-N32R16V onboard addressable LED presents as
     * RGB in practice.  The previous GRB order made the Wi-Fi state request for
     * green show up as red on the bench, while blue still looked correct.
     */
    const uint8_t bytes[3] = {rgb.red, rgb.green, rgb.blue};
    const rmt_transmit_config_t transmit_config = {
        .loop_count = 0,
        .flags.eot_level = 0,
    };
    if (rmt_transmit(s_channel, s_encoder, bytes, sizeof(bytes), &transmit_config) != ESP_OK ||
        rmt_tx_wait_all_done(s_channel, 100) != ESP_OK) return -1;
    esp_rom_delay_us(80);
    return 0;
}
#else
int walle_status_led_init(void) { return 0; }
int walle_status_led_set(walle_status_led_state_t state) { (void)state; return 0; }
#endif
