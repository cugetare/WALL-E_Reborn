#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    WALLE_OTA_REQUEST_OK = 0,
    WALLE_OTA_REQUEST_EMPTY,
    WALLE_OTA_REQUEST_TOO_LARGE,
} walle_ota_request_result_t;

walle_ota_request_result_t walle_ota_validate_request(size_t image_size,
                                                       size_t partition_size);
uint8_t walle_ota_progress_percent(size_t received, size_t total);
bool walle_ota_motion_allowed(bool update_active);
