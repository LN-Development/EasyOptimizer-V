/*
 * keygen.c — Derive ng.dat / lut.dat from the user's GTA5.exe.
 *
 * Mirrors CodeWalker's GTA5Keys.Generate(): the executable embeds GTA's NG
 * decryption keys, tables and hash LUT at 8-byte-aligned offsets. We ship only
 * the SHA-1 fingerprints of those windows (gtav_key_hashes.dat — no actual key
 * material), scan the exe for byte windows matching them, and reassemble the
 * key files in the exact layout rpf_scan.cpp / fivefury expect:
 *
 *   ng.dat  = 101 NG keys (272 bytes each, = 27472) + 17*16 decrypt tables
 *             (1024 bytes each, = 278528)  -> 306000 bytes total
 *   lut.dat = 256-byte hash lookup table
 */

#include "keygen.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <bcrypt.h>

#pragma comment(lib, "bcrypt.lib")

/* ── Hash-file layout (see gtav_key_hashes.dat generation) ─────────────── */
#define SHA1_LEN          20
#define NG_KEY_COUNT      101
#define NG_KEY_WINDOW     0x110   /* 272 */
#define NG_TABLE_COUNT    272     /* 17 rounds * 16 cells */
#define NG_TABLE_WINDOW   0x400   /* 1024 */
#define AES_WINDOW        0x20    /* 32  (found but not needed for ng/lut)  */
#define LUT_WINDOW        0x100   /* 256 */

#define OFF_AES_HASH      0
#define OFF_LUT_HASH      (OFF_AES_HASH + SHA1_LEN)
#define OFF_NGKEY_HASHES  (OFF_LUT_HASH + SHA1_LEN)
#define OFF_TABLE_HASHES  (OFF_NGKEY_HASHES + NG_KEY_COUNT * SHA1_LEN)
#define HASH_FILE_SIZE    (OFF_TABLE_HASHES + NG_TABLE_COUNT * SHA1_LEN)  /* 7500 */

#define NG_KEYS_SIZE      27472
#define NG_TABLES_SIZE    278528
#define NG_DAT_SIZE       (NG_KEYS_SIZE + NG_TABLES_SIZE)  /* 306000 */
#define LUT_DAT_SIZE      256

#define SCAN_ALIGN        8

/* ── helpers ───────────────────────────────────────────────────────────── */

static uint8_t *read_whole_file(const wchar_t *path, size_t *out_size) {
    FILE *f = _wfopen(path, L"rb");
    if (!f) return NULL;
    _fseeki64(f, 0, SEEK_END);
    long long sz = _ftelli64(f);
    _fseeki64(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return NULL; }
    uint8_t *buf = (uint8_t *)malloc((size_t)sz);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) { free(buf); fclose(f); return NULL; }
    fclose(f);
    *out_size = (size_t)sz;
    return buf;
}

static bool exe_dir_path(const wchar_t *leaf, wchar_t *out, size_t count) {
    wchar_t exe[MAX_PATH];
    if (!GetModuleFileNameW(NULL, exe, MAX_PATH)) return false;
    wchar_t *slash = wcsrchr(exe, L'\\');
    if (slash) *slash = 0;
    _snwprintf(out, count, L"%s\\%s", exe, leaf);
    out[count - 1] = 0;
    return true;
}

/* Reusable SHA-1 context (BCrypt). The hash handle is created once with the
 * REUSABLE flag so the per-window loop only does HashData + FinishHash. */
typedef struct {
    BCRYPT_ALG_HANDLE alg;
    BCRYPT_HASH_HANDLE hash;
    DWORD obj_len;
    uint8_t *obj;
    bool reusable;
} Sha1Ctx;

static bool sha1_init(Sha1Ctx *c) {
    memset(c, 0, sizeof(*c));
    c->reusable = true;
    if (BCryptOpenAlgorithmProvider(&c->alg, BCRYPT_SHA1_ALGORITHM, NULL,
                                    BCRYPT_HASH_REUSABLE_FLAG) < 0) {
        c->reusable = false;
        if (BCryptOpenAlgorithmProvider(&c->alg, BCRYPT_SHA1_ALGORITHM, NULL, 0) < 0)
            return false;
    }
    DWORD cb = 0;
    if (BCryptGetProperty(c->alg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&c->obj_len,
                          sizeof(c->obj_len), &cb, 0) < 0) return false;
    c->obj = (uint8_t *)malloc(c->obj_len);
    if (!c->obj) return false;
    if (c->reusable) {
        if (BCryptCreateHash(c->alg, &c->hash, c->obj, c->obj_len, NULL, 0, 0) < 0)
            return false;
    }
    return true;
}

