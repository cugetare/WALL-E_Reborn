#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

typedef struct {
    bool active;
    bool pending_validation;
    size_t received;
    size_t total;
    size_t target_size;
    char running_partition[17];
    char target_partition[17];
    char version[33];
} walle_ota_status_t;

esp_err_t walle_ota_begin(size_t image_size);
esp_err_t walle_ota_write(const void *data, size_t length);
esp_err_t walle_ota_finish(void);
void walle_ota_abort(void);
bool walle_ota_is_active(void);
void walle_ota_get_status(walle_ota_status_t *status);
esp_err_t walle_ota_confirm_running_image(void);
