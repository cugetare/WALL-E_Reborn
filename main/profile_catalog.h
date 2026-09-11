#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define WALLE_PROFILE_MAX 24
#define WALLE_PROFILE_ID_LENGTH 32
#define WALLE_PROFILE_NAME_LENGTH 48

typedef struct {
    char id[WALLE_PROFILE_ID_LENGTH];
    char name[WALLE_PROFILE_NAME_LENGTH];
    uint32_t size;
    uint32_t crc32;
} walle_profile_info_t;

typedef struct {
    walle_profile_info_t entries[WALLE_PROFILE_MAX];
    uint16_t count;
} walle_profile_catalog_t;

void walle_profile_catalog_init(walle_profile_catalog_t *catalog);
bool walle_profile_id_valid(const char *id);
bool walle_profile_catalog_set(walle_profile_catalog_t *catalog,
                               const walle_profile_info_t *profile);
bool walle_profile_catalog_remove(walle_profile_catalog_t *catalog, const char *id);
bool walle_profile_catalog_contains(const walle_profile_catalog_t *catalog, const char *id);
int walle_profile_catalog_load(walle_profile_catalog_t *catalog);
int walle_profile_catalog_save(const walle_profile_catalog_t *catalog);
uint32_t walle_crc32_update(uint32_t crc, const void *data, size_t size);

