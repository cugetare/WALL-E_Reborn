#include "wifi_manager.h"
#include "status_led.h"

#include <string.h>
#include <stdlib.h>

bool walle_wifi_credentials_valid(const char *ssid, const char *password)
{
    if (!ssid || !password) return false;
    const size_t ssid_length = strnlen(ssid, 33);
    const size_t password_length = strnlen(password, 64);
    return ssid_length > 0 && ssid_length <= 32 &&
           (password_length == 0 || (password_length >= 8 && password_length <= 63));
}

#if defined(ESP_PLATFORM) && !defined(WALLE_PROTOCOL_ONLY)
#include <stdio.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs.h"
#include "sdkconfig.h"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAILED_BIT BIT1
#define WIFI_MAX_RETRIES 5

static const char *TAG = "wifi";
static EventGroupHandle_t s_events;
static walle_wifi_status_t s_status;
static unsigned s_retries;
static bool s_startup_waiting;
static bool s_switching;
static bool s_fallback_scheduled;
static bool s_scanning;

static bool load_credentials(char ssid[33], char password[64])
{
    nvs_handle_t nvs;
    if (nvs_open("wifi_cfg", NVS_READONLY, &nvs) != ESP_OK) return false;
    size_t ssid_size = 33, password_size = 64;
    const bool ok = nvs_get_str(nvs, "ssid", ssid, &ssid_size) == ESP_OK &&
                    nvs_get_str(nvs, "password", password, &password_size) == ESP_OK &&
                    walle_wifi_credentials_valid(ssid, password);
    nvs_close(nvs);
    return ok;
}

static int start_ap(void)
{
    s_switching = true;
    esp_wifi_stop();
    wifi_config_t config = {0};
    snprintf((char *)config.ap.ssid, sizeof(config.ap.ssid), "%s", CONFIG_WALLE_AP_SSID);
    snprintf((char *)config.ap.password, sizeof(config.ap.password), "%s", CONFIG_WALLE_AP_PASSWORD);
    config.ap.ssid_len = strlen(CONFIG_WALLE_AP_SSID);
    config.ap.channel = 6;
    config.ap.max_connection = 4;
    config.ap.authmode = strlen(CONFIG_WALLE_AP_PASSWORD) >= 8 ?
        WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    esp_err_t error = esp_wifi_set_mode(WIFI_MODE_AP);
    if (error == ESP_OK) error = esp_wifi_set_config(WIFI_IF_AP, &config);
    if (error == ESP_OK) error = esp_wifi_start();
    s_switching = false;
    if (error != ESP_OK) return error;
    s_status.mode = WALLE_WIFI_AP;
    snprintf(s_status.ssid, sizeof(s_status.ssid), "%s", CONFIG_WALLE_AP_SSID);
    snprintf(s_status.ip, sizeof(s_status.ip), "192.168.4.1");
    walle_status_led_set(WALLE_STATUS_LED_AP);
    ESP_LOGW(TAG, "Provisioning AP: %s, http://192.168.4.1", CONFIG_WALLE_AP_SSID);
    return 0;
}

static void fallback_task(void *argument)
{
    (void)argument;
    start_ap();
    s_fallback_scheduled = false;
    vTaskDelete(NULL);
}

static void event_handler(void *argument, esp_event_base_t base, int32_t id, void *data)
{
    (void)argument;
    if (s_switching) return;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        if (s_status.mode == WALLE_WIFI_CONNECTING) esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_status.mode = WALLE_WIFI_CONNECTING;
        s_status.ip[0] = 0;
        walle_status_led_set(WALLE_STATUS_LED_OFF);
        if (s_retries++ < WIFI_MAX_RETRIES) {
            esp_wifi_connect();
        } else {
            xEventGroupSetBits(s_events, WIFI_FAILED_BIT);
            if (!s_startup_waiting && !s_fallback_scheduled) {
                s_fallback_scheduled = true;
                if (xTaskCreate(fallback_task, "wifi_fallback", 3072, NULL, 8, NULL) != pdPASS) {
                    s_fallback_scheduled = false;
                    ESP_LOGE(TAG, "Could not schedule provisioning AP fallback");
                }
            }
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = data;
        snprintf(s_status.ip, sizeof(s_status.ip), IPSTR, IP2STR(&event->ip_info.ip));
        s_status.mode = WALLE_WIFI_STATION;
        s_retries = 0;
        walle_status_led_set(WALLE_STATUS_LED_WIFI);
        xEventGroupSetBits(s_events, WIFI_CONNECTED_BIT);
        ESP_LOGI(TAG, "Connected to %s; web UI: http://%s", s_status.ssid, s_status.ip);
    }
}

