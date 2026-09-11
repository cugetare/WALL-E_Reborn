#include "profile_catalog.h"

#include <string.h>

void walle_profile_catalog_init(walle_profile_catalog_t *catalog)
{
    memset(catalog, 0, sizeof(*catalog));
}

bool walle_profile_id_valid(const char *id)
{
    const size_t length = id ? strnlen(id, WALLE_PROFILE_ID_LENGTH) : 0;
    if (!length || length >= WALLE_PROFILE_ID_LENGTH) return false;
    for (size_t i = 0; i < length; ++i)
        if (!((id[i] >= 'a' && id[i] <= 'z') || (id[i] >= 'A' && id[i] <= 'Z') ||
              (id[i] >= '0' && id[i] <= '9') || id[i] == '-' || id[i] == '_')) return false;
    return true;
}

bool walle_profile_catalog_set(walle_profile_catalog_t *catalog,
                               const walle_profile_info_t *profile)
{
    if (!catalog || !profile || !walle_profile_id_valid(profile->id) ||
        profile->size == 0 || strnlen(profile->name, sizeof(profile->name)) >= sizeof(profile->name))
        return false;
    size_t slot = catalog->count;
    for (size_t i = 0; i < catalog->count; ++i)
        if (strcmp(catalog->entries[i].id, profile->id) == 0) { slot = i; break; }
    if (slot == catalog->count) {
        if (catalog->count >= WALLE_PROFILE_MAX) return false;
        ++catalog->count;
    }
    catalog->entries[slot] = *profile;
    return true;
}

bool walle_profile_catalog_remove(walle_profile_catalog_t *catalog, const char *id)
{
    if (!catalog || !id) return false;
    for (size_t i = 0; i < catalog->count; ++i) {
        if (strcmp(catalog->entries[i].id, id) != 0) continue;
        if (i + 1 < catalog->count)
            memmove(&catalog->entries[i], &catalog->entries[i + 1],
                    (catalog->count - i - 1) * sizeof(catalog->entries[0]));
        --catalog->count;
        return true;
    }
    return false;
}

bool walle_profile_catalog_contains(const walle_profile_catalog_t *catalog, const char *id)
{
    if (!catalog || !id) return false;
    for (size_t i = 0; i < catalog->count; ++i)
        if (strcmp(catalog->entries[i].id, id) == 0) return true;
    return false;
}

uint32_t walle_crc32_update(uint32_t crc, const void *data, size_t size)
{
    const uint8_t *bytes = data;
    crc = ~crc;
    for (size_t i = 0; i < size; ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xEDB88320U & (uint32_t)-(int32_t)(crc & 1U));
    }
    return ~crc;
}

#if defined(ESP_PLATFORM) && !defined(WALLE_PROTOCOL_ONLY)
#include "nvs.h"
int walle_profile_catalog_load(walle_profile_catalog_t *catalog)
{
    walle_profile_catalog_init(catalog);
    nvs_handle_t nvs;
    if (nvs_open("profiles", NVS_READONLY, &nvs) != ESP_OK) return 0;
    size_t size = sizeof(catalog->entries);
    esp_err_t error = nvs_get_blob(nvs, "catalog", catalog->entries, &size);
    nvs_close(nvs);
    if (error == ESP_ERR_NVS_NOT_FOUND) return 0;
    if (error != ESP_OK || size % sizeof(catalog->entries[0])) return -1;
    catalog->count = (uint16_t)(size / sizeof(catalog->entries[0]));
    return catalog->count <= WALLE_PROFILE_MAX ? 0 : -1;
}
int walle_profile_catalog_save(const walle_profile_catalog_t *catalog)
{
    nvs_handle_t nvs;
    if (nvs_open("profiles", NVS_READWRITE, &nvs) != ESP_OK) return -1;
    const size_t size = catalog->count * sizeof(catalog->entries[0]);
    esp_err_t error = size ? nvs_set_blob(nvs, "catalog", catalog->entries, size) :
                             nvs_erase_key(nvs, "catalog");
    if (error == ESP_ERR_NVS_NOT_FOUND) error = ESP_OK;
    if (error == ESP_OK) error = nvs_commit(nvs);
    nvs_close(nvs);
    return error == ESP_OK ? 0 : -1;
}
#else
int walle_profile_catalog_load(walle_profile_catalog_t *catalog) { walle_profile_catalog_init(catalog); return 0; }
int walle_profile_catalog_save(const walle_profile_catalog_t *catalog) { (void)catalog; return 0; }
#endif
