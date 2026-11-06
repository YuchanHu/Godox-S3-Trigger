/* Threaded host stand-ins. Tests execute the production pipeline C code. */
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "rf_receiver.h"
#include "rf_transmitter.h"
typedef struct {
    CRITICAL_SECTION lock;
    HANDLE items,spaces;
    unsigned count,size,read,write;
    unsigned char *data;
} queue_t;
static HANDLE atomic_mutex;
static volatile LONG queues,threads,cancelled,captures,releases;
static int init_error,capture_error,bad_size,task_fail,fail_init_once;
static int psram_available;
void *heap_caps_malloc(size_t size,unsigned caps) {
    return psram_available ? malloc(size) : NULL;
}
void mock_rx_psram(int available) { psram_available=available; }
static unsigned frequencies[4096];
QueueHandle_t xQueueCreate(unsigned count,unsigned size) {
    queue_t *q=calloc(1,sizeof(*q));q->data=calloc(count,size);
    q->count=count;q->size=size;InitializeCriticalSection(&q->lock);
    q->items=CreateSemaphoreA(NULL,0,count,NULL);q->spaces=CreateSemaphoreA(NULL,count,count,NULL);
    InterlockedIncrement(&queues);return q;
}
int xQueueSend(QueueHandle_t handle,const void *item,TickType_t wait) {
    queue_t *q=handle;
    if(WaitForSingleObject(q->spaces,wait)!=WAIT_OBJECT_0)return 0;
    EnterCriticalSection(&q->lock);
    memcpy(q->data+q->write*q->size,item,q->size);q->write=(q->write+1)%q->count;
    LeaveCriticalSection(&q->lock);ReleaseSemaphore(q->items,1,NULL);return 1;
}
int xQueueReceive(QueueHandle_t handle,void *item,TickType_t wait) {
    queue_t *q=handle;
    if(WaitForSingleObject(q->items,wait)!=WAIT_OBJECT_0)return 0;
    EnterCriticalSection(&q->lock);
    memcpy(item,q->data+q->read*q->size,q->size);q->read=(q->read+1)%q->count;
    LeaveCriticalSection(&q->lock);ReleaseSemaphore(q->spaces,1,NULL);return 1;
}
void vQueueDelete(QueueHandle_t handle) {
    queue_t *q=handle;CloseHandle(q->items);CloseHandle(q->spaces);
    DeleteCriticalSection(&q->lock);free(q->data);free(q);InterlockedDecrement(&queues);
}
SemaphoreHandle_t xSemaphoreCreateBinary(void){return xQueueCreate(1,1);}
int xSemaphoreGive(SemaphoreHandle_t s){char byte=1;return xQueueSend(s,&byte,0);}
int xSemaphoreTake(SemaphoreHandle_t s,TickType_t wait){char byte;return xQueueReceive(s,&byte,wait);}
typedef struct {void(*fn)(void*);void *arg;} start_t;
static DWORD WINAPI run(void *arg) {
    start_t start=*(start_t*)arg;free(arg);start.fn(start.arg);return 0;
}
int xTaskCreatePinnedToCore(void(*fn)(void*),const char *name,unsigned stack,void *arg,unsigned priority,void *task,unsigned core) {
    if(task_fail)return 0;
    start_t *start=malloc(sizeof(*start));start->fn=fn;start->arg=arg;
    InterlockedIncrement(&threads);
    HANDLE thread=CreateThread(NULL,0,run,start,0,NULL);
    if(!thread){InterlockedDecrement(&threads);free(start);return 0;}
    CloseHandle(thread);return 1;
}
void vTaskDelay(TickType_t ticks){Sleep(ticks);}
void vTaskDelete(void *task){InterlockedDecrement(&threads);ExitThread(0);}
int64_t esp_timer_get_time(void){return (int64_t)GetTickCount()*1000;}
esp_err_t rf_receiver_init(void){if(fail_init_once){fail_init_once=0;return ESP_ERR_NO_MEM;}return init_error;}
esp_err_t rf_receiver_release(void){InterlockedIncrement(&releases);return ESP_OK;}
bool rf_transmitter_cancelled(void){return InterlockedCompareExchange(&cancelled,0,0)!=0;}
esp_err_t rf_receiver_capture(unsigned wifi,const uint32_t **samples,size_t *count) {
    static uint32_t raw[16128];
    InterlockedIncrement(&captures);
    unsigned n=captures;
    frequencies[(n-1)%4096]=wifi;Sleep(1);
    for(unsigned i=0;i<16128;i++)raw[i]=((n&127)*4)|((1020u)<<10);
    *samples=raw;*count=bad_size?1:16128;
    return capture_error;
}
rf_rx_metrics_t rf_receiver_metrics(void){rf_rx_metrics_t m={1008,100,200,0,0,0};return m;}
void mock_rx_reset(int init,int capture,int size,int task) {
    if(!atomic_mutex)atomic_mutex=CreateMutexA(NULL,FALSE,NULL);
    init_error=init;capture_error=capture;bad_size=size;task_fail=task;fail_init_once=0;
    psram_available=0;
    cancelled=captures=releases=0;memset(frequencies,0,sizeof(frequencies));
}
void mock_rx_cancel(void){InterlockedExchange(&cancelled,1);}
void mock_rx_fail_init_once(void){fail_init_once=1;}
int mock_rx_captures(void){return captures;}
int mock_rx_releases(void){return releases;}
int mock_rx_queues(void){return queues;}
int mock_rx_threads(void){return threads;}
unsigned mock_rx_wifi(unsigned index){return frequencies[index%4096];}



LONG mock_atomic_load(volatile LONG *p) {
    WaitForSingleObject(atomic_mutex,INFINITE);LONG v=*p;ReleaseMutex(atomic_mutex);return v;
}
LONG mock_atomic_exchange(volatile LONG *p,LONG value) {
    WaitForSingleObject(atomic_mutex,INFINITE);LONG old=*p;*p=value;ReleaseMutex(atomic_mutex);return old;
}
esp_err_t rf_receiver_capture_iq8(unsigned wifi,int8_t *out,rf_rx_metrics_t *metrics) {
    const uint32_t *raw;size_t count;
    esp_err_t e=rf_receiver_capture(wifi,&raw,&count);
    if(e!=ESP_OK)return e;
    if(count!=16128)return ESP_ERR_INVALID_RESPONSE;
    for(unsigned k=0;k<count;k++){out[2*k]=(int8_t)((raw[k]>>2)&255);out[2*k+1]=(int8_t)((raw[k]>>12)&255);}
    *metrics=rf_receiver_metrics();return ESP_OK;
}

esp_err_t rf_receiver_capture_iq8_rate(unsigned wifi,unsigned rate,int8_t *out,rf_rx_metrics_t *metrics) {
    esp_err_t e=rf_receiver_capture_iq8(wifi,out,metrics);
    if(e==ESP_OK)metrics->elapsed_us=rate==40000000?403:1008;
    return e;
}
