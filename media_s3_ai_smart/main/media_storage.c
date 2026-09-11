#include "media_storage.h"
#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "esp_log.h"
#include "media_crc32.h"
#include "sdkconfig.h"

#if CONFIG_MEDIA_SD_ENABLE
#include "driver/sdspi_host.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#endif

#define MOUNT_POINT "/sdcard"

static const char *TAG = "media_storage";
static bool s_ready;
static FILE *s_upload;
static char s_final_path[160];
static size_t s_remaining;
static uint32_t s_crc;

bool media_path_valid(const char *path)
{
    if (!path || !*path || strlen(path) > 95 || path[0] == '/' || strstr(path, "..") ||
        strchr(path, '\\') || strchr(path, ' ') || strchr(path, '\n') || strchr(path, '\r') ||
        strchr(path, ':')) return false;
    const char *slash = strchr(path, '/');
    size_t root_len = slash ? (size_t)(slash - path) : strlen(path);
    if (!root_len || (slash && !slash[1])) return false;
    const char *roots[] = {"sounds", "profiles", "recordings", "firmware", "tmp"};
    bool root_ok = false;
    for (size_t i = 0; i < sizeof(roots) / sizeof(roots[0]); i++)
        if (strlen(roots[i]) == root_len && !strncmp(path, roots[i], root_len)) root_ok = true;
    if (!root_ok) return false;
    for (const char *p = path; *p; p++)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
              (*p >= '0' && *p <= '9') || *p == '/' || *p == '_' || *p == '-' || *p == '.')) return false;
    return true;
}

static void absolute_path(char *out, size_t capacity, const char *path)
{
    snprintf(out, capacity, MOUNT_POINT "/%s", path);
}

int media_storage_init(void)
{
#if CONFIG_MEDIA_SD_ENABLE
    const char *roots[] = {"sounds", "profiles", "recordings", "firmware", "tmp"};
    sdmmc_card_t *card = NULL;
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = CONFIG_MEDIA_SD_MOSI,
        .miso_io_num = CONFIG_MEDIA_SD_MISO,
        .sclk_io_num = CONFIG_MEDIA_SD_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096,
    };
    esp_err_t err = spi_bus_initialize(host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPI init failed: %s", esp_err_to_name(err));
        return -1;
    }
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = CONFIG_MEDIA_SD_CS;
    slot_config.host_id = host.slot;
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 6,
        .allocation_unit_size = 16 * 1024,
    };
    err = esp_vfs_fat_sdspi_mount(MOUNT_POINT, &host, &slot_config, &mount_config, &card);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SD mount failed: %s", esp_err_to_name(err));
        return -1;
    }
    s_ready = true;
    for (size_t i = 0; i < sizeof(roots) / sizeof(roots[0]); i++) {
        char path[80];
        absolute_path(path, sizeof(path), roots[i]);
        mkdir(path, 0775);
    }
    return 0;
#else
    ESP_LOGW(TAG, "SD disabled in Kconfig; HELLO will report media not ready");
    return -1;
#endif
}

bool media_storage_ready(void) { return s_ready; }

int media_storage_list(const char *path, char **json, size_t *length)
{
    if (!s_ready || !media_path_valid(path) || !json || !length) return -1;
    char full[160];
    absolute_path(full, sizeof(full), path);
    DIR *dir = opendir(full);
    if (!dir) return -1;
    size_t cap = 8192, used = 0;
    char *out = malloc(cap);
    if (!out) { closedir(dir); return -1; }
    used += snprintf(out + used, cap - used, "{\"path\":\"%s\",\"entries\":[", path);
    bool first = true;
    struct dirent *entry;
    while ((entry = readdir(dir)) && used + 180 < cap) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        char child[192];
        snprintf(child, sizeof(child), "%s/%s", full, entry->d_name);
        struct stat st;
        if (stat(child, &st)) continue;
        used += snprintf(out + used, cap - used, "%s{\"name\":\"%s\",\"path\":\"%s/%s\",\"type\":\"%s\",\"size\":%lu}",
                         first ? "" : ",", entry->d_name, path, entry->d_name,
                         S_ISDIR(st.st_mode) ? "dir" : "file", (unsigned long)st.st_size);
        first = false;
    }
    closedir(dir);
    used += snprintf(out + used, cap - used, "]}");
    *json = out;
    *length = used;
    return 0;
}

int media_storage_read(const char *path, uint8_t **data, size_t *length, uint32_t *crc)
{
    if (!s_ready || !media_path_valid(path) || !data || !length || !crc) return -1;
    char full[160];
    absolute_path(full, sizeof(full), path);
    FILE *f = fopen(full, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);
    if (size < 0 || size > 262144) { fclose(f); return -1; }
    uint8_t *buffer = malloc((size_t)size);
    if (!buffer) { fclose(f); return -1; }
    size_t got = fread(buffer, 1, (size_t)size, f);
    fclose(f);
    if (got != (size_t)size) { free(buffer); return -1; }
    *data = buffer;
    *length = got;
    *crc = media_crc32_update(0, buffer, got);
    return 0;
}

int media_storage_begin_write(const char *path, size_t size)
{
    if (!s_ready || !media_path_valid(path) || !size || size > 4194304 || s_upload) return -1;
    absolute_path(s_final_path, sizeof(s_final_path), path);
    char tmp[160];
    absolute_path(tmp, sizeof(tmp), "tmp/upload.tmp");
    s_upload = fopen(tmp, "wb");
    if (!s_upload) return -1;
    s_remaining = size;
    s_crc = 0;
    return 0;
}

int media_storage_write_chunk(const uint8_t *data, size_t size)
{
    if (!s_upload || !data || !size || size > s_remaining) return -1;
    if (fwrite(data, 1, size, s_upload) != size) return -1;
    s_crc = media_crc32_update(s_crc, data, size);
    s_remaining -= size;
    return 0;
}

int media_storage_commit(uint32_t expected_crc)
{
    if (!s_upload || s_remaining || s_crc != expected_crc) return -1;
    fclose(s_upload);
    s_upload = NULL;
    char tmp[160];
    absolute_path(tmp, sizeof(tmp), "tmp/upload.tmp");
    remove(s_final_path);
    return rename(tmp, s_final_path);
}

void media_storage_abort(void)
{
    if (s_upload) {
        fclose(s_upload);
        s_upload = NULL;
    }
    char tmp[160];
    absolute_path(tmp, sizeof(tmp), "tmp/upload.tmp");
    remove(tmp);
}

int media_storage_delete(const char *path)
{
    if (!s_ready || !media_path_valid(path)) return -1;
    char full[160];
    absolute_path(full, sizeof(full), path);
    return remove(full);
}
