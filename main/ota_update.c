#include "ota_update.h"

#include <stdio.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "freertos/FreeRTOS.h"
#include "ota_policy.h"

static const char *TAG = "ota";
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static esp_ota_handle_t s_handle;
static const esp_partition_t *s_target;
static bool s_active;
static size_t s_received;
static size_t s_total;

static void clear_update_state(void)
{
    taskENTER_CRITICAL(&s_lock);
    s_active = false;
    s_received = 0;
    s_total = 0;
    s_target = NULL;
    s_handle = 0;
    taskEXIT_CRITICAL(&s_lock);
}

esp_err_t walle_ota_begin(size_t image_size)
{
    taskENTER_CRITICAL(&s_lock);
    if (s_active) {
        taskEXIT_CRITICAL(&s_lock);
        return ESP_ERR_INVALID_STATE;
    }
    s_active = true;
    s_received = 0;
    s_total = image_size;
    taskEXIT_CRITICAL(&s_lock);

    s_target = esp_ota_get_next_update_partition(NULL);
    if (s_target == NULL) {
        clear_update_state();
        return ESP_ERR_NOT_FOUND;
    }
    const walle_ota_request_result_t validation =
        walle_ota_validate_request(image_size, s_target->size);
    if (validation != WALLE_OTA_REQUEST_OK) {
        clear_update_state();
        return validation == WALLE_OTA_REQUEST_EMPTY ? ESP_ERR_INVALID_SIZE : ESP_ERR_NO_MEM;
    }

    const esp_err_t error = esp_ota_begin(s_target, image_size, &s_handle);
    if (error != ESP_OK) clear_update_state();
    else ESP_LOGI(TAG, "Receiving %u bytes into %s", (unsigned)image_size, s_target->label);
    return error;
}

esp_err_t walle_ota_write(const void *data, size_t length)
{
    if (!s_active || s_handle == 0 || data == NULL || length == 0)
        return ESP_ERR_INVALID_STATE;
    if (s_received + length > s_total) return ESP_ERR_INVALID_SIZE;
    const esp_err_t error = esp_ota_write(s_handle, data, length);
    if (error == ESP_OK) {
        taskENTER_CRITICAL(&s_lock);
        s_received += length;
        taskEXIT_CRITICAL(&s_lock);
    }
    return error;
}

esp_err_t walle_ota_finish(void)
{
    if (!s_active || s_handle == 0 || s_target == NULL) return ESP_ERR_INVALID_STATE;
    if (s_received != s_total) {
        walle_ota_abort();
        return ESP_ERR_INVALID_SIZE;
    }
    esp_err_t error = esp_ota_end(s_handle);
    s_handle = 0;
    if(error==ESP_OK) {
        esp_app_desc_t description;
        error=esp_ota_get_partition_description(s_target,&description);
        if(error==ESP_OK && strcmp(description.project_name,"walle_reborn"))
            error=ESP_ERR_INVALID_ARG;
    }
    if (error == ESP_OK) error = esp_ota_set_boot_partition(s_target);
    if (error != ESP_OK) {
        clear_update_state();
        return error;
    }
    ESP_LOGI(TAG, "Image validated; next boot partition is %s", s_target->label);
    /* Keep the interlock active until the scheduled restart. */
    return ESP_OK;
}

void walle_ota_abort(void)
{
    if (s_handle != 0) esp_ota_abort(s_handle);
    clear_update_state();
}

bool walle_ota_is_active(void)
{
    taskENTER_CRITICAL(&s_lock);
    const bool active = s_active;
    taskEXIT_CRITICAL(&s_lock);
    return active;
}

void walle_ota_get_status(walle_ota_status_t *status)
{
    if (status == NULL) return;
    memset(status, 0, sizeof(*status));
    taskENTER_CRITICAL(&s_lock);
    status->active = s_active;
    status->received = s_received;
    status->total = s_total;
    const esp_partition_t *target = s_target;
    taskEXIT_CRITICAL(&s_lock);

    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running != NULL)
        snprintf(status->running_partition, sizeof(status->running_partition), "%s", running->label);
    if (target != NULL) {
        snprintf(status->target_partition, sizeof(status->target_partition), "%s", target->label);
        status->target_size = target->size;
    } else {
        target = esp_ota_get_next_update_partition(NULL);
        if (target != NULL) {
            snprintf(status->target_partition, sizeof(status->target_partition), "%s", target->label);
            status->target_size = target->size;
        }
    }
    const esp_app_desc_t *description = esp_app_get_description();
    if (description != NULL)
        snprintf(status->version, sizeof(status->version), "%s", description->version);
    esp_ota_img_states_t state;
    status->pending_validation = running != NULL &&
        esp_ota_get_state_partition(running, &state) == ESP_OK &&
        state == ESP_OTA_IMG_PENDING_VERIFY;
}

esp_err_t walle_ota_confirm_running_image(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (running == NULL || esp_ota_get_state_partition(running, &state) != ESP_OK ||
        state != ESP_OTA_IMG_PENDING_VERIFY) return ESP_OK;
    ESP_LOGI(TAG, "Startup checks passed; confirming OTA image %s", running->label);
    return esp_ota_mark_app_valid_cancel_rollback();
}
