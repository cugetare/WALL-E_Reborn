#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    WALLE_WIFI_AP = 0,
    WALLE_WIFI_CONNECTING,
    WALLE_WIFI_STATION,
} walle_wifi_mode_t;

typedef struct {
    walle_wifi_mode_t mode;
    bool credentials_saved;
    char ssid[33];
    char ip[16];
} walle_wifi_status_t;

bool walle_wifi_credentials_valid(const char *ssid, const char *password);
int walle_wifi_start(void);
typedef struct { char ssid[33]; int rssi; bool open; } walle_wifi_network_t;
int walle_wifi_scan(walle_wifi_network_t *networks, size_t capacity, size_t *count);
void walle_wifi_get_status(walle_wifi_status_t *status);
int walle_wifi_save_credentials(const char *ssid, const char *password);
int walle_wifi_forget_credentials(void);
