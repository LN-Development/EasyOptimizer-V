#include "nvtt_c_wrapper.h"
#include "log.h"
#include <windows.h>
#include <stdlib.h>
#include <string.h>

// Typedefs for the NVTT C API
typedef void* NvttInputOptions;
typedef void* NvttCompressionOptions;
typedef void* NvttOutputOptions;
typedef void* NvttCompressor;

typedef struct {
    void (*beginImage)(int size, int width, int height, int depth, int face, int miplevel);
    bool (*writeData)(const void * data, int size);
    void (*endImage)();
} NvttOutputHandler;

static NvttInputOptions (*p_nvttCreateInputOptions)();
static void (*p_nvttDestroyInputOptions)(NvttInputOptions);
static void (*p_nvttSetInputOptionsFormat)(NvttInputOptions, int);
static void (*p_nvttSetInputOptionsMipmapData)(NvttInputOptions, const void*, int, int, int, int, int);

static NvttCompressionOptions (*p_nvttCreateCompressionOptions)();
static void (*p_nvttDestroyCompressionOptions)(NvttCompressionOptions);
static void (*p_nvttSetCompressionOptionsFormat)(NvttCompressionOptions, int);

static NvttOutputOptions (*p_nvttCreateOutputOptions)();
static void (*p_nvttDestroyOutputOptions)(NvttOutputOptions);
static void (*p_nvttSetOutputOptionsOutputHandler)(NvttOutputOptions, NvttOutputHandler*);
static void (*p_nvttSetOutputOptionsOutputHeader)(NvttOutputOptions, bool);

static NvttCompressor (*p_nvttCreateCompressor)();
static void (*p_nvttDestroyCompressor)(NvttCompressor);
static bool (*p_nvttCompress)(NvttCompressor, const NvttInputOptions, const NvttCompressionOptions, const NvttOutputOptions);
static void (*p_nvttEnableCudaAcceleration)(NvttCompressor, bool);

static HMODULE g_hNvtt = NULL;
static NvttCompressor g_compressor = NULL;
static bool g_nvtt_failed = false;   /* cache failure so we don't retry+spam per texture */

bool nvtt_is_available(void) {
    return g_hNvtt != NULL;
}

/* Load nvtt.dll preferring the executable's own directory, so its sibling
 * dependencies (CUDA runtime, etc.) are found via the altered search path. */
