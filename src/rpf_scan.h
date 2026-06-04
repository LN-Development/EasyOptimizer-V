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

/* A single file to substitute when rewriting an RPF. `entry_path` is the logical
 * path as produced by rpf_scan_file (forward slashes). For resource files
 * (.ytd/.ydr/.yft/.ydd/.wtd) `data` is the full standalone RSC7 file (16-byte
 * header + compressed payload), exactly as ytd_save/ydr_save produce. */
typedef struct {
    const wchar_t *entry_path;
    const uint8_t *data;
    size_t         data_size;
} RpfReplacement;

/* Rewrite a (non-nested, non-NG) RPF7 archive: copy every entry verbatim except
 * those listed in `replacements`, which are swapped for new bytes, then repack
 * the table of contents and file data. Nested .rpf containers are preserved as
 * opaque blobs (their inner files cannot be edited). `dst_path` may equal
 * `src_path` (the caller is responsible for any backup). NG-encrypted archives
 * are rejected. Returns the number of replacements applied, or -1 on error. */
int rpf_rewrite_file(const wchar_t *src_path, const wchar_t *dst_path,
                     const RpfReplacement *replacements, int replacement_count,
                     char *error, size_t error_size);

#ifdef __cplusplus
}
#endif

#endif
