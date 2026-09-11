#include "servo_bus.h"

#include <string.h>

#if defined(ESP_PLATFORM) && !defined(WALLE_PROTOCOL_ONLY)
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_err.h"
#include "freertos/semphr.h"
#include "sdkconfig.h"
#endif

#define SERVO_BROADCAST_ID 0xFE
#define SERVO_INSTRUCTION_SYNC_WRITE 0x83
#define SERVO_INSTRUCTION_PING 0x01
#define SERVO_INSTRUCTION_READ 0x02
#define SERVO_INSTRUCTION_WRITE 0x03
#define SERVO_GOAL_POSITION_L 42
#define SERVO_GOAL_BLOCK_SIZE 6
#define SERVO_ID_REGISTER 5
#define SERVO_LOCK_REGISTER 48
#define SERVO_PRESENT_POSITION_L 56

static uint8_t checksum(const uint8_t *packet, size_t from, size_t to_exclusive)
{
    unsigned sum = 0;
    for (size_t i = from; i < to_exclusive; ++i) sum += packet[i];
    return (uint8_t)~sum;
}

static size_t build_instruction(uint8_t id, uint8_t instruction,
                                const uint8_t *parameters, size_t count,
                                uint8_t *packet, size_t capacity)
{
    const size_t total = count + 6U;
    if (!packet || capacity < total || count > 251U) return 0;
    packet[0] = 0xFF; packet[1] = 0xFF; packet[2] = id;
    packet[3] = (uint8_t)(count + 2U); packet[4] = instruction;
    if (count) memcpy(&packet[5], parameters, count);
    packet[5 + count] = checksum(packet, 2, 5 + count);
    return total;
}

size_t servo_build_ping_packet(uint8_t id, uint8_t *packet, size_t capacity)
{
    return build_instruction(id, SERVO_INSTRUCTION_PING, NULL, 0, packet, capacity);
}

size_t servo_build_read_packet(uint8_t id, uint8_t address, uint8_t length,
                               uint8_t *packet, size_t capacity)
{
    const uint8_t parameters[] = {address, length};
    return build_instruction(id, SERVO_INSTRUCTION_READ, parameters, 2, packet, capacity);
}

size_t servo_build_write_byte_packet(uint8_t id, uint8_t address, uint8_t value,
                                     uint8_t *packet, size_t capacity)
{
    const uint8_t parameters[] = {address, value};
    return build_instruction(id, SERVO_INSTRUCTION_WRITE, parameters, 2, packet, capacity);
}

bool servo_parse_status_packet(const uint8_t *packet, size_t length,
                               walle_servo_status_t *status)
{
    if (!packet || !status || length < 6 || packet[0] != 0xFF || packet[1] != 0xFF)
        return false;
    const size_t expected = (size_t)packet[3] + 4U;
    if (expected != length || packet[3] < 2U) return false;
    const size_t parameter_count = packet[3] - 2U;
    if (parameter_count > sizeof(status->parameters) ||
        checksum(packet, 2, length - 1) != packet[length - 1]) return false;
    status->id = packet[2]; status->error = packet[4];
    status->parameter_count = parameter_count;
    if (parameter_count) memcpy(status->parameters, &packet[5], parameter_count);
    return true;
}

size_t servo_build_sync_position_packet(
    const walle_servo_calibration_t calibration[WALLE_JOINT_COUNT],
    const uint16_t positions[WALLE_JOINT_COUNT], uint16_t move_time_ms,
    uint8_t *packet, size_t capacity)
{
    size_t servo_count = 0;
    for (size_t i = 0; i < WALLE_JOINT_COUNT; ++i) {
        if (calibration[i].id != 0) ++servo_count;
    }
    const size_t payload = 2U + servo_count * (1U + SERVO_GOAL_BLOCK_SIZE);
    const size_t total = payload + 6U;
    if (!packet || capacity < total) return 0;

    packet[0] = 0xFF;
    packet[1] = 0xFF;
    packet[2] = SERVO_BROADCAST_ID;
    packet[3] = (uint8_t)(payload + 2U);
    packet[4] = SERVO_INSTRUCTION_SYNC_WRITE;
    packet[5] = SERVO_GOAL_POSITION_L;
    packet[6] = SERVO_GOAL_BLOCK_SIZE;
    size_t out = 7;
    for (size_t i = 0; i < WALLE_JOINT_COUNT; ++i) {
        if (calibration[i].id == 0) continue; /* Unassigned during commissioning. */
        const uint16_t pos = positions[i];
        const uint16_t speed = calibration[i].max_speed;
        packet[out++] = calibration[i].id;
        packet[out++] = (uint8_t)(pos >> 8);
        packet[out++] = (uint8_t)(pos & 0xFF);
        packet[out++] = (uint8_t)(move_time_ms >> 8);
        packet[out++] = (uint8_t)(move_time_ms & 0xFF);
        packet[out++] = (uint8_t)(speed >> 8);
        packet[out++] = (uint8_t)(speed & 0xFF);
    }
    const uint8_t packet_checksum = checksum(packet, 2, out);
    packet[out++] = packet_checksum;
    return out;
}

