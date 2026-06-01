#ifndef EO_TYPES_H
#define EO_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define EO_MAX_TEXTURES 4096
#define EO_MAX_NAME     256
#define EO_MAX_PATH     1024
#define EO_MAX_YTDS     256

typedef enum {
    TEX_FMT_BC1 = 0,
    TEX_FMT_BC3 = 1,
    TEX_FMT_BC4 = 2,
    TEX_FMT_BC5 = 3,
    TEX_FMT_BC7 = 4,
    TEX_FMT_A8R8G8B8 = 5,
    TEX_FMT_BC2 = 6,
    TEX_FMT_A8 = 21,
    TEX_FMT_R8 = 20,
    TEX_FMT_R8G8B8A8 = 10,
    TEX_FMT_B5G6R5 = 11,
    TEX_FMT_B5G5R5A1 = 12,
    TEX_FMT_UNKNOWN = -1,
} TexFormat;

typedef struct {
    char name[EO_MAX_NAME];
    uint32_t name_hash;
    int width;
    int height;
    TexFormat format;
    int mip_count;
    int stride;
    uint8_t *data;
    size_t data_size;
    void *wtd_meta;
} TextureEntry;

typedef enum {
    ARCHIVE_YTD,
    ARCHIVE_WTD,
    ARCHIVE_MODEL_READONLY
} ArchiveType;

typedef struct {
    ArchiveType type;
    char name[EO_MAX_NAME];
    wchar_t file_path[EO_MAX_PATH];
    TextureEntry *textures;
    int texture_count;
    bool modified;
    bool expanded;
    bool is_preview;      /* consolidated YTD shown as migration preview, not yet committed */
    bool keep_originals;  /* preview only: if set, commit keeps the source textures in originals */
    bool from_rpf;         /* loaded from inside an RPF; unload only, never written back to the container */
    
    uint32_t sys_flags;
    uint32_t gfx_flags;
    void *wtd_meta;
} ArchiveFile;

typedef ArchiveFile YtdFile;

static inline const char *tex_format_name(TexFormat fmt) {
    switch (fmt) {
        case TEX_FMT_BC1:      return "D3DFMT_DXT1";
        case TEX_FMT_BC2:      return "D3DFMT_DXT3";
        case TEX_FMT_BC3:      return "D3DFMT_DXT5";
        case TEX_FMT_BC4:      return "D3DFMT_ATI1";
        case TEX_FMT_BC5:      return "D3DFMT_ATI2";
        case TEX_FMT_BC7:      return "D3DFMT_BC7";
        case TEX_FMT_A8R8G8B8: return "D3DFMT_A8R8G8B8";
        case TEX_FMT_A8:       return "D3DFMT_A8";
        case TEX_FMT_R8:       return "D3DFMT_L8";
        case TEX_FMT_R8G8B8A8: return "R8G8B8A8";
        case TEX_FMT_B5G6R5:   return "D3DFMT_R5G6B5";
        case TEX_FMT_B5G5R5A1: return "D3DFMT_A1R5G5B5";
        default:               return "Unknown";
    }
}

static inline bool tex_format_is_compressed(TexFormat fmt) {
    return fmt == TEX_FMT_BC1 || fmt == TEX_FMT_BC2 || fmt == TEX_FMT_BC3 ||
           fmt == TEX_FMT_BC4 || fmt == TEX_FMT_BC5 || fmt == TEX_FMT_BC7;
}

static inline int tex_format_block_bytes(TexFormat fmt) {
    switch (fmt) {
        case TEX_FMT_BC1: case TEX_FMT_BC4: return 8;
        case TEX_FMT_BC2: case TEX_FMT_BC3: case TEX_FMT_BC5: case TEX_FMT_BC7: return 16;
        default: return 0;
    }
}

static inline int tex_format_pixel_bytes(TexFormat fmt) {
    switch (fmt) {
        case TEX_FMT_A8R8G8B8: case TEX_FMT_R8G8B8A8: return 4;
        case TEX_FMT_B5G6R5: case TEX_FMT_B5G5R5A1: return 2;
        case TEX_FMT_A8: case TEX_FMT_R8: return 1;
        default: return 0;
    }
}

static inline size_t tex_mip_data_size(int w, int h, TexFormat fmt) {
    if (tex_format_is_compressed(fmt)) {
        int bw = (w + 3) / 4; if (bw < 1) bw = 1;
        int bh = (h + 3) / 4; if (bh < 1) bh = 1;
        return (size_t)bw * bh * tex_format_block_bytes(fmt);
    }
    return (size_t)w * h * tex_format_pixel_bytes(fmt);
}

static inline size_t tex_total_mip_size(int w, int h, TexFormat fmt, int mips) {
    size_t total = 0;
    for (int i = 0; i < mips; i++) {
        int mw = w >> i; if (mw < 1) mw = 1;
        int mh = h >> i; if (mh < 1) mh = 1;
        total += tex_mip_data_size(mw, mh, fmt);
    }
    return total;
}

static inline int tex_row_pitch(int w, TexFormat fmt) {
    if (tex_format_is_compressed(fmt)) {
        int bw = (w + 3) / 4; if (bw < 1) bw = 1;
        return bw * tex_format_block_bytes(fmt);
    }
    return w * tex_format_pixel_bytes(fmt);
}

#endif