int walle_wifi_start(void)
{
    if (walle_status_led_init() != 0) {
        ESP_LOGW(TAG, "Onboard status LED unavailable");
    }
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    s_events = xEventGroupCreate();
    if (!s_events) return -1;
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, event_handler, NULL));

    char ssid[33] = {0}, password[64] = {0};
    s_status.credentials_saved = load_credentials(ssid, password);
    if (!s_status.credentials_saved) return start_ap();

    wifi_config_t station = {0};
    memcpy(station.sta.ssid, ssid, strlen(ssid));
    memcpy(station.sta.password, password, strlen(password));
    station.sta.threshold.authmode = strlen(password) ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    station.sta.pmf_cfg.capable = true;
    snprintf(s_status.ssid, sizeof(s_status.ssid), "%s", ssid);
    s_status.mode = WALLE_WIFI_CONNECTING;
    s_retries = 0;
    s_startup_waiting = true;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &station));
    ESP_ERROR_CHECK(esp_wifi_start());
    const EventBits_t result = xEventGroupWaitBits(s_events,
        WIFI_CONNECTED_BIT | WIFI_FAILED_BIT, pdTRUE, pdFALSE, pdMS_TO_TICKS(15000));
    s_startup_waiting = false;
    if (result & WIFI_CONNECTED_BIT) return 0;
    ESP_LOGW(TAG, "Saved network unavailable; falling back to provisioning AP");
    return start_ap();
}

void walle_wifi_get_status(walle_wifi_status_t *status)
{
    if (status) *status = s_status;
}

int walle_wifi_scan(walle_wifi_network_t *networks, size_t capacity, size_t *count)
{
    if (!networks || !count || !capacity) return -1;
    *count = 0;
    wifi_ap_record_t *records=calloc(50,sizeof(*records));
    if(!records) return -1;
    wifi_mode_t previous;
    if (esp_wifi_get_mode(&previous) != ESP_OK) { free(records); return -1; }
    s_scanning = true;
    esp_err_t error = ESP_OK;
    if (previous == WIFI_MODE_AP) error = esp_wifi_set_mode(WIFI_MODE_APSTA);
    wifi_scan_config_t config = {0};
    if (error == ESP_OK) error = esp_wifi_scan_start(&config, true);
    uint16_t found = 50;
    if (error == ESP_OK) error = esp_wifi_scan_get_ap_records(&found, records);
    if (error == ESP_OK) {
        for (uint16_t i = 0; i < found && *count < capacity; ++i) {
            if (!records[i].ssid[0]) continue;
            bool duplicate = false;
            for (size_t j = 0; j < *count; ++j)
                if (!strcmp(networks[j].ssid, (char *)records[i].ssid)) duplicate = true;
            if (duplicate) continue;
            walle_wifi_network_t *n = &networks[(*count)++];
            snprintf(n->ssid, sizeof(n->ssid), "%s", (char *)records[i].ssid);
            n->rssi = records[i].rssi;
            n->open = records[i].authmode == WIFI_AUTH_OPEN;
        }
    }
    if (previous == WIFI_MODE_AP) {
        esp_err_t restored = esp_wifi_set_mode(previous);
        if (error == ESP_OK) error = restored;
    }
    s_scanning = false;
    free(records);
    ESP_LOGI(TAG, "Network scan completed: %u networks, result %s", (unsigned)*count, esp_err_to_name(error));
    return error == ESP_OK ? 0 : -1;
}

int walle_wifi_save_credentials(const char *ssid, const char *password)
{
    if (!walle_wifi_credentials_valid(ssid, password)) return -1;
    nvs_handle_t nvs;
    if (nvs_open("wifi_cfg", NVS_READWRITE, &nvs) != ESP_OK) return -1;
    esp_err_t error = nvs_set_str(nvs, "ssid", ssid);
    if (error == ESP_OK) error = nvs_set_str(nvs, "password", password);
    if (error == ESP_OK) error = nvs_commit(nvs);
    nvs_close(nvs);
    return error == ESP_OK ? 0 : -1;
}

int walle_wifi_forget_credentials(void)
{
    nvs_handle_t nvs;
    if (nvs_open("wifi_cfg", NVS_READWRITE, &nvs) != ESP_OK) return -1;
    esp_err_t error = nvs_erase_all(nvs);
    if (error == ESP_OK) error = nvs_commit(nvs);
    nvs_close(nvs);
    return error == ESP_OK ? 0 : -1;
}
#else
int walle_wifi_scan(walle_wifi_network_t *networks, size_t capacity, size_t *count) { (void)networks; (void)capacity; if (count) *count = 0; return -1; }
int walle_wifi_start(void) { return 0; }
void walle_wifi_get_status(walle_wifi_status_t *status) { if (status) memset(status, 0, sizeof(*status)); }
int walle_wifi_save_credentials(const char *ssid, const char *password) { return walle_wifi_credentials_valid(ssid, password) ? 0 : -1; }
int walle_wifi_forget_credentials(void) { return 0; }
#endif
