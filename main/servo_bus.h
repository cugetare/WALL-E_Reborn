#pragma once

#include <stddef.h>
#include <stdint.h>

#include "walle_core.h"

#define WALLE_SERVO_PACKET_MAX 80

typedef struct {
    uint8_t id;
    uint8_t error;
    uint8_t parameters[16];
    size_t parameter_count;
} walle_servo_status_t;

size_t servo_build_sync_position_packet(
    const walle_servo_calibration_t calibration[WALLE_JOINT_COUNT],
    const uint16_t positions[WALLE_JOINT_COUNT], uint16_t move_time_ms,
    uint8_t *packet, size_t capacity);
size_t servo_build_ping_packet(uint8_t id, uint8_t *packet, size_t capacity);
size_t servo_build_read_packet(uint8_t id, uint8_t address, uint8_t length,
                               uint8_t *packet, size_t capacity);
size_t servo_build_write_byte_packet(uint8_t id, uint8_t address, uint8_t value,
                                     uint8_t *packet, size_t capacity);
bool servo_parse_status_packet(const uint8_t *packet, size_t length,
                               walle_servo_status_t *status);

int servo_bus_init(void);
int servo_bus_ping(uint8_t id);
int servo_bus_scan(uint8_t first_id, uint8_t last_id, uint8_t *ids,
                   size_t capacity, size_t *count);
int servo_bus_change_id(uint8_t old_id, uint8_t new_id);
int servo_bus_read_telemetry(uint8_t id, uint16_t *position,
                             uint16_t *load);
int servo_bus_write_single_position(uint8_t id, uint16_t position,
                                    uint16_t move_time_ms, uint16_t speed);
int servo_bus_write_positions(
    const walle_servo_calibration_t calibration[WALLE_JOINT_COUNT],
    const uint16_t positions[WALLE_JOINT_COUNT], uint16_t move_time_ms);
