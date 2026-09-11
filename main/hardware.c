#include "hardware.h"

#include <math.h>
#include <stdio.h>
#include "safety.h"
#include "wifi_manager.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#define PWM_TIMER LEDC_TIMER_0
#define PWM_MODE LEDC_LOW_SPEED_MODE
#define LEFT_CHANNEL LEDC_CHANNEL_0
#define RIGHT_CHANNEL LEDC_CHANNEL_1
#define LCD_HOST SPI2_HOST
#define LCD_DRAW_LINES 16
#define LCD_PIXEL_CLOCK_HZ (20 * 1000 * 1000)

static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_adc_cal;
static float s_left, s_right, s_voltage;
static bool s_stopped=true, s_bus, s_media;
static uint8_t s_panel_leds = UINT8_MAX;
static esp_lcd_panel_handle_t s_lcd;
static uint16_t *s_lcd_buffers[2];
static portMUX_TYPE s_display_lock = portMUX_INITIALIZER_UNLOCKED;
static uint8_t s_display_battery;
static bool s_display_drive_armed;
static walle_display_status_t s_display_status = WALLE_DISPLAY_BOOT;
static walle_display_mode_t s_display_mode = WALLE_DISPLAY_ENGINEERING;
static bool s_display_dirty = true;
static float clamp_unit(float value)
{
    if (value < -1.0f) return -1.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

static void set_pwm(ledc_channel_t channel, uint32_t duty)
{
    ledc_set_duty(PWM_MODE, channel, duty);
    ledc_update_duty(PWM_MODE, channel);
}

static void set_brushed(float value, int in1, int in2, ledc_channel_t channel)
{
    value = clamp_unit(value);
    gpio_set_level(in1, value > 0.001f);
    gpio_set_level(in2, value < -0.001f);
    set_pwm(channel, (uint32_t)lroundf(fabsf(value) * 1023.0f));
}

static uint16_t lcd_big_endian_rgb565(uint16_t color)
{
    return (uint16_t)((color << 8) | (color >> 8));
}

static void lcd_render(uint8_t percentage, bool drive_armed,
                       walle_display_status_t status, walle_display_mode_t mode)
{
    if (s_lcd == NULL || s_lcd_buffers[0] == NULL || s_lcd_buffers[1] == NULL) return;
    char lines_text[5][40];
    taskENTER_CRITICAL(&s_display_lock);
    float voltage=s_voltage; bool stopped=s_stopped,bus=s_bus,media=s_media;
    taskEXIT_CRITICAL(&s_display_lock);
    walle_wifi_status_t wifi; walle_wifi_get_status(&wifi);
    snprintf(lines_text[0],40,"BATTERY %.2f V",(double)voltage);
    snprintf(lines_text[1],40,"%s",stopped?"STOPPED - REARM":"ARMED");
    snprintf(lines_text[2],40,"BUS %s",bus?"VERIFIED":"NOT VERIFIED");
    snprintf(lines_text[3],40,"MEDIA %s",media?"ONLINE":"OFFLINE");
    snprintf(lines_text[4],40,"%s",wifi.ip);
    bool engineering=mode==WALLE_DISPLAY_ENGINEERING || status==WALLE_DISPLAY_ERROR ||
        status==WALLE_DISPLAY_CALIBRATION || status==WALLE_DISPLAY_OTA;
    unsigned transfer = 0;
    for (unsigned y = 0; y < WALLE_LCD_HEIGHT; y += LCD_DRAW_LINES) {
        uint16_t *buffer = s_lcd_buffers[transfer & 1U];
        const unsigned lines = y + LCD_DRAW_LINES <= WALLE_LCD_HEIGHT ?
            LCD_DRAW_LINES : WALLE_LCD_HEIGHT - y;
        for (unsigned row = 0; row < lines; ++row) {
            for (unsigned x = 0; x < WALLE_LCD_WIDTH; ++x) {
                uint16_t color = walle_display_pixel(
                    x, y + row, percentage, drive_armed, status, mode);
                if(engineering && y+row>=36 && y+row<228) {
                    color=0;
                    for(unsigned k=0;k<5;k++) if(walle_display_text_pixel(lines_text[k],x,y+row,12,44+k*34))
                        color=0xFFFF;
                }
                buffer[row * WALLE_LCD_WIDTH + x] =
                    lcd_big_endian_rgb565(color);
            }
        }
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(
            s_lcd, 0, y, WALLE_LCD_WIDTH, y + lines, buffer));
        ++transfer;
    }
}

