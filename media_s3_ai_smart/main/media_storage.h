#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

bool media_path_valid(const char *path);
int media_storage_init(void);
bool media_storage_ready(void);
int media_storage_list(const char *path, char **json, size_t *length);
int media_storage_read(const char *path, uint8_t **data, size_t *length, uint32_t *crc);
int media_storage_begin_write(const char *path, size_t size);
int media_storage_write_chunk(const uint8_t *data, size_t size);
int media_storage_commit(uint32_t expected_crc);
void media_storage_abort(void);
int media_storage_delete(const char *path);