#if defined(ESP_PLATFORM) && !defined(WALLE_PROTOCOL_ONLY)
static const uart_port_t s_uart = UART_NUM_1;
static SemaphoreHandle_t s_bus_lock;

static int transact(const uint8_t *request, size_t request_length,
                    walle_servo_status_t *status)
{
    if (!s_bus_lock || xSemaphoreTake(s_bus_lock, pdMS_TO_TICKS(100)) != pdTRUE)
        return ESP_ERR_TIMEOUT;
    uint8_t response[96];
    uart_flush_input(s_uart);
    const int written = uart_write_bytes(s_uart, request, request_length);
    esp_err_t error = uart_wait_tx_done(s_uart, pdMS_TO_TICKS(20));
    if (written != (int)request_length || error != ESP_OK) {
        xSemaphoreGive(s_bus_lock);
        return error == ESP_OK ? ESP_FAIL : error;
    }
    const int received = uart_read_bytes(s_uart, response, sizeof(response),
                                         pdMS_TO_TICKS(12));
    xSemaphoreGive(s_bus_lock);
    if (received <= 0) return ESP_ERR_TIMEOUT;
    /* Adapter A can echo the transmitted frame. Skip echo/noise, parse complete replies. */
    for(size_t offset=0;offset+6U<=(size_t)received;offset++) {
        if(response[offset]!=0xFF || response[offset+1]!=0xFF) continue;
        size_t length=(size_t)response[offset+3]+4;
        if(offset+length>(size_t)received) continue;
        if(servo_parse_status_packet(response+offset,length,status) && status->error==0 &&
           (status->id==request[2] ||
            (request_length==8 && request[4]==SERVO_INSTRUCTION_WRITE && request[5]==SERVO_ID_REGISTER && status->id==request[6])))
            return ESP_OK;
    }
    return ESP_ERR_INVALID_RESPONSE;
}

static int write_byte(uint8_t id, uint8_t address, uint8_t value)
{
    uint8_t packet[8]; walle_servo_status_t status;
    const size_t length = servo_build_write_byte_packet(id, address, value,
                                                         packet, sizeof(packet));
    int result = transact(packet, length, &status);
    return result == ESP_OK && status.id != id && !(address==SERVO_ID_REGISTER && status.id==value) ? ESP_ERR_INVALID_RESPONSE : result;
}

