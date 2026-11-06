#pragma once
#include "queue.h"
typedef QueueHandle_t SemaphoreHandle_t;
SemaphoreHandle_t xSemaphoreCreateBinary(void);
int xSemaphoreTake(SemaphoreHandle_t s,TickType_t wait);
int xSemaphoreGive(SemaphoreHandle_t s);
#define vSemaphoreDelete(s) vQueueDelete(s)
