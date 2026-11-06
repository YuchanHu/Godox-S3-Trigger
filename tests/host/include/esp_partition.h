/* Memory-backed flash adapter for executing the real cache code in host tests. */
#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>
#define ESP_PARTITION_TYPE_DATA 1
#define ESP_PARTITION_MMAP_DATA 0
typedef struct {size_t size;} esp_partition_t;
typedef uint32_t esp_partition_mmap_handle_t;
const esp_partition_t *esp_partition_find_first(int type,int subtype,const char *label);
esp_err_t esp_partition_read(const esp_partition_t *,size_t,void *,size_t);
esp_err_t esp_partition_write(const esp_partition_t *,size_t,const void *,size_t);
esp_err_t esp_partition_erase_range(const esp_partition_t *,size_t,size_t);
esp_err_t esp_partition_mmap(const esp_partition_t *,size_t,size_t,int,const void **,esp_partition_mmap_handle_t *);
void esp_partition_munmap(esp_partition_mmap_handle_t);