int servo_bus_init(void)
{
    const uart_config_t cfg = {
        .baud_rate = 1000000,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(s_uart, 512, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(s_uart, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(s_uart, CONFIG_WALLE_SERVO_UART_TX,
                                 CONFIG_WALLE_SERVO_UART_RX,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    /* Waveshare adapter A owns half-duplex direction; host uses ordinary UART. */
    s_bus_lock = xSemaphoreCreateMutex();
    if (!s_bus_lock) return ESP_ERR_NO_MEM;
    return 0;
}

int servo_bus_ping(uint8_t id)
{
    uint8_t packet[6]; walle_servo_status_t status;
    const size_t length = servo_build_ping_packet(id, packet, sizeof(packet));
    const int error = transact(packet, length, &status);
    return error == ESP_OK && status.id == id ? ESP_OK : (error ? error : ESP_FAIL);
}

int servo_bus_scan(uint8_t first_id, uint8_t last_id, uint8_t *ids,
                   size_t capacity, size_t *count)
{
    if (!count || first_id > last_id) return ESP_ERR_INVALID_ARG;
    *count = 0;
    for (unsigned id = first_id; id <= last_id; ++id) {
        if (servo_bus_ping((uint8_t)id) == ESP_OK) {
            if (*count >= capacity || !ids) return ESP_ERR_NO_MEM;
            ids[(*count)++] = (uint8_t)id;
        }
    }
    return ESP_OK;
}

int servo_bus_change_id(uint8_t old_id, uint8_t new_id)
{
    if (old_id == 0 || old_id >= 0xFE || new_id < 2 || new_id >= 0xFE)
        return ESP_ERR_INVALID_ARG;
    if (old_id == new_id) return servo_bus_ping(old_id);
    if (servo_bus_ping(new_id) == ESP_OK) return ESP_ERR_INVALID_STATE;
    int error = write_byte(old_id, SERVO_LOCK_REGISTER, 0);
    if (!error) error = write_byte(old_id, SERVO_ID_REGISTER, new_id);
    if (!error) error = write_byte(new_id, SERVO_LOCK_REGISTER, 1);
    if (!error) error = servo_bus_ping(new_id);
    return error;
}

int servo_bus_read_telemetry(uint8_t id, uint16_t *position, uint16_t *load)
{
    if (!position || !load) return ESP_ERR_INVALID_ARG;
    uint8_t packet[8]; walle_servo_status_t status;
    const size_t length = servo_build_read_packet(id, SERVO_PRESENT_POSITION_L, 6,
                                                   packet, sizeof(packet));
    int error = transact(packet, length, &status);
    if (!error && status.id == id && status.parameter_count == 6) {
        *position = (uint16_t)((status.parameters[0] << 8) | status.parameters[1]);
        *load = (uint16_t)((status.parameters[4] << 8) | status.parameters[5]);
    } else if (!error) error = ESP_ERR_INVALID_SIZE;
    return error;
}

int servo_bus_write_single_position(uint8_t id, uint16_t position,
                                    uint16_t move_time_ms, uint16_t speed)
{
    walle_servo_calibration_t calibration[WALLE_JOINT_COUNT] = {0};
    uint16_t positions[WALLE_JOINT_COUNT] = {0};
    calibration[0].id = id; calibration[0].max_speed = speed; positions[0] = position;
    return servo_bus_write_positions(calibration, positions, move_time_ms);
}

int servo_bus_write_positions(
    const walle_servo_calibration_t calibration[WALLE_JOINT_COUNT],
    const uint16_t positions[WALLE_JOINT_COUNT], uint16_t move_time_ms)
{
    uint8_t packet[WALLE_SERVO_PACKET_MAX];
    const size_t length = servo_build_sync_position_packet(
        calibration, positions, move_time_ms, packet, sizeof(packet));
    if (!length) return ESP_ERR_INVALID_SIZE;
    if (!s_bus_lock || xSemaphoreTake(s_bus_lock, pdMS_TO_TICKS(100)) != pdTRUE)
        return ESP_ERR_TIMEOUT;
    const int written = uart_write_bytes(s_uart, packet, length);
    const esp_err_t waited = uart_wait_tx_done(s_uart, pdMS_TO_TICKS(20));
    xSemaphoreGive(s_bus_lock);
    if (waited != ESP_OK) return waited;
    return written == (int)length ? ESP_OK : ESP_FAIL;
}
#else
int servo_bus_init(void) { return 0; }
int servo_bus_ping(uint8_t id) { (void)id; return -1; }
int servo_bus_scan(uint8_t first_id, uint8_t last_id, uint8_t *ids,
                   size_t capacity, size_t *count)
{ (void)first_id; (void)last_id; (void)ids; (void)capacity; if (count) *count = 0; return 0; }
int servo_bus_change_id(uint8_t old_id, uint8_t new_id)
{ (void)old_id; (void)new_id; return 0; }
int servo_bus_read_telemetry(uint8_t id, uint16_t *position, uint16_t *load)
{ (void)id; if (position) *position = 0; if (load) *load = 0; return 0; }
int servo_bus_write_single_position(uint8_t id, uint16_t position,
                                    uint16_t move_time_ms, uint16_t speed)
{ (void)id; (void)position; (void)move_time_ms; (void)speed; return 0; }
int servo_bus_write_positions(
    const walle_servo_calibration_t calibration[WALLE_JOINT_COUNT],
    const uint16_t positions[WALLE_JOINT_COUNT], uint16_t move_time_ms)
{
    (void)calibration; (void)positions; (void)move_time_ms;
    return 0;
}
#endif