static void sha1_free(Sha1Ctx *c) {
    if (c->hash) BCryptDestroyHash(c->hash);
    if (c->alg) BCryptCloseAlgorithmProvider(c->alg, 0);
    free(c->obj);
    memset(c, 0, sizeof(*c));
}

static bool sha1_compute(Sha1Ctx *c, const uint8_t *data, size_t len, uint8_t out[20]) {
    if (c->reusable) {
        /* Reusable handle auto-resets after FinishHash. */
        return BCryptHashData(c->hash, (PUCHAR)data, (ULONG)len, 0) >= 0 &&
               BCryptFinishHash(c->hash, out, 20, 0) >= 0;
    }
    BCRYPT_HASH_HANDLE h = NULL;
    if (BCryptCreateHash(c->alg, &h, c->obj, c->obj_len, NULL, 0, 0) < 0) return false;
    bool ok = BCryptHashData(h, (PUCHAR)data, (ULONG)len, 0) >= 0 &&
              BCryptFinishHash(h, out, 20, 0) >= 0;
    BCryptDestroyHash(h);
    return ok;
}

/* Scan `exe` for `count` windows of `window` bytes whose SHA-1 matches one of
 * `hashes[count][20]`. Found windows are copied into out_slots[j] (each must be
 * `window` bytes). Returns the number of distinct hashes matched. */
static int scan_for_hashes(const uint8_t *exe, size_t exe_size,
                           const uint8_t *hashes, int count, int window,
                           uint8_t **out_slots, KeygenProgress progress, void *ctx,
                           const wchar_t *label, int pct_base, int pct_span) {
    Sha1Ctx sc;
    if (!sha1_init(&sc)) return 0;

    char *found = (char *)calloc(count, 1);
    int found_n = 0;
    if (exe_size < (size_t)window) { free(found); sha1_free(&sc); return 0; }

    size_t last_pos = exe_size - (size_t)window;
    size_t step_total = last_pos / SCAN_ALIGN + 1;
    size_t step = 0;
    int last_pct = -1;

    for (size_t pos = 0; pos <= last_pos && found_n < count; pos += SCAN_ALIGN, step++) {
        uint8_t digest[20];
        if (!sha1_compute(&sc, exe + pos, (size_t)window, digest)) continue;

        for (int j = 0; j < count; j++) {
            if (found[j]) continue;
            if (memcmp(digest, hashes + (size_t)j * SHA1_LEN, SHA1_LEN) == 0) {
                memcpy(out_slots[j], exe + pos, (size_t)window);
                found[j] = 1;
                found_n++;
                break;
            }
        }

        if (progress && (step & 0x3FFF) == 0) {
            int pct = pct_base + (int)((long long)pct_span * step / (step_total ? step_total : 1));
            if (pct != last_pct) {
                wchar_t msg[160];
                _snwprintf(msg, 160, L"%s  (%d/%d found)", label, found_n, count);
                progress(msg, pct, ctx);
                last_pct = pct;
            }
        }
    }

    free(found);
    sha1_free(&sc);
    return found_n;
}

/* ── public ────────────────────────────────────────────────────────────── */

