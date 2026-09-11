#include "media_uart.h"
#include <stdlib.h>
#include <string.h>
#include "driver/uart.h"
#include "esp_log.h"
#include "media_crc32.h"
#include "media_storage.h"
#include "sdkconfig.h"

static const char *TAG = "media_uart";
static const uart_port_t PORT = UART_NUM_1;

static int read_line(char *out, size_t capacity)
{
    size_t n = 0;
    for (;;) {
        char c;
        if (uart_read_bytes(PORT, &c, 1, portMAX_DELAY) != 1) continue;
        if (c == '\r') continue;
        if (c == '\n') {
            out[n] = 0;
            return 0;
        }
        if (n + 1 >= capacity) return -1;
        out[n++] = c;
    }
}

static void ack(unsigned seq, const char *status)
{
    char line[96];
    int n = snprintf(line, sizeof(line), "ACK %u %s\n", seq, status);
    uart_write_bytes(PORT, line, n);
}

static void send_blob(unsigned seq, const char *status, const uint8_t *data, size_t length, uint32_t crc)
{
    ack(seq, status);
    char header[48];
    int n = snprintf(header, sizeof(header), "DATA %u %08lX\n", (unsigned)length, (unsigned long)crc);
    uart_write_bytes(PORT, header, n);
    if (length) uart_write_bytes(PORT, data, length);
}

static int read_exact(uint8_t *data, size_t length)
{
    size_t have = 0;
    while (have < length) {
        int n = uart_read_bytes(PORT, data + have, length - have, pdMS_TO_TICKS(1500));
        if (n <= 0) return -1;
        have += (size_t)n;
    }
    return 0;
}

static void handle(unsigned seq, char *verb, char *args)
{
    if (!strcmp(verb, "HELLO")) {
        ack(seq, media_storage_ready() ? "OK" : "ERR SD");
    } else if (!strcmp(verb, "STOP")) {
        media_storage_abort();
        ack(seq, "OK");
    } else if (!strcmp(verb, "PLAY") || !strcmp(verb, "ASSET")) {
        ESP_LOGI(TAG, "audio request %s %s", verb, args);
        ack(seq, "OK");
    } else if (!strcmp(verb, "FS_LIST")) {
        char *json = NULL;
        size_t length = 0;
        if (media_storage_list(args, &json, &length)) ack(seq, "ERR LIST");
        else { send_blob(seq, "OK", (const uint8_t *)json, length, media_crc32_update(0, json, length)); free(json); }
    } else if (!strcmp(verb, "FS_GET") || !strcmp(verb, "GET")) {
        uint8_t *data = NULL;
        size_t length = 0;
        uint32_t crc = 0;
        const char *path = !strcmp(verb, "GET") ? "" : args;
        char profile_path[128];
        if (!strcmp(verb, "GET")) {
            snprintf(profile_path, sizeof(profile_path), "profiles/%s.walle", args);
            path = profile_path;
        }
        if (media_storage_read(path, &data, &length, &crc)) ack(seq, "ERR GET");
        else { send_blob(seq, "OK", data, length, crc); free(data); }
    } else if (!strcmp(verb, "FS_PUT") || !strcmp(verb, "PUT")) {
        char path[96];
        unsigned size = 0;
        if (!strcmp(verb, "PUT")) {
            char id[32];
            if (sscanf(args, "%31s %u", id, &size) == 2) snprintf(path, sizeof(path), "profiles/%s.walle", id);
            else path[0] = 0;
        } else if (sscanf(args, "%95s %u", path, &size) != 2) path[0] = 0;
        ack(seq, media_storage_begin_write(path, size) ? "ERR PUT" : "OK");
    } else if (!strcmp(verb, "FS_CHUNK") || !strcmp(verb, "CHUNK")) {
        unsigned size = 0;
        if (sscanf(args, "%u", &size) != 1 || size > 1024) { ack(seq, "ERR CHUNK"); return; }
        uint8_t *buffer = malloc(size);
        if (!buffer) { ack(seq, "ERR MEM"); return; }
        int result = read_exact(buffer, size) || media_storage_write_chunk(buffer, size);
        free(buffer);
        ack(seq, result ? "ERR CHUNK" : "OK");
    } else if (!strcmp(verb, "FS_COMMIT") || !strcmp(verb, "COMMIT")) {
        unsigned long crc = 0;
        if (sscanf(args, "%lx", &crc) != 1 || media_storage_commit((uint32_t)crc)) ack(seq, "ERR COMMIT");
        else ack(seq, "OK");
    } else if (!strcmp(verb, "FS_ABORT") || !strcmp(verb, "ABORT")) {
        media_storage_abort();
        ack(seq, "OK");
    } else if (!strcmp(verb, "FS_DELETE") || !strcmp(verb, "DELETE")) {
        char path[128];
        if (!strcmp(verb, "DELETE")) snprintf(path, sizeof(path), "profiles/%s.walle", args);
        else snprintf(path, sizeof(path), "%s", args);
        ack(seq, media_storage_delete(path) ? "ERR DELETE" : "OK");
    } else if (!strcmp(verb, "PROFILE_PLAY")) {
        ack(seq, "OK");
    } else {
        ack(seq, "ERR VERB");
    }
}

void media_uart_run(void)
{
    const uart_config_t cfg = {
        .baud_rate = 921600,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(PORT, 4096, 4096, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(PORT, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(PORT, CONFIG_MEDIA_UART_TX, CONFIG_MEDIA_UART_RX, -1, -1));
    char line[192];
    for (;;) {
        if (read_line(line, sizeof(line))) {
            uart_flush_input(PORT);
            continue;
        }
        unsigned seq = 0;
        char verb[32] = {0};
        char args[128] = {0};
        if (sscanf(line, "REQ %u %31s %127[^\n]", &seq, verb, args) >= 2) handle(seq, verb, args);
    }
}
