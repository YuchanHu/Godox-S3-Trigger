/* SPDX-License-Identifier: GPL-3.0-or-later; test-only NOR flash + failure injection. */
#include "esp_partition.h"
#include <string.h>
static uint8_t flash[0xa0000];
static esp_partition_t partition={sizeof(flash)};
static uint8_t joint_flash[0x200000];
static esp_partition_t joint_partition={sizeof(joint_flash)};
static uint8_t *storage(const esp_partition_t *p){return p==&joint_partition?joint_flash:flash;}
static int erase_calls,write_calls,fail_write,maps;
void mock_flash_reset(void){memset(flash,255,sizeof(flash));memset(joint_flash,255,sizeof(joint_flash));erase_calls=write_calls=fail_write=0;}
void mock_joint_corrupt(unsigned offset){if(offset<sizeof(joint_flash))joint_flash[offset]^=1;}
void mock_flash_fail_write(int n){fail_write=n;}
void mock_flash_corrupt(unsigned offset){if(offset<sizeof(flash))flash[offset]^=1;}
int mock_flash_erase_calls(void){return erase_calls;}
int mock_flash_write_calls(void){return write_calls;}
int mock_flash_map_count(void){return maps;}
const esp_partition_t *esp_partition_find_first(int type,int subtype,const char *label){
    if(type==1&&subtype==0x41&&!strcmp(label,"joint_cache"))return &joint_partition;
    return type==1&&subtype==0x40&&!strcmp(label,"wave_cache")?&partition:0;
}
esp_err_t esp_partition_read(const esp_partition_t *p,size_t offset,void *out,size_t len){
    if(!p||offset>p->size||len>p->size-offset)return ESP_ERR_INVALID_ARG;
    memcpy(out,storage(p)+offset,len);return ESP_OK;
}
esp_err_t esp_partition_write(const esp_partition_t *p,size_t offset,const void *data,size_t len){
    if(!p||offset>p->size||len>p->size-offset)return ESP_ERR_INVALID_ARG;
    if(++write_calls==fail_write)return ESP_FAIL;
    const uint8_t *b=data;
    uint8_t *destination=storage(p);
    for(size_t i=0;i<len;i++)if((destination[offset+i]&b[i])!=b[i])return ESP_ERR_INVALID_STATE;
    for(size_t i=0;i<len;i++)destination[offset+i]&=b[i];return ESP_OK;
}
esp_err_t esp_partition_erase_range(const esp_partition_t *p,size_t offset,size_t len){
    if(!p||offset%4096||len%4096||offset>p->size||len>p->size-offset)return ESP_ERR_INVALID_ARG;
    erase_calls++;memset(storage(p)+offset,255,len);return ESP_OK;
}
esp_err_t esp_partition_mmap(const esp_partition_t *p,size_t offset,size_t len,int memory,const void **out,esp_partition_mmap_handle_t *h){
    (void)memory;if(!p||offset>p->size||len>p->size-offset)return ESP_ERR_INVALID_ARG;
    maps++;*out=storage(p)+offset;*h=1;return ESP_OK;
}
void esp_partition_munmap(esp_partition_mmap_handle_t handle){if(handle)maps--;}