static HMODULE nvtt_load_library(void) {
    wchar_t exe_dir[MAX_PATH];
    DWORD n = GetModuleFileNameW(NULL, exe_dir, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        wchar_t *slash = wcsrchr(exe_dir, L'\\');
        if (slash) {
            *slash = 0;
            wchar_t dll_path[MAX_PATH];
            _snwprintf(dll_path, MAX_PATH, L"%s\\nvtt.dll", exe_dir);
            dll_path[MAX_PATH - 1] = 0;
            HMODULE h = LoadLibraryExW(dll_path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
            if (h) { LOG("nvtt_wrapper: loaded from exe dir"); return h; }
            LOG_ERR("nvtt_wrapper: LoadLibraryEx('%ls') failed (GetLastError=%lu)",
                    dll_path, (unsigned long)GetLastError());
        }
    }
    /* Fall back to the default search order. */
    HMODULE h = LoadLibraryA("nvtt.dll");
    if (!h)
        LOG_ERR("nvtt_wrapper: LoadLibrary('nvtt.dll') failed (GetLastError=%lu)",
                (unsigned long)GetLastError());
    return h;
}

void nvtt_wrapper_probe(void) {
    HMODULE h = nvtt_load_library();
    if (!h) {
        LOG("nvtt probe: nvtt.dll NOT loadable -> GPU encoding unavailable (CPU only)");
        return;
    }
    void *fn = (void *)GetProcAddress(h, "nvttCreateCompressor");
    if (fn)
        LOG("nvtt probe: nvtt.dll OK, C API present -> GPU encoding available when toggled");
    else
        LOG("nvtt probe: nvtt.dll loaded but C API missing -> incompatible build, GPU unavailable");
    FreeLibrary(h);
}

bool nvtt_wrapper_init(void) {
    if (g_hNvtt) return true;       // Already loaded
    if (g_nvtt_failed) return false; // Don't retry a known failure (avoids log spam)

    g_hNvtt = nvtt_load_library();
    if (!g_hNvtt) {
        LOG_ERR("nvtt_wrapper: nvtt.dll unavailable; GPU encoding will fall back to CPU");
        g_nvtt_failed = true;
        return false;
    }

    #define LOAD_FUNC(name) \
        p_##name = (void*)GetProcAddress(g_hNvtt, #name); \
        if (!p_##name) { LOG_ERR("nvtt_wrapper: Missing export " #name); FreeLibrary(g_hNvtt); g_hNvtt = NULL; g_nvtt_failed = true; return false; }

    LOAD_FUNC(nvttCreateInputOptions);
    LOAD_FUNC(nvttDestroyInputOptions);
    LOAD_FUNC(nvttSetInputOptionsFormat);
    LOAD_FUNC(nvttSetInputOptionsMipmapData);

    LOAD_FUNC(nvttCreateCompressionOptions);
    LOAD_FUNC(nvttDestroyCompressionOptions);
    LOAD_FUNC(nvttSetCompressionOptionsFormat);

    LOAD_FUNC(nvttCreateOutputOptions);
    LOAD_FUNC(nvttDestroyOutputOptions);
    LOAD_FUNC(nvttSetOutputOptionsOutputHandler);
    LOAD_FUNC(nvttSetOutputOptionsOutputHeader);

    LOAD_FUNC(nvttCreateCompressor);
    LOAD_FUNC(nvttDestroyCompressor);
    LOAD_FUNC(nvttCompress);
    LOAD_FUNC(nvttEnableCudaAcceleration);

    g_compressor = p_nvttCreateCompressor();
    p_nvttEnableCudaAcceleration(g_compressor, true); // Initialize CUDA once

    LOG("nvtt_wrapper: nvtt.dll loaded successfully (GPU/CUDA encoder ready)");
    return true;
}

void nvtt_wrapper_shutdown(void) {
    if (g_compressor) {
        p_nvttDestroyCompressor(g_compressor);
        g_compressor = NULL;
    }
    if (g_hNvtt) {
        FreeLibrary(g_hNvtt);
        g_hNvtt = NULL;
    }
}

// Global state for the output handler since we can't pass user_data easily
static uint8_t *g_out_buffer = NULL;
static size_t g_out_size = 0;
static size_t g_out_capacity = 0;

static void out_beginImage(int size, int width, int height, int depth, int face, int miplevel) {
    // We can preallocate
}

static bool out_writeData(const void * data, int size) {
    if (g_out_size + size > g_out_capacity) {
        g_out_capacity = (g_out_capacity == 0) ? size * 2 : (g_out_capacity * 2) + size;
        g_out_buffer = realloc(g_out_buffer, g_out_capacity);
    }
    memcpy(g_out_buffer + g_out_size, data, size);
    g_out_size += size;
    return true;
}

static void out_endImage() {
}

static NvttOutputHandler g_output_handler = {
    out_beginImage,
    out_writeData,
    out_endImage
};

bool nvtt_encode(const uint8_t *bgra_data, int width, int height, NvttFormat format, uint8_t **out_data, size_t *out_size) {
    if (!nvtt_wrapper_init()) return false;

    NvttInputOptions inputOptions = p_nvttCreateInputOptions();
    p_nvttSetInputOptionsFormat(inputOptions, NVTT_INPUT_FORMAT_BGRA_8UB);
    p_nvttSetInputOptionsMipmapData(inputOptions, bgra_data, width, height, 1, 0, 0);

    NvttCompressionOptions compressionOptions = p_nvttCreateCompressionOptions();
    p_nvttSetCompressionOptionsFormat(compressionOptions, format);

    NvttOutputOptions outputOptions = p_nvttCreateOutputOptions();
    p_nvttSetOutputOptionsOutputHeader(outputOptions, false); // No DDS header
    p_nvttSetOutputOptionsOutputHandler(outputOptions, &g_output_handler);

    // Reset buffer
    g_out_size = 0;
    if (g_out_buffer) {
        free(g_out_buffer);
        g_out_buffer = NULL;
        g_out_capacity = 0;
    }

    bool success = p_nvttCompress(g_compressor, inputOptions, compressionOptions, outputOptions);

    p_nvttDestroyOutputOptions(outputOptions);
    p_nvttDestroyCompressionOptions(compressionOptions);
    p_nvttDestroyInputOptions(inputOptions);

    if (success && g_out_size > 0) {
        *out_data = g_out_buffer;
        *out_size = g_out_size;
        // Detach buffer from globals so caller can free it
        g_out_buffer = NULL;
        g_out_capacity = 0;
        g_out_size = 0;
        return true;
    }

    return false;
}
