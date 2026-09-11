#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
bool media_link_online(void);
int media_link_asset_play(const char *asset);
void media_link_request_stop(void);
int media_link_profile_read(const char *id,char **data,size_t *length);
bool media_link_path_valid(const char *path);
int media_link_file_list(const char *path,char **json,size_t *length);
int media_link_file_read(const char *path,char **data,size_t *length);
int media_link_file_begin(const char *path, size_t size);
int media_link_file_write(const void *data, size_t size);
int media_link_file_finish(uint32_t crc32);
void media_link_file_abort(void);
int media_link_file_delete(const char *path);

int media_link_init(void);
int media_link_play(uint16_t track);
int media_link_stop(void);
int media_link_profile_begin(const char *profile_id, size_t size);
int media_link_profile_write(const void *data, size_t size);
int media_link_profile_finish(uint32_t crc32);
void media_link_profile_abort(void);
int media_link_profile_play(const char *profile_id);
int media_link_profile_delete(const char *profile_id);
