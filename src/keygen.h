#ifndef EO_KEYGEN_H
#define EO_KEYGEN_H

#include <stdbool.h>
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Generate ng.dat and lut.dat next to the running executable by scanning the
 * user's GTA5.exe for the key windows whose SHA-1 fingerprints are stored in
 * gtav_key_hashes.dat (CodeWalker's approach — only hashes are distributed,
 * never the keys themselves).
 *
 * exe_path : full path to gta5.exe / gta5_enhanced.exe chosen by the user.
 * out_dir  : directory to write ng.dat and lut.dat into (the exe folder).
 * progress : optional callback for status text (may be NULL).
 * error    : receives a human-readable failure reason (may be NULL).
 *
 * Returns true on success (both files written). */
typedef void (*KeygenProgress)(const wchar_t *status, int percent, void *ctx);

bool keygen_from_exe(const wchar_t *exe_path, const wchar_t *out_dir,
                     KeygenProgress progress, void *ctx,
                     char *error, size_t error_size);

#ifdef __cplusplus
}
#endif

#endif
