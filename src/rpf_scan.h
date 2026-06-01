#ifndef EO_RPF_SCAN_H
#define EO_RPF_SCAN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef bool (*RpfScanCallback)(const wchar_t *entry_path, const uint8_t *data,
                                size_t data_size, void *context);

int rpf_scan_file(const wchar_t *path, RpfScanCallback callback, void *context,
                  char *error, size_t error_size);

#ifdef __cplusplus
}
#endif

#endif
