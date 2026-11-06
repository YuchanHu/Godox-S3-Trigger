/* Exercise the real cache builder with a fragmented-heap allocation limit. */
#include <stdlib.h>
static int deny_wave_allocation;
void mock_wave_heap_limit(int deny) { deny_wave_allocation=deny; }
static void *limited_malloc(size_t bytes) {
    if(deny_wave_allocation && bytes>=64512)return NULL;
    return malloc(bytes);
}
#define malloc limited_malloc
#include "../../components/stage_control/stage_cache.c"
