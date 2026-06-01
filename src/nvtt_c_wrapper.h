#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Texture format (from NVTT 2)
typedef enum {
    NVTT_FORMAT_BC1 = 1,
    NVTT_FORMAT_BC3 = 4,
    NVTT_FORMAT_BC7 = 11,
} NvttFormat;

// Input format (from NVTT 2)
typedef enum {
    NVTT_INPUT_FORMAT_BGRA_8UB = 0,
} NvttInputFormat;

// Initialize the NVTT wrapper (loads nvtt.dll)
bool nvtt_wrapper_init(void);

// True if nvtt.dll is loaded and ready (does not attempt a load on its own
// after a previous failure was cached).
bool nvtt_is_available(void);

// Lightweight startup probe: tries to load nvtt.dll and verify the C API is
// present, logging the result, then unloads. Does NOT initialize CUDA.
void nvtt_wrapper_probe(void);

// Shutdown the NVTT wrapper
void nvtt_wrapper_shutdown(void);

// Compress a single image
// Returns true on success, allocates out_data which must be freed by caller
bool nvtt_encode(const uint8_t *bgra_data, int width, int height, NvttFormat format, uint8_t **out_data, size_t *out_size);

#ifdef __cplusplus
}
#endif
