#include "command_protocol.h"

#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "walle_core.h"

static bool parse_float(const char **cursor, float *value)
{
    char *end = NULL;
    errno = 0;
    *value = strtof(*cursor, &end);
    if (errno || end == *cursor || !isfinite(*value)) return false;
    *cursor = end;
    return true;
}

static bool parse_unsigned(const char **cursor, unsigned long *value, char delimiter)
{
    char *end = NULL;
    errno = 0;
    *value = strtoul(*cursor, &end, 10);
    if (errno || end == *cursor || *end != delimiter) return false;
    *cursor = delimiter == '\0' ? end : end + 1;
    return true;
}

bool walle_command_parse(const char *text, size_t length, walle_command_t *command)
{
    if (!text || !command || length == 0 || length >= 96) return false;
    /* Some older callers include the trailing terminator, never accept hidden suffixes. */
    const char *terminator=memchr(text,0,length);
    if (terminator && terminator!=text+length-1) return false;
    char buffer[96];
    memcpy(buffer, text, length);
    buffer[length] = '\0';
    memset(command, 0, sizeof(*command));

    if (strcmp(buffer, "stop") == 0) {
        command->kind = WALLE_CMD_STOP;
        return true;
    }
    if (strcmp(buffer, "calib,finish") == 0) {
        command->kind = WALLE_CMD_CALIB_FINISH;
        return true;
    }
    if (strcmp(buffer, "calib,reset") == 0) {
        command->kind = WALLE_CMD_CALIB_RESET;
        return true;
    }
    if (strncmp(buffer, "calib,move,", 11) == 0) {
        const char *p = buffer + 11;
        unsigned long joint, raw;
        if (!parse_unsigned(&p, &joint, ',') ||
            !parse_unsigned(&p, &raw, '\0') || joint >= WALLE_JOINT_COUNT ||
            raw > 1023) return false;
        command->kind = WALLE_CMD_CALIB_MOVE;
        command->index = (uint16_t)joint;
        command->rest = (uint16_t)raw;
        return true;
    }
    if (strncmp(buffer, "calib,set,", 10) == 0) {
        const char *p = buffer + 10;
        unsigned long joint, minimum, rest, maximum, inverted;
        if (!parse_unsigned(&p, &joint, ',') ||
            !parse_unsigned(&p, &minimum, ',') ||
            !parse_unsigned(&p, &rest, ',') ||
            !parse_unsigned(&p, &maximum, ',') ||
            !parse_unsigned(&p, &inverted, '\0') || joint >= WALLE_JOINT_COUNT ||
            minimum > 1023 || rest > 1023 || maximum > 1023 || inverted > 1)
            return false;
        command->kind = WALLE_CMD_CALIB_SET;
        command->index = (uint16_t)joint;
        command->minimum = (uint16_t)minimum;
        command->rest = (uint16_t)rest;
        command->maximum = (uint16_t)maximum;
        command->inverted = inverted != 0;
        return true;
    }
    if (strncmp(buffer, "drive,", 6) == 0) {
        const char *p = buffer + 6;
        if (!parse_float(&p, &command->a) || *p++ != ',' ||
            !parse_float(&p, &command->b) || *p != '\0') return false;
        if (command->a < -1.0f || command->a > 1.0f ||
            command->b < -1.0f || command->b > 1.0f) return false;
        command->kind = WALLE_CMD_DRIVE;
        return true;
    }
    if (strncmp(buffer, "joint,", 6) == 0) {
        char *end = NULL;
        unsigned long index = strtoul(buffer + 6, &end, 10);
        if (end == buffer + 6 || *end++ != ',' || index >= WALLE_JOINT_COUNT) return false;
        const char *p = end;
        if (!parse_float(&p, &command->a) || *p != '\0' ||
            command->a < 0.0f || command->a > 1.0f) return false;
        command->index = (uint16_t)index;
        command->kind = WALLE_CMD_JOINT;
        return true;
    }
    if (strncmp(buffer, "sound,", 6) == 0) {
        char *end = NULL;
        unsigned long track = strtoul(buffer + 6, &end, 10);
        if (end == buffer + 6 || *end != '\0' || track == 0 || track > 3000) return false;
        command->index = (uint16_t)track;
        command->kind = WALLE_CMD_SOUND;
        return true;
    }
    return false;
}