static void lcd_task(void *argument)
{
    (void)argument;
    for (;;) {
        uint8_t percentage = 0;
        bool drive_armed = false;
        walle_display_status_t status = WALLE_DISPLAY_BOOT;
        walle_display_mode_t mode = WALLE_DISPLAY_ENGINEERING;
        bool dirty = false;
        taskENTER_CRITICAL(&s_display_lock);
        dirty = s_display_dirty;
        if (dirty) {
            percentage = s_display_battery;
            drive_armed = s_display_drive_armed;
            status = s_display_status;
            mode = s_display_mode;
            s_display_dirty = false;
        }
        taskEXIT_CRITICAL(&s_display_lock);
        if (dirty) lcd_render(percentage, drive_armed, status, mode);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

static void lcd_init(void)
{
    const gpio_config_t backlight = {
        .pin_bit_mask = 1ULL << CONFIG_WALLE_LCD_BACKLIGHT,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&backlight));
    gpio_set_level(CONFIG_WALLE_LCD_BACKLIGHT, 0);

    const spi_bus_config_t bus = {
        .sclk_io_num = CONFIG_WALLE_LCD_SPI_SCLK,
        .mosi_io_num = CONFIG_WALLE_LCD_SPI_MOSI,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = WALLE_LCD_WIDTH * LCD_DRAW_LINES * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_handle_t io = NULL;
    const esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = CONFIG_WALLE_LCD_DC,
        .cs_gpio_num = CONFIG_WALLE_LCD_SPI_CS,
        .pclk_hz = LCD_PIXEL_CLOCK_HZ,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 1,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(
        (esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io));

    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = CONFIG_WALLE_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io, &panel_config, &s_lcd));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_lcd));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_lcd));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(s_lcd, true));
    /* The mechanical carrier mounts the 240x280 panel in native portrait. */
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(s_lcd, false));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(s_lcd, false, false));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_lcd, true));

    for (unsigned i = 0; i < 2; ++i) {
        s_lcd_buffers[i] = spi_bus_dma_memory_alloc(
            LCD_HOST, WALLE_LCD_WIDTH * LCD_DRAW_LINES * sizeof(uint16_t), 0);
        ESP_ERROR_CHECK(s_lcd_buffers[i] == NULL ? ESP_ERR_NO_MEM : ESP_OK);
    }
    lcd_render(0, false, WALLE_DISPLAY_BOOT, WALLE_DISPLAY_ENGINEERING);
    gpio_set_level(CONFIG_WALLE_LCD_BACKLIGHT, 1);
    ESP_ERROR_CHECK(xTaskCreatePinnedToCore(
        lcd_task, "status_lcd", 4096, NULL, 3, NULL, 0) == pdPASS ?
            ESP_OK : ESP_ERR_NO_MEM);
}

int hardware_init(void)
{
    const gpio_config_t outputs = {
        .pin_bit_mask = (1ULL << CONFIG_WALLE_MOTOR_LEFT_IN1) |
                        (1ULL << CONFIG_WALLE_MOTOR_LEFT_IN2) |
                        (1ULL << CONFIG_WALLE_MOTOR_RIGHT_IN1) |
                        (1ULL << CONFIG_WALLE_MOTOR_RIGHT_IN2),
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&outputs));
    const ledc_timer_config_t drive_timer = {
        .speed_mode = PWM_MODE, .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = PWM_TIMER, .freq_hz = 20000, .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&drive_timer));
    const ledc_channel_config_t left = {
        .gpio_num = CONFIG_WALLE_MOTOR_LEFT_PWM, .speed_mode = PWM_MODE,
        .channel = LEFT_CHANNEL, .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = PWM_TIMER, .duty = 0,
    };
    const ledc_channel_config_t right = {
        .gpio_num = CONFIG_WALLE_MOTOR_RIGHT_PWM, .speed_mode = PWM_MODE,
        .channel = RIGHT_CHANNEL, .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = PWM_TIMER, .duty = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&left));
    ESP_ERROR_CHECK(ledc_channel_config(&right));

    const gpio_config_t buttons = {
        .pin_bit_mask = (1ULL << CONFIG_WALLE_BUTTON_ENGINEERING) |
                        (1ULL << CONFIG_WALLE_BUTTON_STOP),
        .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&buttons));
    const gpio_config_t panel_leds = {
        .pin_bit_mask = (1ULL << CONFIG_WALLE_LED_ENGINEERING) |
                        (1ULL << CONFIG_WALLE_LED_SOLAR),
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&panel_leds));
    gpio_set_level(CONFIG_WALLE_LED_ENGINEERING, 0);
    gpio_set_level(CONFIG_WALLE_LED_SOLAR, 0);
    s_panel_leds = 0;

    const adc_oneshot_unit_init_cfg_t adc_cfg = {.unit_id = ADC_UNIT_1};
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&adc_cfg, &s_adc));
    const adc_oneshot_chan_cfg_t channel_cfg = {
        .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(
        s_adc, (adc_channel_t)CONFIG_WALLE_BATTERY_ADC_CHANNEL, &channel_cfg));
    const adc_cali_curve_fitting_config_t calibration={.unit_id=ADC_UNIT_1,
        .chan=(adc_channel_t)CONFIG_WALLE_BATTERY_ADC_CHANNEL,.atten=ADC_ATTEN_DB_12,.bitwidth=ADC_BITWIDTH_12};
    ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&calibration,&s_adc_cal));

    lcd_init();
    return 0;
}

