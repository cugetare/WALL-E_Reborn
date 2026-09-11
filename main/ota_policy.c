#include "ota_policy.h"

walle_ota_request_result_t walle_ota_validate_request(size_t image_size,
                                                       size_t partition_size)
{
    if (image_size == 0) return WALLE_OTA_REQUEST_EMPTY;
    if (image_size > partition_size) return WALLE_OTA_REQUEST_TOO_LARGE;
    return WALLE_OTA_REQUEST_OK;
}

uint8_t walle_ota_progress_percent(size_t received, size_t total)
{
    if (total == 0) return 0;
    if (received >= total) return 100;
    return (uint8_t)((received * 100U) / total);
}

bool walle_ota_motion_allowed(bool update_active)
{
    return !update_active;
}
