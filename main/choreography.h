#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
bool choreography_validate(const char *data,size_t size,char *error,size_t capacity);
void choreography_init(QueueHandle_t commands);
int choreography_start(const char *id);
void choreography_stop(void);
bool choreography_playing(void);
