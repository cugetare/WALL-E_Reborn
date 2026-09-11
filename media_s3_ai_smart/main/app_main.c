#include "esp_log.h"
#include "media_storage.h"
#include "media_uart.h"

void app_main(void)
{
    ESP_LOGI("walle_media", "WALL-E S3 AI Smart media firmware boot");
    media_storage_init();
    media_uart_run();
}
