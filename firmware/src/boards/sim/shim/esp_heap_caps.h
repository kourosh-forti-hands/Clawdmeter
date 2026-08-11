#pragma once
// Native shim: capability-tagged heap allocations become plain malloc.
#include <stdlib.h>

#define MALLOC_CAP_SPIRAM   (1u << 0)
#define MALLOC_CAP_INTERNAL (1u << 1)
#define MALLOC_CAP_8BIT     (1u << 2)
#define MALLOC_CAP_DMA      (1u << 3)

static inline void* heap_caps_malloc(size_t size, unsigned caps) {
    (void)caps;
    return malloc(size);
}
static inline void heap_caps_free(void* ptr) { free(ptr); }

// The System page reports free heap/PSRAM. A desktop has neither in any
// meaningful sense, so report a plausible constant rather than 0 — a zero
// would render as "0 KB free" and look like a bug in the sim.
static inline size_t heap_caps_get_free_size(unsigned caps) {
    (void)caps;
    return 4u * 1024u * 1024u;
}
