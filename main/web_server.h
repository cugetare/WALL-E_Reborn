#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "automation.h"

int walle_web_start(QueueHandle_t command_queue);
void walle_web_controller_update(const walle_controller_state_t *state,
                                 uint32_t now_ms, bool motion_ready);