bool keygen_from_exe(const wchar_t *exe_path, const wchar_t *out_dir,
                     KeygenProgress progress, void *ctx,
                     char *error, size_t error_size) {
    #define FAIL(msg) do { if (error && error_size) { strncpy(error, msg, error_size - 1); error[error_size-1]=0; } goto cleanup; } while (0)

    bool result = false;
    uint8_t *hashes = NULL, *exe = NULL, *ng = NULL, *lut = NULL;
    uint8_t **ng_key_slots = NULL, **tab_slots = NULL;
    size_t hash_size = 0, exe_size = 0;

    /* 1. Load the SHA-1 fingerprint table that ships with the app. */
    wchar_t hash_path[MAX_PATH];
    if (!exe_dir_path(L"gtav_key_hashes.dat", hash_path, MAX_PATH))
        FAIL("cannot locate application directory");
    hashes = read_whole_file(hash_path, &hash_size);
    if (!hashes || hash_size < HASH_FILE_SIZE)
        FAIL("gtav_key_hashes.dat missing or corrupt (must sit next to the .exe)");

    /* 2. Load the GTA5 executable. */
    if (progress) progress(L"Reading GTA5.exe...", 2, ctx);
    exe = read_whole_file(exe_path, &exe_size);
    if (!exe) FAIL("could not read the selected GTA5.exe");
    if (exe_size < 16 * 1024 * 1024)
        FAIL("selected file is too small to be GTA5.exe");

    ng  = (uint8_t *)calloc(1, NG_DAT_SIZE);
    lut = (uint8_t *)calloc(1, LUT_DAT_SIZE);
    if (!ng || !lut) FAIL("out of memory");

    /* Slot pointers into the output buffers. */
    ng_key_slots = (uint8_t **)malloc(NG_KEY_COUNT * sizeof(uint8_t *));
    tab_slots    = (uint8_t **)malloc(NG_TABLE_COUNT * sizeof(uint8_t *));
    if (!ng_key_slots || !tab_slots) FAIL("out of memory");
    for (int i = 0; i < NG_KEY_COUNT; i++)
        ng_key_slots[i] = ng + (size_t)i * NG_KEY_WINDOW;                 /* keys region */
    for (int i = 0; i < NG_TABLE_COUNT; i++)
        tab_slots[i] = ng + NG_KEYS_SIZE + (size_t)i * NG_TABLE_WINDOW;   /* tables region */

    uint8_t *lut_slot = lut;

    /* 3. Scan for the NG keys (largest hit count first gives early-out). */
    if (progress) progress(L"Searching for NG keys...", 5, ctx);
    int nk = scan_for_hashes(exe, exe_size, hashes + OFF_NGKEY_HASHES, NG_KEY_COUNT,
                             NG_KEY_WINDOW, ng_key_slots, progress, ctx,
                             L"Searching NG keys", 5, 35);
    if (nk != NG_KEY_COUNT)
        FAIL("NG keys not found — wrong/updated GTA5.exe, or not the PC version");

    /* 4. Scan for the 272 decrypt tables. */
    if (progress) progress(L"Searching for NG decrypt tables...", 40, ctx);
    int nt = scan_for_hashes(exe, exe_size, hashes + OFF_TABLE_HASHES, NG_TABLE_COUNT,
                             NG_TABLE_WINDOW, tab_slots, progress, ctx,
                             L"Searching NG tables", 40, 50);
    if (nt != NG_TABLE_COUNT)
        FAIL("NG decrypt tables not found — wrong/updated GTA5.exe");

    /* 5. Scan for the hash LUT. */
    if (progress) progress(L"Searching for hash LUT...", 90, ctx);
    int nl = scan_for_hashes(exe, exe_size, hashes + OFF_LUT_HASH, 1,
                             LUT_WINDOW, &lut_slot, progress, ctx,
                             L"Searching LUT", 90, 8);
    if (nl != 1)
        FAIL("hash LUT not found — wrong/updated GTA5.exe");

    /* 6. Write ng.dat + lut.dat into the output directory. */
    if (progress) progress(L"Writing ng.dat / lut.dat...", 99, ctx);
    wchar_t ng_path[MAX_PATH], lut_path[MAX_PATH];
    _snwprintf(ng_path, MAX_PATH, L"%s\\ng.dat", out_dir);   ng_path[MAX_PATH-1]=0;
    _snwprintf(lut_path, MAX_PATH, L"%s\\lut.dat", out_dir); lut_path[MAX_PATH-1]=0;

    FILE *f = _wfopen(ng_path, L"wb");
    if (!f) FAIL("could not write ng.dat (folder not writable?)");
    bool wok = fwrite(ng, 1, NG_DAT_SIZE, f) == NG_DAT_SIZE;
    fclose(f);
    if (!wok) FAIL("failed writing ng.dat");

    f = _wfopen(lut_path, L"wb");
    if (!f) FAIL("could not write lut.dat");
    wok = fwrite(lut, 1, LUT_DAT_SIZE, f) == LUT_DAT_SIZE;
    fclose(f);
    if (!wok) FAIL("failed writing lut.dat");

    if (progress) progress(L"Keys generated successfully.", 100, ctx);
    LOG("keygen: ng.dat + lut.dat generated from GTA5.exe");
    result = true;

cleanup:
    free(hashes); free(exe); free(ng); free(lut);
    free(ng_key_slots); free(tab_slots);
    return result;
    #undef FAIL
}
