#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    WALLE_CMD_INVALID = 0,
    WALLE_CMD_DRIVE,
    WALLE_CMD_JOINT,
    WALLE_CMD_SOUND,
    WALLE_CMD_STOP,
    WALLE_CMD_CALIB_MOVE,
    WALLE_CMD_CALIB_SET,
    WALLE_CMD_CALIB_FINISH,
    WALLE_CMD_CALIB_RESET,
} walle_command_kind_t;

typedef struct {
    walle_command_kind_t kind;
    float a;
    float b;
    uint16_t index;
    uint16_t minimum;
    uint16_t rest;
    uint16_t maximum;
    bool inverted;
    uint32_t epoch;
    uint32_t duration_ms;
} walle_command_t;

bool walle_command_parse(const char *text, size_t length, walle_command_t *command);
