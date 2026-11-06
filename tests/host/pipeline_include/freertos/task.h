#pragma once
#include "FreeRTOS.h"
int xTaskCreatePinnedToCore(void (*fn)(void*),const char *name,unsigned stack,void *arg,unsigned priority,void *task,unsigned core);
void vTaskDelete(void *task);
void vTaskDelay(TickType_t ticks);
