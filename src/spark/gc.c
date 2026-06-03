#include "kerosene.h"

static volatile LONG64 g_spark_live_bytes = 0;

void spark_gc_note_alloc(size_t bytes) {
    InterlockedAdd64(&g_spark_live_bytes, (LONG64)bytes);
}

void spark_gc_note_free(size_t bytes) {
    InterlockedAdd64(&g_spark_live_bytes, -(LONG64)bytes);
}

size_t spark_gc_live_bytes(void) {
    LONG64 value = InterlockedCompareExchange64(&g_spark_live_bytes, 0, 0);
    return value > 0 ? (size_t)value : 0;
}
