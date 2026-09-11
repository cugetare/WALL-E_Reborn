#pragma once
#include <stddef.h>
#include <stdint.h>

uint32_t media_crc32_update(uint32_t crc, const void *data, size_t length);