void hardware_set_tracks(float left, float right)
{
    taskENTER_CRITICAL(&s_display_lock);
    s_left=isfinite(left)?left:0; s_right=isfinite(right)?right:0;
    taskEXIT_CRITICAL(&s_display_lock);
}

void hardware_apply_tracks(void)
{
    taskENTER_CRITICAL(&s_display_lock);
    float left=s_left,right=s_right;
    taskEXIT_CRITICAL(&s_display_lock);
    if(!safety_allowed(false)) left=right=0;
    set_brushed(left, CONFIG_WALLE_MOTOR_LEFT_IN1, CONFIG_WALLE_MOTOR_LEFT_IN2,
                LEFT_CHANNEL);
    set_brushed(right, CONFIG_WALLE_MOTOR_RIGHT_IN1, CONFIG_WALLE_MOTOR_RIGHT_IN2,
                RIGHT_CHANNEL);
}

float hardware_read_battery_voltage(void)
{
    int raw = 0;
    if (adc_oneshot_read(s_adc, (adc_channel_t)CONFIG_WALLE_BATTERY_ADC_CHANNEL,
                         &raw) != ESP_OK) return NAN;
    int millivolts;
    if(adc_cali_raw_to_voltage(s_adc_cal,raw,&millivolts)!=ESP_OK) return NAN;
    const float adc_voltage = millivolts/1000.0f;
    return adc_voltage / ((float)CONFIG_WALLE_BATTERY_DIVIDER_RATIO_MILLI / 1000.0f);
}

uint8_t hardware_read_buttons(void)
{
    uint8_t value = 0;
    if (!gpio_get_level(CONFIG_WALLE_BUTTON_STOP)) value |= WALLE_BUTTON_STOP;
    if (!gpio_get_level(CONFIG_WALLE_BUTTON_ENGINEERING))
        value |= WALLE_BUTTON_ENGINEERING;
    return value;
}

void hardware_set_panel_leds(uint8_t led_mask)
{
    if (led_mask == s_panel_leds) return;
    gpio_set_level(CONFIG_WALLE_LED_SOLAR, (led_mask & WALLE_LED_SOLAR) != 0U);
    gpio_set_level(CONFIG_WALLE_LED_ENGINEERING,
                   (led_mask & WALLE_LED_ENGINEERING) != 0U);
    s_panel_leds = led_mask;
}

void hardware_display_status(uint8_t percentage, bool drive_armed,
                             walle_display_status_t status,
                             walle_display_mode_t mode)
{
    taskENTER_CRITICAL(&s_display_lock);
    s_display_battery = percentage;
    s_display_drive_armed = drive_armed;
    s_display_status = status;
    s_display_mode = mode;
    s_display_dirty = true;
    taskEXIT_CRITICAL(&s_display_lock);
}

void hardware_display_diagnostics(float voltage,bool stopped,bool bus,bool media) {
    taskENTER_CRITICAL(&s_display_lock);
    s_voltage=voltage; s_stopped=stopped; s_bus=bus; s_media=media; s_display_dirty=true;
    taskEXIT_CRITICAL(&s_display_lock);
}
