#include "gui.h"
#include <shellapi.h>
#include "theme.h"
#include "ytd.h"
#include "wtd.h"
#include "ydr.h"
#include "texture.h"
#include "image.h"
#include "optimizer.h"
#include "dds.h"
#include "bc7enc_wrapper.h"
#include "nvtt_c_wrapper.h"
#include "rpf_scan.h"
#include "keygen.h"
#include "log.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shlwapi.h>
#include <shlobj.h>
#include <windowsx.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "msimg32.lib")

AppState g_app = {0};

#define ID_SIDEBAR_ADDYTD     1001
#define ID_SIDEBAR_SAVEALL    1002
#define ID_SIDEBAR_CLEARALL   1003
#define ID_SIDEBAR_DETECT     1004
#define ID_SIDEBAR_OPTIMIZE   1005
#define ID_SIDEBAR_FASTRECOMP 1006
#define ID_SIDEBAR_TOGGLE_ENC 1007
#define ID_SIDEBAR_ADDFOLDER  1008
#define ID_SAVE_REPLACE_ORIGINALS 4001
#define ID_SAVE_TO_FOLDER         4002
#define ID_SAVE_PROJECT_CACHE     4003
#define ID_SIDEBAR_LANGUAGE   1009
#define ID_SIDEBAR_SORT       1011
#define ID_SIDEBAR_MIGRATE    1013
#define ID_SIDEBAR_SAMEFORMAT 1014
#define ID_SIDEBAR_GRID       1015

#define IDC_DET_CRITERION 3030
#define IDC_MIG_CRITERION 3031
#define IDC_MIG_STRATEGY  3032
#define IDC_IMPORT_YTD    3040
#define IDC_IMPORT_YFT    3041
#define IDC_IMPORT_YDD    3042
#define IDC_IMPORT_YDR    3043
#define IDC_IMPORT_WTD    3044
#define IDC_IMPORT_RPF    3045

#define IDC_RES_MAXW 3011
#define IDC_RES_MAXH 3012
#define IDC_RES_FMT  3013
#define IDC_RES_MIPMODE 3014
#define IDC_RES_MIPVAL 3015

typedef struct {
    int max_w;
    int max_h;
    TexFormat fmt;
    int mips;
} CustomResizeParams;

#define ID_SEARCH_BOX       1010

/* Context menu IDs */
#define IDM_CTX_RESIZE_HALF  2001
#define IDM_CTX_RESIZE_QRTR  2002
#define IDM_CTX_RESIZE_CUSTOM 2003
#define IDM_CTX_EXPORT_DDS   2004
#define IDM_CTX_REMOVE       2005
#define IDM_CTX_UNLOAD       2006
#define IDM_ARCH_UNLOAD      2007
#define IDM_CTX_FMT_BC1      2010
#define IDM_CTX_FMT_BC3      2011
#define IDM_CTX_FMT_BC5      2012
#define IDM_CTX_FMT_BC7      2013
#define IDM_CTX_FMT_KEEP     2014

/* Smart optimize dialog control IDs */
#define IDC_OPT_MAXW    3001
#define IDC_OPT_MAXH    3002
#define IDC_OPT_FMT     3003
#define IDC_OPT_MIPMODE 3004
#define IDC_OPT_MIPVAL  3005

#define SIDEBAR_WIDTH   220
#define HEADER_HEIGHT   56
#define STATUS_HEIGHT   28
#define CARD_MARGIN     8
#define FOLDER_H        56

/* Texture grid card size is adjustable at runtime (Small / Medium / Native),
 * mirroring the C# "Grid" button. CARD_W/CARD_H read the active size. */
static int g_grid_index = 1;   /* 0=Small, 1=Medium, 2=Native */
static const int g_grid_w[3] = {160, 220, 300};
static const int g_grid_h[3] = {200, 260, 340};
#define CARD_W (g_grid_w[g_grid_index])
#define CARD_H (g_grid_h[g_grid_index])

/* ── Forward declarations ──────────────────────────────────────────── */

static LRESULT CALLBACK MainWndProc(HWND, UINT, WPARAM, LPARAM);
static LRESULT CALLBACK ContentWndProc(HWND, UINT, WPARAM, LPARAM);
static LRESULT CALLBACK SidebarWndProc(HWND, UINT, WPARAM, LPARAM);
static void layout_children(void);
static void open_file_dialog(HWND parent);
static void open_folder_dialog(HWND parent);
static void save_all(void);
static void save_project_cache(void);
static void do_detect_duplicates(void);
static void do_migrate_duplicates(void);
static void do_smart_optimize(void);
static void paint_content(HWND hwnd, HDC hdc);
static void paint_sidebar(HWND hwnd, HDC hdc);
static void paint_header(HWND hwnd, HDC hdc);
static bool hit_test_texture(int mx, int my, int *out_ytd, int *out_tex, int *out_cx, int *out_cy);
static void show_texture_context_menu(HWND hwnd, int screen_x, int screen_y, int ytd_idx, int tex_idx);
static bool do_texture_resize(int ytd_idx, int tex_idx, int new_w, int new_h, TexFormat fmt, int max_mips);
static void do_fast_recompress(void);
static void do_same_format_recompress(void);
static void choose_recompress_mode(void);
static void do_custom_resize(int ytd_idx, int tex_idx);
static void do_texture_export_dds(int ytd_idx, int tex_idx);
static void do_texture_remove(int ytd_idx, int tex_idx);
static void do_texture_unload(int ytd_idx, int tex_idx);
static bool ng_keys_available(void);
static void log_ng_keys_state(void);
static bool ensure_ng_keys_for_rpf(void);
static void unload_rpf_archive(int ytd_idx);
static int texture_grid_height(YtdFile *ytd, int area_w);
static void select_language(void);
static void apply_archive_sort(void);
static bool select_import_types(HWND parent);

/* ── Sidebar button struct ─────────────────────────────────────────── */

typedef struct {
    RECT rc;
    int id;
    const wchar_t *text;
    bool hovered;
    bool visible;
} SidebarButton;

static SidebarButton g_sidebar_btns[] = {
    {{0}, ID_SIDEBAR_ADDYTD,     L"Add File",       false, true},
    {{0}, ID_SIDEBAR_ADDFOLDER,  L"Add Folder",     false, true},
    {{0}, ID_SIDEBAR_SAVEALL,    L"Save All",       false, true},
    {{0}, ID_SIDEBAR_CLEARALL,   L"Clear All",      false, true},
    {{0}, ID_SIDEBAR_DETECT,     L"Detect Duplicates", false, true},
    {{0}, ID_SIDEBAR_MIGRATE,    L"Migrate Dups",      false, false},  /* shown only after detect */
    {{0}, ID_SIDEBAR_OPTIMIZE,   L"Smart Optimize", false, true},
    {{0}, ID_SIDEBAR_FASTRECOMP, L"Recompress",     false, true},
    {{0}, ID_SIDEBAR_SAMEFORMAT, L"",               false, false},
    {{0}, ID_SIDEBAR_TOGGLE_ENC, L"Encoder: CPU",   false, true},
    {{0}, ID_SIDEBAR_LANGUAGE,   L"Language: English", false, true},
    {{0}, ID_SIDEBAR_SORT,       L"Sort: Name", false, true},
    {{0}, ID_SIDEBAR_GRID,       L"Grid: Medium", false, true},
};
#define SIDEBAR_BTN_COUNT (sizeof(g_sidebar_btns)/sizeof(g_sidebar_btns[0]))

typedef enum {
    UI_LANG_ENGLISH,
    UI_LANG_PORTUGUESE,
    UI_LANG_SPANISH,
    UI_LANG_RUSSIAN,
    UI_LANG_TURKISH,
    UI_LANG_MANDARIN,
    UI_LANG_HINDI,
    UI_LANG_JAPANESE,
    UI_LANG_ARABIC,
    UI_LANG_BENGALI,
    UI_LANG_FRENCH,
    UI_LANG_GERMAN,
    UI_LANG_INDONESIAN,
    UI_LANG_KOREAN,
    UI_LANG_ITALIAN
} AppLanguage;

static AppLanguage g_language = UI_LANG_ENGLISH;

typedef enum {
    SORT_BY_NAME,
    SORT_BY_TYPE,
    SORT_BY_SIZE,
    SORT_BY_TEXTURE_COUNT,
    SORT_BY_RESOLUTION,
    SORT_BY_MIPMAPS,
    SORT_BY_COMPRESSION,
    SORT_BY_MODIFIED
} ArchiveSortMode;

static ArchiveSortMode g_sort_mode = SORT_BY_NAME;
static int g_scan_candidates = 0;
static int g_scan_failed = 0;
static bool g_bulk_add = false;   /* true during folder scans / multi-file loads */

typedef struct {
    bool ytd;
    bool yft;
    bool ydd;
    bool ydr;
    bool wtd;
    bool rpf;
} ImportFilter;

static ImportFilter g_import_filter = {true, true, true, true, true, true};

/* Pending migration preview (built by Detect, committed by Migrate Dups) */
static PendingRemoval *g_pending_removals = NULL;
static int g_pending_removal_count = 0;
static bool g_has_pending_migration = false;

/* Log which encoder a batch operation will drive (and whether GPU is usable). */
static void log_encoder_intent(const char *op) {
    if (g_app.use_gpu_encoding) {
        bool ready = nvtt_wrapper_init();
        LOG("%s: encoder = GPU%s", op,
            ready ? " (NVTT/CUDA ready)"
                  : " requested, but NVTT unavailable -> CPU (bc7enc) fallback");
    } else {
        LOG("%s: encoder = CPU (bc7enc ISPC)", op);
    }
}

static size_t archive_total_size(const YtdFile *archive) {
    size_t total = 0;
    for (int i = 0; i < archive->texture_count; i++)
        total += archive->textures[i].data_size;
    return total;
}

static const TextureEntry *archive_largest_texture(const YtdFile *archive) {
    const TextureEntry *best = NULL;
    int best_area = -1;
    for (int i = 0; i < archive->texture_count; i++) {
        const TextureEntry *tex = &archive->textures[i];
        int area = tex->width * tex->height;
        if (!best || area > best_area) {
            best = tex;
            best_area = area;
        }
    }
    return best;
}

static int archive_max_resolution(const YtdFile *archive) {
    const TextureEntry *tex = archive_largest_texture(archive);
    return tex ? tex->width * tex->height : 0;
}

static int archive_max_mipmaps(const YtdFile *archive) {
    int best = 0;
    for (int i = 0; i < archive->texture_count; i++)
        if (archive->textures[i].mip_count > best) best = archive->textures[i].mip_count;
    return best;
}

static TexFormat archive_primary_format(const YtdFile *archive) {
    const TextureEntry *tex = archive_largest_texture(archive);
    return tex ? tex->format : TEX_FMT_UNKNOWN;
}

static int compare_archives(const void *left, const void *right) {
    const YtdFile *a = *(YtdFile * const *)left;
    const YtdFile *b = *(YtdFile * const *)right;
    switch (g_sort_mode) {
        case SORT_BY_TYPE:
            return _wcsicmp(PathFindExtensionW(a->file_path), PathFindExtensionW(b->file_path));
        case SORT_BY_SIZE: {
            size_t as = archive_total_size(a), bs = archive_total_size(b);
            return (as < bs) - (as > bs);
        }
        case SORT_BY_TEXTURE_COUNT:
            return (a->texture_count < b->texture_count) - (a->texture_count > b->texture_count);
        case SORT_BY_RESOLUTION: {
            int ar = archive_max_resolution(a), br = archive_max_resolution(b);
            if (ar != br) return (ar < br) - (ar > br);
            break;
        }
        case SORT_BY_MIPMAPS: {
            int am = archive_max_mipmaps(a), bm = archive_max_mipmaps(b);
            if (am != bm) return (am < bm) - (am > bm);
            break;
        }
        case SORT_BY_COMPRESSION: {
            int cf = _stricmp(tex_format_name(archive_primary_format(a)),
                              tex_format_name(archive_primary_format(b)));
            if (cf != 0) return cf;
            break;
        }
        case SORT_BY_MODIFIED:
            return (a->modified < b->modified) - (a->modified > b->modified);
        default:
            return _stricmp(a->name, b->name);
    }
    return _stricmp(a->name, b->name);
}

static int compare_textures(const void *left, const void *right) {
    const TextureEntry *a = (const TextureEntry *)left;
    const TextureEntry *b = (const TextureEntry *)right;
    switch (g_sort_mode) {
        case SORT_BY_SIZE:
            if (a->data_size != b->data_size)
                return (a->data_size < b->data_size) - (a->data_size > b->data_size);
            break;
        case SORT_BY_RESOLUTION: {
            int ar = a->width * a->height;
            int br = b->width * b->height;
            if (ar != br) return (ar < br) - (ar > br);
            if (a->width != b->width) return (a->width < b->width) - (a->width > b->width);
            if (a->height != b->height) return (a->height < b->height) - (a->height > b->height);
            break;
        }
        case SORT_BY_MIPMAPS:
            if (a->mip_count != b->mip_count)
                return (a->mip_count < b->mip_count) - (a->mip_count > b->mip_count);
            break;
        case SORT_BY_COMPRESSION:
        case SORT_BY_TYPE: {
            int fmt = _stricmp(tex_format_name(a->format), tex_format_name(b->format));
            if (fmt != 0) return fmt;
            break;
        }
        case SORT_BY_NAME:
        default:
            break;
    }
    return _stricmp(a->name, b->name);
}

static void apply_texture_sort(void) {
    switch (g_sort_mode) {
        case SORT_BY_NAME:
        case SORT_BY_TYPE:
        case SORT_BY_SIZE:
        case SORT_BY_RESOLUTION:
        case SORT_BY_MIPMAPS:
        case SORT_BY_COMPRESSION:
            break;
        default:
            return;
    }

    for (int i = 0; i < g_app.ytd_count; i++) {
        YtdFile *archive = g_app.ytds[i];
        if (!archive || archive->is_rpf_group || !archive->textures || archive->texture_count <= 1)
            continue;
        qsort(archive->textures, archive->texture_count, sizeof(archive->textures[0]), compare_textures);
    }
}

static void apply_archive_sort(void) {
    apply_texture_sort();
    if (g_app.ytd_count > 1)
        qsort(g_app.ytds, g_app.ytd_count, sizeof(g_app.ytds[0]), compare_archives);
}

static bool is_supported_archive_path(const wchar_t *path) {
    const wchar_t *ext = PathFindExtensionW(path);
    return _wcsicmp(ext, L".ytd") == 0 || _wcsicmp(ext, L".wtd") == 0 ||
           _wcsicmp(ext, L".ydr") == 0 || _wcsicmp(ext, L".yft") == 0 ||
           _wcsicmp(ext, L".ydd") == 0 || _wcsicmp(ext, L".rpf") == 0;
}

static bool should_import_archive_path(const wchar_t *path) {
    const wchar_t *ext = PathFindExtensionW(path);
    if (_wcsicmp(ext, L".ytd") == 0) return g_import_filter.ytd;
    if (_wcsicmp(ext, L".yft") == 0) return g_import_filter.yft;
    if (_wcsicmp(ext, L".ydd") == 0) return g_import_filter.ydd;
    if (_wcsicmp(ext, L".ydr") == 0) return g_import_filter.ydr;
    if (_wcsicmp(ext, L".wtd") == 0) return g_import_filter.wtd;
    if (_wcsicmp(ext, L".rpf") == 0) return g_import_filter.rpf;
    return false;
}

static const wchar_t *trw(const wchar_t *en, const wchar_t *pt,
                           const wchar_t *es, const wchar_t *ru) {
    switch (g_language) {
        case UI_LANG_PORTUGUESE: return pt;
        case UI_LANG_SPANISH: return es;
        case UI_LANG_RUSSIAN: return ru;
        default: return en;
    }
}

static const wchar_t *trw8(const wchar_t *en, const wchar_t *pt, const wchar_t *es,
                            const wchar_t *ru, const wchar_t *tr, const wchar_t *zh,
                            const wchar_t *hi, const wchar_t *ja) {
    switch (g_language) {
        case UI_LANG_PORTUGUESE: return pt;
        case UI_LANG_SPANISH: return es;
        case UI_LANG_RUSSIAN: return ru;
        case UI_LANG_TURKISH: return tr;
        case UI_LANG_MANDARIN: return zh;
        case UI_LANG_HINDI: return hi;
        case UI_LANG_JAPANESE: return ja;
        default: return en;
    }
}

static const wchar_t *trw9(const wchar_t *en, const wchar_t *pt, const wchar_t *es,
                            const wchar_t *ru, const wchar_t *tr, const wchar_t *zh,
                            const wchar_t *hi, const wchar_t *ja, const wchar_t *ar) {
    if (g_language == UI_LANG_ARABIC) return ar;
    return trw8(en, pt, es, ru, tr, zh, hi, ja);
}

static const wchar_t *language_button_text(void) {
    switch (g_language) {
        case UI_LANG_PORTUGUESE: return L"Idioma: Português";
        case UI_LANG_SPANISH: return L"Idioma: Español";
        case UI_LANG_RUSSIAN: return L"Язык: Русский";
        case UI_LANG_TURKISH: return L"Dil: Türkçe";
        case UI_LANG_MANDARIN: return L"语言: 中文";
        case UI_LANG_HINDI: return L"भाषा: हिन्दी";
        case UI_LANG_JAPANESE: return L"言語: 日本語";
        case UI_LANG_ARABIC: return L"اللغة: العربية";
        case UI_LANG_BENGALI: return L"ভাষা: বাংলা";
        case UI_LANG_FRENCH: return L"Langue : Français";
        case UI_LANG_GERMAN: return L"Sprache: Deutsch";
        case UI_LANG_INDONESIAN: return L"Bahasa: Indonesia";
        case UI_LANG_KOREAN: return L"언어: 한국어";
        case UI_LANG_ITALIAN: return L"Lingua: Italiano";
        default: return L"Language: English";
    }
}

static void update_sidebar_labels(void) {
    g_sidebar_btns[0].text = trw9(L"Add File", L"Adicionar arquivo", L"Añadir archivo", L"Добавить файл", L"Dosya ekle", L"添加文件", L"फ़ाइल जोड़ें", L"ファイル追加", L"إضافة ملف");
    g_sidebar_btns[1].text = trw9(L"Add Folder", L"Adicionar pasta", L"Añadir carpeta", L"Добавить папку", L"Klasör ekle", L"添加文件夹", L"फ़ोल्डर जोड़ें", L"フォルダー追加", L"إضافة مجلد");
    g_sidebar_btns[2].text = trw9(L"Save All", L"Salvar tudo", L"Guardar todo", L"Сохранить все", L"Tümünü kaydet", L"全部保存", L"सभी सहेजें", L"すべて保存", L"حفظ الكل");
    g_sidebar_btns[3].text = trw9(L"Clear All", L"Limpar tudo", L"Limpiar todo", L"Очистить все", L"Tümünü temizle", L"全部清除", L"सभी साफ़ करें", L"すべてクリア", L"مسح الكل");
    g_sidebar_btns[4].text = trw9(L"Detect Duplicates", L"Detectar duplicadas", L"Detectar duplicados", L"Найти дубликаты", L"Yinelenenleri bul", L"查找重复项", L"डुप्लिकेट खोजें", L"重複を検出", L"كشف التكرارات");
    g_sidebar_btns[5].text = trw9(L"Migrate Dups", L"Migrar duplicadas", L"Migrar duplicados", L"Перенести дубликаты", L"Yinelenenleri taşı", L"迁移重复项", L"डुप्लिकेट स्थानांतरण", L"重複を移行", L"نقل التكرارات");
    g_sidebar_btns[6].text = trw9(L"Smart Optimize", L"Otimização inteligente", L"Optimización inteligente", L"Умная оптимизация", L"Akıllı optimize", L"智能优化", L"स्मार्ट अनुकूलन", L"スマート最適化", L"تحسين ذكي");
    g_sidebar_btns[7].text = trw9(L"Fast Recompress", L"Recompressão rápida", L"Recompresión rápida", L"Быстрое сжатие", L"Hızlı sıkıştır", L"快速重新压缩", L"तेज़ पुनःसंपीड़न", L"高速再圧縮", L"إعادة ضغط سريعة");
    g_sidebar_btns[8].text = trw(L"Recompress Same Format", L"Recomprimir mesmo formato",
        L"Recomprimir mismo formato", L"Сжать в том же формате");
    g_sidebar_btns[7].text = L"Recompress";
    g_sidebar_btns[9].text = g_app.use_gpu_encoding
        ? trw(L"Encoder: GPU", L"Encoder: GPU", L"Codificador: GPU", L"Кодировщик: GPU")
        : trw(L"Encoder: CPU", L"Encoder: CPU", L"Codificador: CPU", L"Кодировщик: CPU");
    if (g_language == UI_LANG_BENGALI) {
        g_sidebar_btns[0].text = L"ফাইল যোগ করুন"; g_sidebar_btns[1].text = L"ফোল্ডার যোগ করুন";
        g_sidebar_btns[2].text = L"সব সংরক্ষণ"; g_sidebar_btns[3].text = L"সব মুছুন";
    } else if (g_language == UI_LANG_FRENCH) {
        g_sidebar_btns[0].text = L"Ajouter fichier"; g_sidebar_btns[1].text = L"Ajouter dossier";
        g_sidebar_btns[2].text = L"Tout enregistrer"; g_sidebar_btns[3].text = L"Tout effacer";
    } else if (g_language == UI_LANG_GERMAN) {
        g_sidebar_btns[0].text = L"Datei hinzufügen"; g_sidebar_btns[1].text = L"Ordner hinzufügen";
        g_sidebar_btns[2].text = L"Alles speichern"; g_sidebar_btns[3].text = L"Alles löschen";
    } else if (g_language == UI_LANG_INDONESIAN) {
        g_sidebar_btns[0].text = L"Tambah file"; g_sidebar_btns[1].text = L"Tambah folder";
        g_sidebar_btns[2].text = L"Simpan semua"; g_sidebar_btns[3].text = L"Hapus semua";
    } else if (g_language == UI_LANG_KOREAN) {
        g_sidebar_btns[0].text = L"파일 추가"; g_sidebar_btns[1].text = L"폴더 추가";
        g_sidebar_btns[2].text = L"모두 저장"; g_sidebar_btns[3].text = L"모두 지우기";
    } else if (g_language == UI_LANG_ITALIAN) {
        g_sidebar_btns[0].text = L"Aggiungi file"; g_sidebar_btns[1].text = L"Aggiungi cartella";
        g_sidebar_btns[2].text = L"Salva tutto"; g_sidebar_btns[3].text = L"Cancella tutto";
    }
    g_sidebar_btns[10].text = language_button_text();
    switch (g_sort_mode) {
        case SORT_BY_TYPE: g_sidebar_btns[11].text = trw8(L"Sort: Type", L"Ordenar: Tipo", L"Ordenar: Tipo", L"Сорт.: Тип", L"Sırala: Tür", L"排序: 类型", L"क्रम: प्रकार", L"並べ替え: 種類"); break;
        case SORT_BY_SIZE: g_sidebar_btns[11].text = trw8(L"Sort: Size", L"Ordenar: Tamanho", L"Ordenar: Tamaño", L"Сорт.: Размер", L"Sırala: Boyut", L"排序: 大小", L"क्रम: आकार", L"並べ替え: サイズ"); break;
        case SORT_BY_TEXTURE_COUNT: g_sidebar_btns[11].text = trw8(L"Sort: Textures", L"Ordenar: Texturas", L"Ordenar: Texturas", L"Сорт.: Текстуры", L"Sırala: Dokular", L"排序: 纹理数", L"क्रम: टेक्सचर", L"並べ替え: テクスチャ"); break;
        case SORT_BY_RESOLUTION: g_sidebar_btns[11].text = trw8(L"Sort: Resolution", L"Ordenar: Resolucao", L"Ordenar: Resolucion", L"Sort: Resolution", L"Sort: Resolution", L"Sort: Resolution", L"Sort: Resolution", L"Sort: Resolution"); break;
        case SORT_BY_MIPMAPS: g_sidebar_btns[11].text = trw8(L"Sort: Mipmaps", L"Ordenar: Mipmaps", L"Ordenar: Mipmaps", L"Sort: Mipmaps", L"Sort: Mipmaps", L"Sort: Mipmaps", L"Sort: Mipmaps", L"Sort: Mipmaps"); break;
        case SORT_BY_COMPRESSION: g_sidebar_btns[11].text = trw8(L"Sort: Compression", L"Ordenar: Compressao", L"Ordenar: Compresion", L"Sort: Compression", L"Sort: Compression", L"Sort: Compression", L"Sort: Compression", L"Sort: Compression"); break;
        case SORT_BY_MODIFIED: g_sidebar_btns[11].text = trw8(L"Sort: Modified", L"Ordenar: Modificados", L"Ordenar: Modificados", L"Сорт.: Изменены", L"Sırala: Değişen", L"排序: 已修改", L"क्रम: संशोधित", L"並べ替え: 更新"); break;
        default: g_sidebar_btns[11].text = trw8(L"Sort: Name", L"Ordenar: Nome", L"Ordenar: Nombre", L"Сорт.: Имя", L"Sırala: Ad", L"排序: 名称", L"क्रम: नाम", L"並べ替え: 名前"); break;
    }
    {
        const wchar_t *gname =
            (g_grid_index == 0) ? trw8(L"Grid: Small", L"Grade: Pequena", L"Cuadrícula: Pequeña", L"Сетка: Малая", L"Izgara: Küçük", L"网格: 小", L"ग्रिड: छोटा", L"グリッド: 小") :
            (g_grid_index == 2) ? trw8(L"Grid: Native", L"Grade: Nativa", L"Cuadrícula: Nativa", L"Сетка: Полная", L"Izgara: Yerel", L"网格: 原生", L"ग्रिड: मूल", L"グリッド: ネイティブ") :
                                  trw8(L"Grid: Medium", L"Grade: Média", L"Cuadrícula: Media", L"Сетка: Средняя", L"Izgara: Orta", L"网格: 中", L"ग्रिड: मध्यम", L"グリッド: 中");
        g_sidebar_btns[12].text = gname;
    }
}

/* ── Init ──────────────────────────────────────────────────────────── */

void gui_init(HINSTANCE hInst) {
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX icex = { sizeof(icex), ICC_STANDARD_CLASSES | ICC_BAR_CLASSES };
    InitCommonControlsEx(&icex);
    theme_init();
    update_sidebar_labels();

    WNDCLASSEXW wc = {sizeof(wc)};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(CLR_BG_DARK);
    wc.lpszClassName = L"EasyOptimizerMain";
    RegisterClassExW(&wc);

    WNDCLASSEXW wc2 = {sizeof(wc2)};
    wc2.style = CS_HREDRAW | CS_VREDRAW;
    wc2.lpfnWndProc = ContentWndProc;
    wc2.hInstance = hInst;
    wc2.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc2.hbrBackground = CreateSolidBrush(CLR_BG_DARK);
    wc2.lpszClassName = L"EasyOptimizerContent";
    RegisterClassExW(&wc2);

    WNDCLASSEXW wc3 = {sizeof(wc3)};
    wc3.style = CS_HREDRAW | CS_VREDRAW;
    wc3.lpfnWndProc = SidebarWndProc;
    wc3.hInstance = hInst;
    wc3.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc3.hbrBackground = CreateSolidBrush(CLR_SURFACE_DARK);
    wc3.lpszClassName = L"EasyOptimizerSidebar";
    RegisterClassExW(&wc3);

    g_app.hwnd_main = CreateWindowExW(0, L"EasyOptimizerMain", L"EasyOptimizer-V by LN-Development",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1100, 768,
        NULL, NULL, hInst, NULL);

    g_app.hwnd_header = CreateWindowExW(0, L"STATIC", NULL,
        WS_CHILD | WS_VISIBLE | SS_OWNERDRAW, 0, 0, 100, HEADER_HEIGHT,
        g_app.hwnd_main, NULL, hInst, NULL);

    g_app.hwnd_sidebar = CreateWindowExW(0, L"EasyOptimizerSidebar", NULL,
        WS_CHILD | WS_VISIBLE, 0, HEADER_HEIGHT, SIDEBAR_WIDTH, 500,
        g_app.hwnd_main, NULL, hInst, NULL);

    g_app.hwnd_content = CreateWindowExW(0, L"EasyOptimizerContent", NULL,
        WS_CHILD | WS_VISIBLE | WS_VSCROLL, SIDEBAR_WIDTH, HEADER_HEIGHT, 600, 500,
        g_app.hwnd_main, NULL, hInst, NULL);

    /* Parent the search box to the main window (not the header STATIC) so its
     * EN_CHANGE notifications reach MainWndProc's WM_COMMAND handler. Positioned
     * over the header area by layout_children; created last so it stays on top. */
    g_app.hwnd_search = CreateWindowExW(0, L"EDIT", NULL,
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        0, 0, 300, 28, g_app.hwnd_main, (HMENU)ID_SEARCH_BOX, hInst, NULL);
    SendMessageW(g_app.hwnd_search, WM_SETFONT, (WPARAM)theme_font_display(), TRUE);
    SetWindowTextW(g_app.hwnd_search, L"");

    g_app.hwnd_status = CreateWindowExW(0, STATUSCLASSNAMEW, L"Ready",
        WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, 0, 0, 0, 0,
        g_app.hwnd_main, NULL, hInst, NULL);

    layout_children();
    gui_update_status("Ready - No files loaded");
    log_ng_keys_state();

    ShowWindow(g_app.hwnd_main, SW_SHOW);
    UpdateWindow(g_app.hwnd_main);

    DragAcceptFiles(g_app.hwnd_main, TRUE);
}

void gui_run(void) {
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

/* ── Layout ────────────────────────────────────────────────────────── */

static void layout_children(void) {
    RECT rc;
    GetClientRect(g_app.hwnd_main, &rc);
    int w = rc.right, h = rc.bottom;

    SendMessageW(g_app.hwnd_status, WM_SIZE, 0, 0);
    RECT status_rc;
    GetWindowRect(g_app.hwnd_status, &status_rc);
    int sh = status_rc.bottom - status_rc.top;

    MoveWindow(g_app.hwnd_header, 0, 0, w, HEADER_HEIGHT, TRUE);
    MoveWindow(g_app.hwnd_sidebar, 0, HEADER_HEIGHT, SIDEBAR_WIDTH, h - HEADER_HEIGHT - sh, TRUE);
    MoveWindow(g_app.hwnd_content, SIDEBAR_WIDTH, HEADER_HEIGHT, w - SIDEBAR_WIDTH, h - HEADER_HEIGHT - sh, TRUE);

    /* Position search box inside header, aligned with content area */
    int search_x = SIDEBAR_WIDTH + 12;
    int search_y = (HEADER_HEIGHT - 28) / 2;
    MoveWindow(g_app.hwnd_search, search_x, search_y, w - search_x - 16, 28, TRUE);

    /* Update sidebar button rects (skip hidden buttons so the rest pack up) */
    int by = 16;
    for (int i = 0; i < (int)SIDEBAR_BTN_COUNT; i++) {
        if (!g_sidebar_btns[i].visible) {
            SetRectEmpty(&g_sidebar_btns[i].rc);
            continue;
        }
        g_sidebar_btns[i].rc.left = 12;
        g_sidebar_btns[i].rc.top = by;
        g_sidebar_btns[i].rc.right = SIDEBAR_WIDTH - 12;
        g_sidebar_btns[i].rc.bottom = by + 36;
        by += 44;
    }
}

/* ── Status ────────────────────────────────────────────────────────── */

void gui_update_status(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(g_app.status_text, sizeof(g_app.status_text), fmt, args);
    va_end(args);

    LOG("%s", g_app.status_text);

    wchar_t wbuf[512];
    MultiByteToWideChar(CP_UTF8, 0, g_app.status_text, -1, wbuf, 512);
    SendMessageW(g_app.hwnd_status, SB_SETTEXTW, 0, (LPARAM)wbuf);
}

/* ── File open dialog ──────────────────────────────────────────────── */

static INT_PTR CALLBACK ImportTypesDlgProc(HWND hDlg, UINT msg, WPARAM wp, LPARAM lp) {
    ImportFilter *filter = (ImportFilter *)GetWindowLongPtrW(hDlg, DWLP_USER);
    switch (msg) {
    case WM_INITDIALOG:
        SetWindowLongPtrW(hDlg, DWLP_USER, lp);
        filter = (ImportFilter *)lp;
        CheckDlgButton(hDlg, IDC_IMPORT_YTD, filter->ytd ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_IMPORT_YFT, filter->yft ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_IMPORT_YDD, filter->ydd ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_IMPORT_YDR, filter->ydr ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_IMPORT_WTD, filter->wtd ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_IMPORT_RPF, filter->rpf ? BST_CHECKED : BST_UNCHECKED);
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK) {
            filter->ytd = IsDlgButtonChecked(hDlg, IDC_IMPORT_YTD) == BST_CHECKED;
            filter->yft = IsDlgButtonChecked(hDlg, IDC_IMPORT_YFT) == BST_CHECKED;
            filter->ydd = IsDlgButtonChecked(hDlg, IDC_IMPORT_YDD) == BST_CHECKED;
            filter->ydr = IsDlgButtonChecked(hDlg, IDC_IMPORT_YDR) == BST_CHECKED;
            filter->wtd = IsDlgButtonChecked(hDlg, IDC_IMPORT_WTD) == BST_CHECKED;
            filter->rpf = IsDlgButtonChecked(hDlg, IDC_IMPORT_RPF) == BST_CHECKED;
            EndDialog(hDlg, IDOK);
            return TRUE;
        }
        if (LOWORD(wp) == IDCANCEL) { EndDialog(hDlg, IDCANCEL); return TRUE; }
        break;
    case WM_CLOSE:
        EndDialog(hDlg, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}

static LPDLGTEMPLATE build_import_types_template(void) {
    uint8_t *buf = (uint8_t *)calloc(1, 4096);
    uint8_t *p = buf;
    if (!buf) return NULL;

    DLGTEMPLATE *dt = (DLGTEMPLATE *)p;
    dt->style = DS_MODALFRAME | DS_CENTER | WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE;
    dt->dwExtendedStyle = 0;
    dt->cdit = 8;
    dt->x = 0; dt->y = 0; dt->cx = 180; dt->cy = 118;
    p += sizeof(DLGTEMPLATE);
    *(WORD *)p = 0; p += 2;
    *(WORD *)p = 0; p += 2;
    const wchar_t *title = L"Import file types";
    size_t tlen = (wcslen(title) + 1) * 2;
    memcpy(p, title, tlen); p += tlen;

    #define ADD_IMPORT_ITEM(style_, x_, y_, cx_, cy_, id_, cls_atom_, text_) do { \
        p = (uint8_t *)(((uintptr_t)p + 3) & ~3); \
        DLGITEMTEMPLATE *it = (DLGITEMTEMPLATE *)p; \
        it->style = (style_) | WS_CHILD | WS_VISIBLE; \
        it->dwExtendedStyle = 0; \
        it->x = x_; it->y = y_; it->cx = cx_; it->cy = cy_; it->id = id_; \
        p += sizeof(DLGITEMTEMPLATE); \
        *(WORD *)p = 0xFFFF; p += 2; *(WORD *)p = cls_atom_; p += 2; \
        size_t slen = (wcslen(text_) + 1) * 2; \
        memcpy(p, text_, slen); p += slen; *(WORD *)p = 0; p += 2; \
    } while (0)

    ADD_IMPORT_ITEM(BS_AUTOCHECKBOX | WS_TABSTOP, 12, 12, 70, 12, IDC_IMPORT_YTD, 0x0080, L"YTD");
    ADD_IMPORT_ITEM(BS_AUTOCHECKBOX | WS_TABSTOP, 92, 12, 70, 12, IDC_IMPORT_WTD, 0x0080, L"WTD");
    ADD_IMPORT_ITEM(BS_AUTOCHECKBOX | WS_TABSTOP, 12, 34, 70, 12, IDC_IMPORT_YFT, 0x0080, L"YFT");
    ADD_IMPORT_ITEM(BS_AUTOCHECKBOX | WS_TABSTOP, 92, 34, 70, 12, IDC_IMPORT_YDD, 0x0080, L"YDD");
    ADD_IMPORT_ITEM(BS_AUTOCHECKBOX | WS_TABSTOP, 12, 56, 70, 12, IDC_IMPORT_YDR, 0x0080, L"YDR");
    ADD_IMPORT_ITEM(BS_AUTOCHECKBOX | WS_TABSTOP, 92, 56, 70, 12, IDC_IMPORT_RPF, 0x0080, L"RPF");
    ADD_IMPORT_ITEM(BS_DEFPUSHBUTTON | WS_TABSTOP, 32, 88, 52, 16, IDOK, 0x0080, L"Continue");
    ADD_IMPORT_ITEM(BS_PUSHBUTTON | WS_TABSTOP, 96, 88, 52, 16, IDCANCEL, 0x0080, L"Cancel");

    #undef ADD_IMPORT_ITEM
    return (LPDLGTEMPLATE)buf;
}

static bool select_import_types(HWND parent) {
    LPDLGTEMPLATE tpl = build_import_types_template();
    if (!tpl) return false;
    ImportFilter selected = g_import_filter;
    INT_PTR result = DialogBoxIndirectParamW(GetModuleHandleW(NULL), tpl, parent,
        ImportTypesDlgProc, (LPARAM)&selected);
    free(tpl);
    if (result != IDOK) return false;
    g_import_filter = selected;
    return true;
}

static void open_file_dialog(HWND parent) {
    if (!select_import_types(parent)) return;

    IFileOpenDialog *pfd = NULL;
    HRESULT hr = CoCreateInstance(&CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER,
                                  &IID_IFileOpenDialog, (void **)&pfd);
    if (FAILED(hr) || !pfd) {
        gui_update_status("Failed to open file picker");
        return;
    }

    COMDLG_FILTERSPEC filters[] = {
        {L"GTA texture files", L"*.ytd;*.wtd;*.ydr;*.yft;*.ydd;*.rpf"},
        {L"YTD files", L"*.ytd"},
        {L"WTD files", L"*.wtd"},
        {L"Model files", L"*.ydr;*.yft;*.ydd"},
        {L"RPF archives", L"*.rpf"},
        {L"All files", L"*.*"},
    };
    pfd->lpVtbl->SetFileTypes(pfd, ARRAYSIZE(filters), filters);
    pfd->lpVtbl->SetFileTypeIndex(pfd, 1);

    DWORD opts = 0;
    pfd->lpVtbl->GetOptions(pfd, &opts);
    pfd->lpVtbl->SetOptions(pfd, opts | FOS_ALLOWMULTISELECT | FOS_FILEMUSTEXIST | FOS_FORCEFILESYSTEM);
    pfd->lpVtbl->SetTitle(pfd, trw(L"Select texture files", L"Selecione arquivos de textura",
        L"Seleccione archivos de textura", L"Выберите файлы текстур"));

    hr = pfd->lpVtbl->Show(pfd, parent);
    if (SUCCEEDED(hr)) {
        IShellItemArray *items = NULL;
        hr = pfd->lpVtbl->GetResults(pfd, &items);
        if (SUCCEEDED(hr) && items) {
            DWORD count = 0;
            items->lpVtbl->GetCount(items, &count);
            bool prev_bulk = g_bulk_add;
            if (count > 1) g_bulk_add = true;   /* multi-select: collapse */
            for (DWORD i = 0; i < count; i++) {
                IShellItem *item = NULL;
                if (SUCCEEDED(items->lpVtbl->GetItemAt(items, i, &item)) && item) {
                    wchar_t *path = NULL;
                    if (SUCCEEDED(item->lpVtbl->GetDisplayName(item, SIGDN_FILESYSPATH, &path)) && path) {
                        gui_add_ytd(path);
                        CoTaskMemFree(path);
                    }
                    item->lpVtbl->Release(item);
                }
            }
            g_bulk_add = prev_bulk;
            items->lpVtbl->Release(items);
        }
    }
    pfd->lpVtbl->Release(pfd);
}
static void scan_folder_recursive(const wchar_t *dir) {
    wchar_t pattern[MAX_PATH];
    _snwprintf(pattern, MAX_PATH, L"%s\\*", dir);
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(pattern, &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.cFileName[0] == L'.') continue;
        wchar_t full[MAX_PATH];
        _snwprintf(full, MAX_PATH, L"%s\\%s", dir, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            scan_folder_recursive(full);
        } else {
            if (is_supported_archive_path(fd.cFileName) && should_import_archive_path(fd.cFileName)) {
                int before = g_app.ytd_count;
                g_scan_candidates++;
                gui_add_ytd(full);
                if (g_app.ytd_count == before) g_scan_failed++;
            }
        }
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);
}

static void open_folder_dialog(HWND parent) {
    if (!select_import_types(parent)) return;

    IFileOpenDialog *pfd = NULL;
    HRESULT hr = CoCreateInstance(&CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER,
                                  &IID_IFileOpenDialog, (void **)&pfd);
    if (FAILED(hr) || !pfd) return;

    DWORD opts = 0;
    pfd->lpVtbl->GetOptions(pfd, &opts);
    pfd->lpVtbl->SetOptions(pfd, opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    pfd->lpVtbl->SetTitle(pfd, trw(L"Select folder to scan for texture files",
        L"Selecione a pasta para buscar arquivos de textura",
        L"Seleccione la carpeta para buscar archivos de textura",
        L"Выберите папку для поиска файлов текстур"));

    hr = pfd->lpVtbl->Show(pfd, parent);
    if (SUCCEEDED(hr)) {
        IShellItem *psi = NULL;
        hr = pfd->lpVtbl->GetResult(pfd, &psi);
        if (SUCCEEDED(hr) && psi) {
            wchar_t *folder = NULL;
            hr = psi->lpVtbl->GetDisplayName(psi, SIGDN_FILESYSPATH, &folder);
            if (SUCCEEDED(hr) && folder) {
                int before = g_app.ytd_count;
                g_scan_candidates = 0;
                g_scan_failed = 0;
                bool prev_bulk = g_bulk_add;
                g_bulk_add = true;                 /* folder scan: collapse all */
                scan_folder_recursive(folder);
                g_bulk_add = prev_bulk;
                int added = g_app.ytd_count - before;
                gui_update_status("Folder scan: %d compatible, %d loaded, %d rejected",
                    g_scan_candidates, added, g_scan_failed);
                InvalidateRect(g_app.hwnd_content, NULL, TRUE);
                InvalidateRect(g_app.hwnd_sidebar, NULL, TRUE);
                CoTaskMemFree(folder);
            }
            psi->lpVtbl->Release(psi);
        }
    }
    pfd->lpVtbl->Release(pfd);
}



/* ── Add YTD ───────────────────────────────────────────────────────── */

static YtdFile *load_archive_path(const wchar_t *path) {
    const wchar_t *ext = PathFindExtensionW(path);
    if (_wcsicmp(ext, L".wtd") == 0) return wtd_load(path);
    if (_wcsicmp(ext, L".ydr") == 0 || _wcsicmp(ext, L".yft") == 0 ||
        _wcsicmp(ext, L".ydd") == 0)
        return ydr_load(path);
    return ytd_load(path);
}

typedef struct {
    const wchar_t *rpf_path;
    YtdFile *parent;
    int sequence;
    int discovered;
    int loaded;
    int unavailable;
} RpfImportContext;

static const wchar_t *embedded_file_name(const wchar_t *path) {
    const wchar_t *slash = wcsrchr(path, L'/');
    const wchar_t *backslash = wcsrchr(path, L'\\');
    const wchar_t *last = slash > backslash ? slash : backslash;
    return last ? last + 1 : path;
}

static bool import_rpf_entry(const wchar_t *entry_path, const uint8_t *data,
                             size_t data_size, void *opaque) {
    RpfImportContext *ctx = (RpfImportContext *)opaque;
    if (g_app.ytd_count >= MAX_LOADED_YTDS || !should_import_archive_path(entry_path))
        return false;

    wchar_t temp_dir[MAX_PATH], temp_path[MAX_PATH];
    if (!GetTempPathW(MAX_PATH, temp_dir)) return false;
    _snwprintf(temp_path, MAX_PATH, L"%sEasyOptimizer-rpf-%lu-%d%s",
        temp_dir, GetCurrentProcessId(), ctx->sequence++, PathFindExtensionW(entry_path));

    FILE *f = _wfopen(temp_path, L"wb");
    if (!f) return false;
    bool written = fwrite(data, 1, data_size, f) == data_size;
    fclose(f);
    if (!written) {
        DeleteFileW(temp_path);
        return false;
    }

    YtdFile *archive = load_archive_path(temp_path);
    DeleteFileW(temp_path);
    ctx->discovered++;
    if (!archive) {
        archive = (YtdFile *)calloc(1, sizeof(YtdFile));
        if (!archive) return false;
        ctx->unavailable++;
    } else {
        ctx->loaded++;
    }

    const wchar_t *leaf = embedded_file_name(entry_path);
    WideCharToMultiByte(CP_UTF8, 0, leaf, -1, archive->name, EO_MAX_NAME, NULL, NULL);
    wcsncpy(archive->file_path, leaf, EO_MAX_PATH - 1);
    archive->file_path[EO_MAX_PATH - 1] = 0;
    archive->type = ARCHIVE_MODEL_READONLY;
    archive->from_rpf = true;
    archive->rpf_parent = ctx->parent;
    archive->expanded = false;
    g_app.ytds[g_app.ytd_count++] = archive;
    LOG("RPF import: listed embedded %ls from %ls", entry_path, ctx->rpf_path);
    return true;
}

void gui_add_ytd(const wchar_t *path) {
    if (g_app.ytd_count >= MAX_LOADED_YTDS) {
        SET_LOAD_ERR("workspace full (limit %d entries reached)", MAX_LOADED_YTDS);
        LOG_ERR("gui_add_ytd: rejected '%ls': workspace full (%d/%d)",
                embedded_file_name(path), g_app.ytd_count, MAX_LOADED_YTDS);
        return;
    }
    if (!is_supported_archive_path(path)) {
        gui_update_status("Unsupported file type");
        return;
    }
    if (!should_import_archive_path(path)) return;

    const wchar_t *ext = wcsrchr(path, L'.');
    if (ext && _wcsicmp(ext, L".rpf") == 0) {
        YtdFile *group = (YtdFile *)calloc(1, sizeof(YtdFile));
        if (!group) {
            gui_update_status("Out of memory creating RPF group");
            return;
        }
        const wchar_t *leaf = embedded_file_name(path);
        WideCharToMultiByte(CP_UTF8, 0, leaf, -1, group->name, EO_MAX_NAME, NULL, NULL);
        wcsncpy(group->file_path, path, EO_MAX_PATH - 1);
        group->is_rpf_group = true;
        group->expanded = false;
        g_app.ytds[g_app.ytd_count++] = group;

        RpfImportContext context = {path, group, 0, 0, 0, 0};
        char error[256] = {0};
        int result = rpf_scan_file(path, import_rpf_entry, &context, error, sizeof(error));
        if (result < 0) {
            bool missing_ng_keys = (strstr(error, "ng.dat") != NULL) ||
                                   (strstr(error, "NG-encrypted") != NULL);
            for (int i = g_app.ytd_count - 1; i >= 0; i--) {
                if (g_app.ytds[i]->rpf_parent != group) continue;
                ytd_free(g_app.ytds[i]);
                for (int j = i; j < g_app.ytd_count - 1; j++)
                    g_app.ytds[j] = g_app.ytds[j + 1];
                g_app.ytd_count--;
            }
            for (int i = 0; i < g_app.ytd_count; i++) {
                if (g_app.ytds[i] != group) continue;
                for (int j = i; j < g_app.ytd_count - 1; j++)
                    g_app.ytds[j] = g_app.ytds[j + 1];
                g_app.ytd_count--;
                break;
            }
            ytd_free(group);
            LOG_ERR("RPF scan failed: %s", error);
            gui_update_status("RPF scan failed: %s", error);
            if (missing_ng_keys) {
                if (ensure_ng_keys_for_rpf()) {
                    gui_update_status("NG keys ready; retrying RPF scan");
                    gui_add_ytd(path);
                }
                return;
            }
            return;
        }
        group->rpf_child_count = context.discovered;
        gui_update_status("RPF scan: %d files listed, %d opened, %d unavailable; expand '%s'",
            context.discovered, context.loaded, context.unavailable, group->name);
        InvalidateRect(g_app.hwnd_content, NULL, TRUE);
        InvalidateRect(g_app.hwnd_sidebar, NULL, TRUE);
        return;
    }

    LOG("gui_add_ytd: adding file");
    g_load_error[0] = 0;
    YtdFile *archive = load_archive_path(path);
    if (!archive) {
        const wchar_t *leaf = embedded_file_name(path);
        const char *reason = g_load_error[0] ? g_load_error : "unknown load error";
        LOG_ERR("gui_add_ytd: rejected '%ls': %s", leaf, reason);
        gui_update_status("Rejected '%ls': %s", leaf, reason);
        return;
    }

    /* Auto-expand only a lone file; bulk loads (folders / multi-select) stay
     * collapsed so the user gets a browsable list instead of one giant archive. */
    archive->expanded = (!g_bulk_add && g_app.ytd_count == 0);
    g_app.ytds[g_app.ytd_count++] = archive;
    apply_archive_sort();
    gui_update_status("Loaded %s - %d textures", archive->name, archive->texture_count);
    InvalidateRect(g_app.hwnd_content, NULL, TRUE);
    InvalidateRect(g_app.hwnd_sidebar, NULL, TRUE);
}

/* ── Save all ──────────────────────────────────────────────────────── */

static bool select_folder(HWND parent, const wchar_t *title, wchar_t *out_path, size_t out_count) {
    IFileOpenDialog *pfd = NULL;
    HRESULT hr = CoCreateInstance(&CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER,
                                  &IID_IFileOpenDialog, (void **)&pfd);
    if (FAILED(hr) || !pfd) return false;

    DWORD opts = 0;
    pfd->lpVtbl->GetOptions(pfd, &opts);
    pfd->lpVtbl->SetOptions(pfd, opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    pfd->lpVtbl->SetTitle(pfd, title);

    bool selected = false;
    hr = pfd->lpVtbl->Show(pfd, parent);
    if (SUCCEEDED(hr)) {
        IShellItem *psi = NULL;
        hr = pfd->lpVtbl->GetResult(pfd, &psi);
        if (SUCCEEDED(hr) && psi) {
            wchar_t *folder = NULL;
            hr = psi->lpVtbl->GetDisplayName(psi, SIGDN_FILESYSPATH, &folder);
            if (SUCCEEDED(hr) && folder) {
                wcsncpy(out_path, folder, out_count - 1);
                out_path[out_count - 1] = 0;
                CoTaskMemFree(folder);
                selected = true;
            }
            psi->lpVtbl->Release(psi);
        }
    }
    pfd->lpVtbl->Release(pfd);
    return selected;
}

static void select_language(void) {
    g_language = (AppLanguage)((g_language + 1) % 15);
    update_sidebar_labels();
    InvalidateRect(g_app.hwnd_sidebar, NULL, TRUE);
    InvalidateRect(g_app.hwnd_content, NULL, TRUE);
}

/* ── Generate ng.dat / lut.dat from the user's GTA5.exe ────────────────── */

static void keygen_status_cb(const wchar_t *status, int percent, void *ctx) {
    (void)ctx;
    char buf[256];
    WideCharToMultiByte(CP_UTF8, 0, status, -1, buf, sizeof(buf), NULL, NULL);
    gui_update_status("Key generation: %d%% - %s", percent, buf);
    /* Pump paint messages so the status bar updates during the blocking scan. */
    SetWindowTextW(g_app.hwnd_status, NULL);  /* force statusbar refresh below */
    wchar_t wbuf[300];
    _snwprintf(wbuf, 300, L"Key generation %d%% - %s", percent, status);
    SendMessageW(g_app.hwnd_status, SB_SETTEXTW, 0, (LPARAM)wbuf);
    MSG msg;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) { PostQuitMessage(0); break; }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

static bool app_dir_file_path(const wchar_t *name, wchar_t *out, size_t out_count) {
    if (!GetModuleFileNameW(NULL, out, (DWORD)out_count)) return false;
    wchar_t *slash = wcsrchr(out, L'\\');
    if (!slash) return false;
    slash[1] = 0;
    wcsncat(out, name, out_count - wcslen(out) - 1);
    out[out_count - 1] = 0;
    return true;
}

static bool file_size_matches(const wchar_t *path, LONGLONG expected_size) {
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &data)) return false;
    LARGE_INTEGER size;
    size.HighPart = data.nFileSizeHigh;
    size.LowPart = data.nFileSizeLow;
    return size.QuadPart == expected_size;
}

static bool ng_keys_available(void) {
    wchar_t ng_path[MAX_PATH], lut_path[MAX_PATH];
    return app_dir_file_path(L"ng.dat", ng_path, MAX_PATH) &&
           app_dir_file_path(L"lut.dat", lut_path, MAX_PATH) &&
           file_size_matches(ng_path, 306000) &&
           file_size_matches(lut_path, 256);
}

static void log_ng_keys_state(void) {
    if (ng_keys_available())
        LOG("GTA keys: available (ng.dat/lut.dat valid)");
    else
        LOG("GTA keys: not available (will auto-acquire when encrypted RPF needs them)");
}

/* Pick the user's gta5.exe and derive ng.dat/lut.dat next to our executable. */
static bool ensure_ng_keys_for_rpf(void) {
    if (ng_keys_available()) {
        LOG("GTA keys: ng.dat/lut.dat already available");
        return true;
    }
    LOG("GTA keys: ng.dat/lut.dat missing; automatic acquisition required");
    int go = MessageBoxW(g_app.hwnd_main,
        trw(L"This will scan your GTA5.exe and generate the NG decryption keys "
            L"(ng.dat / lut.dat) needed to read encrypted vanilla RPF archives.\n\n"
            L"No key data is distributed — keys are derived from your own game copy.\n"
            L"The scan can take up to a minute and the window may appear busy.\n\nContinue?",
            L"Isto vai escanear seu GTA5.exe e gerar as chaves NG (ng.dat / lut.dat) "
            L"necessárias para ler RPFs vanilla criptografados.\n\nNenhuma chave é distribuída — "
            L"elas são derivadas da sua própria cópia do jogo.\nA varredura pode levar até um "
            L"minuto e a janela pode parecer ocupada.\n\nContinuar?",
            L"Esto escaneará tu GTA5.exe y generará las claves NG (ng.dat / lut.dat).\n\n¿Continuar?",
            L"Это просканирует ваш GTA5.exe и создаст ключи NG (ng.dat / lut.dat).\n\nПродолжить?"),
        L"Generate GTA Keys", MB_OKCANCEL | MB_ICONINFORMATION);
    if (go != IDOK) {
        LOG("GTA keys: acquisition cancelled by user");
        return false;
    }

    /* File picker limited to executables, defaulting to gta5.exe. */
    wchar_t exe_path[MAX_PATH] = L"gta5.exe";
    OPENFILENAMEW ofn = {sizeof(ofn)};
    ofn.hwndOwner = g_app.hwnd_main;
    ofn.lpstrFilter = L"GTA executable\0gta5.exe;gta5_enhanced.exe;*.exe\0All files\0*.*\0";
    ofn.lpstrFile = exe_path;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"Select your GTA5.exe";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (!GetOpenFileNameW(&ofn)) {
        LOG("GTA keys: acquisition cancelled before selecting GTA5.exe");
        return false;
    }

    /* Output directory = this application's own folder (where rpf_scan reads keys). */
    wchar_t app_dir[MAX_PATH];
    GetModuleFileNameW(NULL, app_dir, MAX_PATH);
    wchar_t *slash = wcsrchr(app_dir, L'\\');
    if (slash) *slash = 0;

    HCURSOR old_cursor = SetCursor(LoadCursor(NULL, IDC_WAIT));
    char err[256] = {0};
    LOG("GTA keys: scanning selected GTA executable");
    bool ok = keygen_from_exe(exe_path, app_dir, keygen_status_cb, NULL, err, sizeof(err));
    SetCursor(old_cursor);

    if (ok) {
        MessageBoxW(g_app.hwnd_main,
            trw(L"Keys generated successfully. Encrypted vanilla RPF archives can now be opened.",
                L"Chaves geradas com sucesso. RPFs vanilla criptografados agora podem ser abertos.",
                L"Claves generadas con éxito.", L"Ключи успешно созданы."),
            L"Generate GTA Keys", MB_OK | MB_ICONINFORMATION);
        gui_update_status("ng.dat / lut.dat generated from GTA5.exe");
        LOG("GTA keys: acquired successfully (ng.dat/lut.dat generated)");
        return true;
    } else {
        wchar_t wmsg[400], werr[256];
        MultiByteToWideChar(CP_UTF8, 0, err, -1, werr, 256);
        _snwprintf(wmsg, 400, L"Key generation failed:\n\n%s", werr);
        MessageBoxW(g_app.hwnd_main, wmsg, L"Generate GTA Keys", MB_OK | MB_ICONERROR);
        gui_update_status("Key generation failed: %s", err);
        LOG_ERR("GTA keys: acquisition failed: %s", err);
        return false;
    }
}

static bool save_archive_to_path(YtdFile *archive, const wchar_t *path) {
    if (archive->type == ARCHIVE_WTD) return wtd_save(archive, path);
    return ytd_save(archive, path);
}

static int save_archives_to_folder(const wchar_t *folder, int *out_skipped) {
    int saved = 0;
    int skipped = 0;
    for (int i = 0; i < g_app.ytd_count; i++) {
        YtdFile *archive = g_app.ytds[i];
        if (archive->is_rpf_group) continue;
        if (archive->is_preview) continue;   /* uncommitted migration preview */
        const wchar_t *filename = PathFindFileNameW(archive->file_path);
        wchar_t output[MAX_PATH];
        _snwprintf(output, MAX_PATH, L"%s\\%s", folder, filename);
        output[MAX_PATH - 1] = 0;
        if (archive->type == ARCHIVE_MODEL_READONLY)
            PathRenameExtensionW(output, L".ytd");
        if (PathFileExistsW(output)) {
            wchar_t base[MAX_PATH];
            wchar_t extension[32];
            wcsncpy(base, output, MAX_PATH - 1);
            base[MAX_PATH - 1] = 0;
            wcsncpy(extension, PathFindExtensionW(output), 31);
            extension[31] = 0;
            PathRemoveExtensionW(base);
            for (int suffix = 2; suffix < 10000; suffix++) {
                _snwprintf(output, MAX_PATH, L"%s-%d%s", base, suffix, extension);
                output[MAX_PATH - 1] = 0;
                if (!PathFileExistsW(output)) break;
            }
        }
        if (save_archive_to_path(archive, output)) saved++;
        else skipped++;
    }
    if (out_skipped) *out_skipped = skipped;
    return saved;
}

static bool create_project_snapshot(wchar_t *out_folder, size_t out_count, int *out_saved, int *out_skipped) {
    wchar_t exe_path[MAX_PATH];
    if (!GetModuleFileNameW(NULL, exe_path, MAX_PATH)) return false;
    PathRemoveFileSpecW(exe_path);

    wchar_t projects[MAX_PATH];
    _snwprintf(projects, MAX_PATH, L"%s\\projects", exe_path);
    projects[MAX_PATH - 1] = 0;
    CreateDirectoryW(projects, NULL);

    SYSTEMTIME st;
    GetLocalTime(&st);
    _snwprintf(out_folder, out_count, L"%s\\project-%04d%02d%02d-%02d%02d%02d-%03d",
               projects, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    out_folder[out_count - 1] = 0;
    if (!CreateDirectoryW(out_folder, NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
        return false;

    *out_saved = save_archives_to_folder(out_folder, out_skipped);
    return true;
}

static void save_project_cache(void) {
    if (g_app.ytd_count == 0) {
        gui_update_status("No files loaded");
        return;
    }
    wchar_t folder[MAX_PATH];
    int saved = 0, skipped = 0;
    if (!create_project_snapshot(folder, MAX_PATH, &saved, &skipped)) {
        gui_update_status("Failed to create project cache");
        return;
    }
    gui_update_status("Project cache saved: %d files (%d skipped)", saved, skipped);
    MessageBoxW(g_app.hwnd_main, folder,
        trw(L"Project cache saved", L"Cache do projeto salvo",
            L"Caché del proyecto guardada", L"Кэш проекта сохранен"),
        MB_OK | MB_ICONINFORMATION);
}

static void save_all(void) {
    if (g_app.ytd_count == 0) {
        gui_update_status("No files loaded");
        return;
    }

    const TASKDIALOG_BUTTON buttons[] = {
        {ID_SAVE_REPLACE_ORIGINALS, trw(L"Replace original YTD/WTD files",
            L"Substituir arquivos YTD/WTD originais", L"Reemplazar archivos YTD/WTD originales",
            L"Заменить исходные файлы YTD/WTD")},
        {ID_SAVE_TO_FOLDER, trw(L"Save copies to another folder",
            L"Salvar cópias em outra pasta", L"Guardar copias en otra carpeta",
            L"Сохранить копии в другую папку")},
        {ID_SAVE_PROJECT_CACHE, trw(L"Save only to versioned project cache",
            L"Salvar somente no cache versionado do projeto", L"Guardar solo en la caché versionada del proyecto",
            L"Сохранить только в версионный кэш проекта")},
    };
    TASKDIALOGCONFIG dialog = {sizeof(dialog)};
    dialog.hwndParent = g_app.hwnd_main;
    dialog.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION;
    dialog.pszWindowTitle = trw(L"Save All", L"Salvar tudo", L"Guardar todo", L"Сохранить все");
    dialog.pszMainInstruction = trw(L"Choose how to save the modified files",
        L"Escolha como salvar os arquivos modificados", L"Elija cómo guardar los archivos modificados",
        L"Выберите способ сохранения измененных файлов");
    dialog.pszContent = trw(L"Project cache snapshots are stored next to the executable.",
        L"Snapshots do cache ficam ao lado do executável.", L"Las instantáneas de caché se guardan junto al ejecutable.",
        L"Снимки кэша проекта хранятся рядом с исполняемым файлом.");
    dialog.cButtons = ARRAYSIZE(buttons);
    dialog.pButtons = buttons;
    dialog.nDefaultButton = ID_SAVE_PROJECT_CACHE;

    int choice = IDCANCEL;
    if (FAILED(TaskDialogIndirect(&dialog, &choice, NULL, NULL)) || choice == IDCANCEL)
        return;
    if (choice == ID_SAVE_PROJECT_CACHE) {
        save_project_cache();
        return;
    }

    wchar_t cache_folder[MAX_PATH];
    int cache_saved = 0, cache_skipped = 0;
    if (!create_project_snapshot(cache_folder, MAX_PATH, &cache_saved, &cache_skipped)) {
        gui_update_status("Failed to create project cache; save cancelled");
        return;
    }

    int saved = 0, skipped = 0;
    if (choice == ID_SAVE_TO_FOLDER) {
        wchar_t folder[MAX_PATH];
        if (!select_folder(g_app.hwnd_main,
            trw(L"Select output folder", L"Selecione a pasta de saída",
                L"Seleccione la carpeta de salida", L"Выберите папку вывода"),
            folder, MAX_PATH)) {
            gui_update_status("Save cancelled; project cache kept");
            return;
        }
        saved = save_archives_to_folder(folder, &skipped);
    } else {
        for (int i = 0; i < g_app.ytd_count; i++) {
            YtdFile *archive = g_app.ytds[i];
            if (archive->is_rpf_group) continue;
            if (archive->is_preview) continue;   /* uncommitted migration preview */
            if (archive->type == ARCHIVE_MODEL_READONLY) {
                skipped++;
                continue;
            }
            if (save_archive_to_path(archive, archive->file_path)) saved++;
            else skipped++;
        }
    }
    gui_update_status("Saved %d files (%d skipped); cache: %d files", saved, skipped, cache_saved);
}

/* ── Detect / Migrate Duplicates ───────────────────────────────────── */

typedef struct {
    DupCriterion criterion;
    MigrateStrategy strategy;
} MigrateParams;

static INT_PTR CALLBACK DetectDlgProc(HWND hDlg, UINT msg, WPARAM wp, LPARAM lp) {
    MigrateParams *params = (MigrateParams *)GetWindowLongPtrW(hDlg, DWLP_USER);
    switch (msg) {
    case WM_INITDIALOG: {
        SetWindowLongPtrW(hDlg, DWLP_USER, lp);
        params = (MigrateParams *)lp;
        HWND hC = GetDlgItem(hDlg, IDC_DET_CRITERION);
        SendMessageW(hC, CB_ADDSTRING, 0, (LPARAM)L"By Name");
        SendMessageW(hC, CB_ADDSTRING, 0, (LPARAM)L"By Hash (data)");
        SendMessageW(hC, CB_ADDSTRING, 0, (LPARAM)L"By Name + Hash");
        SendMessageW(hC, CB_SETCURSEL, (WPARAM)params->criterion, 0);

        HWND hS = GetDlgItem(hDlg, IDC_MIG_STRATEGY);
        SendMessageW(hS, CB_ADDSTRING, 0, (LPARAM)L"Save mixed (master kept in both)");
        SendMessageW(hS, CB_ADDSTRING, 0, (LPARAM)L"Move all to consolidated");
        SendMessageW(hS, CB_SETCURSEL, (WPARAM)params->strategy, 0);
        return TRUE;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK) {
            int c = (int)SendDlgItemMessageW(hDlg, IDC_DET_CRITERION, CB_GETCURSEL, 0, 0);
            int s = (int)SendDlgItemMessageW(hDlg, IDC_MIG_STRATEGY, CB_GETCURSEL, 0, 0);
            if (c < 0) c = 0;
            if (s < 0) s = 0;
            params->criterion = (DupCriterion)c;
            params->strategy = (MigrateStrategy)s;
            EndDialog(hDlg, IDOK);
            return TRUE;
        }
        if (LOWORD(wp) == IDCANCEL) { EndDialog(hDlg, IDCANCEL); return TRUE; }
        break;
    case WM_CLOSE: EndDialog(hDlg, IDCANCEL); return TRUE;
    }
    return FALSE;
}

static LPDLGTEMPLATE build_detect_template(void) {
    uint8_t *buf = (uint8_t *)calloc(1, 4096);
    uint8_t *p = buf;

    DLGTEMPLATE *dt = (DLGTEMPLATE *)p;
    dt->style = DS_MODALFRAME | DS_CENTER | WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE;
    dt->dwExtendedStyle = 0;
    dt->cdit = 6;
    dt->x = 0; dt->y = 0; dt->cx = 230; dt->cy = 110;
    p += sizeof(DLGTEMPLATE);
    *(WORD *)p = 0; p += 2;
    *(WORD *)p = 0; p += 2;
    const wchar_t *title = L"Detect Duplicates";
    size_t tlen = (wcslen(title) + 1) * 2;
    memcpy(p, title, tlen); p += tlen;
    p = (uint8_t *)(((uintptr_t)p + 3) & ~3);

    #define ADD_ITEM(style_, x_, y_, cx_, cy_, id_, cls_atom, text_) do { \
        p = (uint8_t *)(((uintptr_t)p + 3) & ~3); \
        DLGITEMTEMPLATE *it = (DLGITEMTEMPLATE *)p; \
        it->style = (style_) | WS_CHILD | WS_VISIBLE; \
        it->dwExtendedStyle = 0; \
        it->x = x_; it->y = y_; it->cx = cx_; it->cy = cy_; \
        it->id = id_; \
        p += sizeof(DLGITEMTEMPLATE); \
        *(WORD *)p = 0xFFFF; p += 2; \
        *(WORD *)p = cls_atom; p += 2; \
        size_t slen = (wcslen(text_) + 1) * 2; \
        memcpy(p, text_, slen); p += slen; \
        *(WORD *)p = 0; p += 2; \
    } while(0)

    ADD_ITEM(SS_LEFT, 10, 12, 70, 12, (WORD)-1, 0x0082, L"Criterion:");
    ADD_ITEM(CBS_DROPDOWNLIST | WS_TABSTOP, 85, 10, 135, 100, IDC_DET_CRITERION, 0x0085, L"");
    ADD_ITEM(SS_LEFT, 10, 42, 70, 12, (WORD)-1, 0x0082, L"Strategy:");
    ADD_ITEM(CBS_DROPDOWNLIST | WS_TABSTOP, 85, 40, 135, 100, IDC_MIG_STRATEGY, 0x0085, L"");
    ADD_ITEM(BS_DEFPUSHBUTTON | WS_TABSTOP, 65, 82, 55, 16, IDOK,     0x0080, L"Detect");
    ADD_ITEM(BS_PUSHBUTTON | WS_TABSTOP,    130, 82, 55, 16, IDCANCEL, 0x0080, L"Cancel");

    #undef ADD_ITEM
    return (LPDLGTEMPLATE)buf;
}

/* Toggle a sidebar button's visibility, relayout and repaint. */
static void set_sidebar_btn_visible(int id, bool vis) {
    for (int i = 0; i < (int)SIDEBAR_BTN_COUNT; i++) {
        if (g_sidebar_btns[i].id == id) { g_sidebar_btns[i].visible = vis; break; }
    }
    layout_children();
    InvalidateRect(g_app.hwnd_sidebar, NULL, TRUE);
}

/* Drop pending migration state and hide the Migrate button. */
static void reset_migration_state(void) {
    free(g_pending_removals);
    g_pending_removals = NULL;
    g_pending_removal_count = 0;
    g_has_pending_migration = false;
    set_sidebar_btn_visible(ID_SIDEBAR_MIGRATE, false);
}

/* Remove any preview consolidated YTDs from the list and clear pending state. */
static void clear_preview_consolidations(void) {
    for (int i = 0; i < g_app.ytd_count; ) {
        if (g_app.ytds[i]->is_preview) {
            ytd_free(g_app.ytds[i]);
            for (int j = i; j < g_app.ytd_count - 1; j++)
                g_app.ytds[j] = g_app.ytds[j + 1];
            g_app.ytd_count--;
        } else {
            i++;
        }
    }
    reset_migration_state();
}

static void do_detect_duplicates(void) {
    if (g_app.ytd_count == 0) {
        gui_update_status("No files loaded");
        return;
    }

    MigrateParams params = {DUP_BY_HASH, MIGRATE_MIXED};
    LPDLGTEMPLATE tpl = build_detect_template();
    INT_PTR result = DialogBoxIndirectParamW(GetModuleHandleW(NULL), tpl,
        g_app.hwnd_main, DetectDlgProc, (LPARAM)&params);
    free(tpl);
    if (result != IDOK) return;

    /* Re-detecting discards any previous preview. */
    clear_preview_consolidations();

    PendingRemoval *removals = NULL;
    int removal_count = 0, dup_groups = 0, affected = 0, consolidated = 0;
    optimizer_build_consolidation(g_app.ytds, &g_app.ytd_count, MAX_LOADED_YTDS,
        params.criterion, params.strategy,
        &removals, &removal_count, &dup_groups, &affected, &consolidated);

    const wchar_t *strat_name =
        (params.strategy == MIGRATE_MIXED) ? L"Save mixed" : L"Move all to consolidated";
    const wchar_t *crit_name =
        (params.criterion == DUP_BY_HASH) ? L"Hex" :
        (params.criterion == DUP_BY_NAME_AND_HASH) ? L"Name + Hex" : L"Name";

    if (dup_groups == 0) {
        free(removals);
        MessageBoxW(g_app.hwnd_main,
            trw(L"No duplicate textures were found.", L"Nenhuma textura duplicada foi encontrada.",
                L"No se encontraron texturas duplicadas.", L"Дубликаты текстур не найдены."),
            L"Detect Duplicates", MB_OK | MB_ICONINFORMATION);
        gui_update_status("No duplicates found (%ls)", crit_name);
        InvalidateRect(g_app.hwnd_content, NULL, TRUE);
        InvalidateRect(g_app.hwnd_sidebar, NULL, TRUE);
        return;
    }

    /* Keep the preview; reveal Migrate so the user can commit it. */
    g_pending_removals = removals;
    g_pending_removal_count = removal_count;
    g_has_pending_migration = true;
    set_sidebar_btn_visible(ID_SIDEBAR_MIGRATE, true);
    apply_archive_sort();

    wchar_t msg[600];
    _snwprintf(msg, 600,
        L"Preview built (nothing applied yet).\n\n"
        L"Criterion: %s\n"
        L"Strategy: %s\n"
        L"Duplicate groups: %d\n"
        L"Textures to move: %d\n"
        L"Preview consolidated YTDs: %d\n\n"
        L"Review the 'consolidated_textures_*' entries, then click "
        L"'Migrate Dups' to apply, or 'Detect Duplicates' again to change settings.",
        crit_name, strat_name, dup_groups, affected, consolidated);
    MessageBoxW(g_app.hwnd_main, msg, L"Detect Duplicates", MB_OK | MB_ICONINFORMATION);
    gui_update_status("Preview: %d groups, %d to move, %d consolidated (%ls / %ls)",
        dup_groups, affected, consolidated, crit_name, strat_name);

    InvalidateRect(g_app.hwnd_content, NULL, TRUE);
    InvalidateRect(g_app.hwnd_sidebar, NULL, TRUE);
}

static void do_migrate_duplicates(void) {
    if (!g_has_pending_migration) {
        gui_update_status("Run 'Detect Duplicates' first");
        return;
    }

    int confirm = MessageBoxW(g_app.hwnd_main,
        trw(L"Apply this migration?\n\nDuplicate textures will be removed from their original YTDs and the consolidated files committed.",
            L"Aplicar esta migração?\n\nAs texturas duplicadas serão removidas dos YTDs originais e os arquivos consolidados confirmados.",
            L"¿Aplicar esta migración?\n\nLas texturas duplicadas se eliminarán de los YTD originales y los consolidados se confirmarán.",
            L"Применить миграцию?\n\nДубликаты будут удалены из исходных YTD, а консолидированные файлы зафиксированы."),
        L"Migrate Duplicates", MB_YESNO | MB_ICONQUESTION);
    if (confirm != IDYES) return;

    optimizer_apply_removals(g_pending_removals, g_pending_removal_count);

    int committed = 0;
    for (int i = 0; i < g_app.ytd_count; i++) {
        if (g_app.ytds[i]->is_preview) {
            g_app.ytds[i]->is_preview = false;
            g_app.ytds[i]->modified = true;
            committed++;
        }
    }
    int moved = g_pending_removal_count;

    reset_migration_state();   /* frees g_pending_removals, hides Migrate button */
    apply_archive_sort();

    wchar_t msg[400];
    _snwprintf(msg, 400,
        L"Migration applied.\n\n"
        L"Consolidated YTDs committed: %d\n"
        L"Textures removed from originals: %d\n\n"
        L"Use 'Save All' to persist the changes to disk.",
        committed, moved);
    MessageBoxW(g_app.hwnd_main, msg, L"Migrate Duplicates", MB_OK | MB_ICONINFORMATION);
    gui_update_status("Migrated: %d consolidated committed, %d textures removed", committed, moved);

    InvalidateRect(g_app.hwnd_content, NULL, TRUE);
    InvalidateRect(g_app.hwnd_sidebar, NULL, TRUE);
}

/* ── Smart Optimize Dialog ─────────────────────────────────────────── */

typedef struct {
    int max_w;
    int max_h;
    TexFormat fmt;
    int mips;
} SmartOptParams;

static INT_PTR CALLBACK SmartOptDlgProc(HWND hDlg, UINT msg, WPARAM wp, LPARAM lp) {
    SmartOptParams *params = (SmartOptParams *)GetWindowLongPtrW(hDlg, DWLP_USER);
    switch (msg) {
    case WM_INITDIALOG: {
        SetWindowLongPtrW(hDlg, DWLP_USER, lp);
        params = (SmartOptParams *)lp;
        HWND hW = GetDlgItem(hDlg, IDC_OPT_MAXW);
        HWND hH = GetDlgItem(hDlg, IDC_OPT_MAXH);
        HWND hF = GetDlgItem(hDlg, IDC_OPT_FMT);
        const wchar_t *sizes[] = {L"4096", L"2048", L"1024", L"512", L"256", L"128", L"64", L"32"};
        for (int i = 0; i < 8; i++) {
            SendMessageW(hW, CB_ADDSTRING, 0, (LPARAM)sizes[i]);
            SendMessageW(hH, CB_ADDSTRING, 0, (LPARAM)sizes[i]);
        }
        SendMessageW(hW, CB_SETCURSEL, 2, 0); /* 512 */
        SendMessageW(hH, CB_SETCURSEL, 2, 0);

        const wchar_t *fmts[] = {L"Keep Original", L"DXT1 (BC1)", L"DXT5 (BC3)", L"BC7"};
        for (int i = 0; i < 4; i++)
            SendMessageW(hF, CB_ADDSTRING, 0, (LPARAM)fmts[i]);
        SendMessageW(hF, CB_SETCURSEL, 0, 0);

        HWND hM = GetDlgItem(hDlg, IDC_OPT_MIPMODE);
        SendMessageW(hM, CB_ADDSTRING, 0, (LPARAM)L"Original");
        SendMessageW(hM, CB_ADDSTRING, 0, (LPARAM)L"Ideal (Auto)");
        SendMessageW(hM, CB_ADDSTRING, 0, (LPARAM)L"Custom");
        
        if (params->mips == -2) {
            SendMessageW(hM, CB_SETCURSEL, 0, 0);
            EnableWindow(GetDlgItem(hDlg, IDC_OPT_MIPVAL), FALSE);
        } else if (params->mips == -1) {
            SendMessageW(hM, CB_SETCURSEL, 1, 0);
            EnableWindow(GetDlgItem(hDlg, IDC_OPT_MIPVAL), FALSE);
        } else {
            SendMessageW(hM, CB_SETCURSEL, 2, 0);
            EnableWindow(GetDlgItem(hDlg, IDC_OPT_MIPVAL), TRUE);
            wchar_t buf[16]; _itow(params->mips, buf, 10);
            SetDlgItemTextW(hDlg, IDC_OPT_MIPVAL, buf);
        }

        return TRUE;
    }
    case WM_COMMAND:
        if (HIWORD(wp) == CBN_SELCHANGE && LOWORD(wp) == IDC_OPT_MIPMODE) {
            int sel = (int)SendDlgItemMessageW(hDlg, IDC_OPT_MIPMODE, CB_GETCURSEL, 0, 0);
            EnableWindow(GetDlgItem(hDlg, IDC_OPT_MIPVAL), (sel == 2));
        }
        if (LOWORD(wp) == IDOK) {
            wchar_t buf[16];
            GetDlgItemTextW(hDlg, IDC_OPT_MAXW, buf, 16);
            params->max_w = _wtoi(buf);
            GetDlgItemTextW(hDlg, IDC_OPT_MAXH, buf, 16);
            params->max_h = _wtoi(buf);
            int sel = (int)SendDlgItemMessageW(hDlg, IDC_OPT_FMT, CB_GETCURSEL, 0, 0);
            TexFormat fmts[] = {TEX_FMT_UNKNOWN, TEX_FMT_BC1, TEX_FMT_BC3, TEX_FMT_BC7};
            params->fmt = fmts[sel];
            if (params->max_w < 4) params->max_w = 4;
            if (params->max_h < 4) params->max_h = 4;
            
            int mip_sel = (int)SendDlgItemMessageW(hDlg, IDC_OPT_MIPMODE, CB_GETCURSEL, 0, 0);
            if (mip_sel == 0) params->mips = -2;
            else if (mip_sel == 1) params->mips = -1;
            else {
                GetDlgItemTextW(hDlg, IDC_OPT_MIPVAL, buf, 16);
                params->mips = _wtoi(buf);
                if (params->mips < 1) params->mips = 1;
            }

            EndDialog(hDlg, IDOK);
            return TRUE;
        }
        if (LOWORD(wp) == IDCANCEL) {
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;
    case WM_CLOSE:
        EndDialog(hDlg, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}

/* Build dialog template in memory so we don't need a .rc file */
static LPDLGTEMPLATE build_smart_opt_template(const wchar_t *title) {
    uint8_t *buf = (uint8_t *)calloc(1, 4096);
    uint8_t *p = buf;

    DLGTEMPLATE *dt = (DLGTEMPLATE *)p;
    dt->style = DS_MODALFRAME | DS_CENTER | WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE;
    dt->dwExtendedStyle = 0;
    dt->cdit = 11; 
    dt->x = 0; dt->y = 0; dt->cx = 200; dt->cy = 130;
    p += sizeof(DLGTEMPLATE);
    *(WORD *)p = 0; p += 2; 
    *(WORD *)p = 0; p += 2; 
    size_t tlen = (wcslen(title) + 1) * 2;
    memcpy(p, title, tlen); p += tlen;

    p = (uint8_t *)(((uintptr_t)p + 3) & ~3);

    #define ADD_ITEM(style_, x_, y_, cx_, cy_, id_, cls_atom, text_) do { \
        p = (uint8_t *)(((uintptr_t)p + 3) & ~3); \
        DLGITEMTEMPLATE *it = (DLGITEMTEMPLATE *)p; \
        it->style = (style_) | WS_CHILD | WS_VISIBLE; \
        it->dwExtendedStyle = 0; \
        it->x = x_; it->y = y_; it->cx = cx_; it->cy = cy_; \
        it->id = id_; \
        p += sizeof(DLGITEMTEMPLATE); \
        *(WORD *)p = 0xFFFF; p += 2; \
        *(WORD *)p = cls_atom; p += 2; \
        size_t slen = (wcslen(text_) + 1) * 2; \
        memcpy(p, text_, slen); p += slen; \
        *(WORD *)p = 0; p += 2; \
    } while(0)

    ADD_ITEM(SS_LEFT, 10, 10, 60, 12, (WORD)-1, 0x0082, L"Max Width:");
    ADD_ITEM(SS_LEFT, 10, 30, 60, 12, (WORD)-1, 0x0082, L"Max Height:");
    ADD_ITEM(SS_LEFT, 10, 50, 60, 12, (WORD)-1, 0x0082, L"Format:");
    ADD_ITEM(SS_LEFT, 10, 70, 60, 12, (WORD)-1, 0x0082, L"Mipmaps:");
    
    ADD_ITEM(CBS_DROPDOWN | WS_TABSTOP | WS_VSCROLL, 80, 8, 110, 100, IDC_OPT_MAXW, 0x0085, L"");
    ADD_ITEM(CBS_DROPDOWN | WS_TABSTOP | WS_VSCROLL, 80, 28, 110, 100, IDC_OPT_MAXH, 0x0085, L"");
    ADD_ITEM(CBS_DROPDOWNLIST | WS_TABSTOP, 80, 48, 110, 100, IDC_OPT_FMT,  0x0085, L"");
    ADD_ITEM(CBS_DROPDOWNLIST | WS_TABSTOP, 80, 70, 75, 100, IDC_OPT_MIPMODE, 0x0085, L"");
    ADD_ITEM(WS_BORDER | WS_TABSTOP, 160, 70, 30, 12, IDC_OPT_MIPVAL, 0x0081, L"");
    
    ADD_ITEM(BS_DEFPUSHBUTTON | WS_TABSTOP, 50, 100, 50, 16, IDOK,     0x0080, L"Optimize");
    ADD_ITEM(BS_PUSHBUTTON | WS_TABSTOP,    110, 100, 50, 16, IDCANCEL, 0x0080, L"Cancel");

    #undef ADD_ITEM
    return (LPDLGTEMPLATE)buf;
}


static INT_PTR CALLBACK CustomResizeDlgProc(HWND hDlg, UINT msg, WPARAM wp, LPARAM lp) {
    CustomResizeParams *params = (CustomResizeParams *)GetWindowLongPtrW(hDlg, DWLP_USER);
    switch (msg) {
    case WM_INITDIALOG: {
        SetWindowLongPtrW(hDlg, DWLP_USER, lp);
        params = (CustomResizeParams *)lp;
        
        HWND hRW = GetDlgItem(hDlg, IDC_RES_MAXW);
        HWND hRH = GetDlgItem(hDlg, IDC_RES_MAXH);
        const wchar_t *common_sizes[] = {L"4096", L"2048", L"1024", L"512", L"256", L"128", L"64", L"32"};
        for (int i = 0; i < 8; i++) {
            SendMessageW(hRW, CB_ADDSTRING, 0, (LPARAM)common_sizes[i]);
            SendMessageW(hRH, CB_ADDSTRING, 0, (LPARAM)common_sizes[i]);
        }
        wchar_t buf[16];
        _itow(params->max_w, buf, 10);
        SetWindowTextW(hRW, buf);
        _itow(params->max_h, buf, 10);
        SetWindowTextW(hRH, buf);

        HWND hF = GetDlgItem(hDlg, IDC_RES_FMT);
        const wchar_t *fmts[] = {L"Preserve Original", L"DXT1 (BC1)", L"DXT3 (BC2)", L"DXT5 (BC3)", L"ATI1 (BC4)", L"ATI2 (BC5)", L"BC7", L"A8R8G8B8", L"A1R5G5B5", L"A8"};
        for (int i = 0; i < 10; i++) SendMessageW(hF, CB_ADDSTRING, 0, (LPARAM)fmts[i]);
        
        int sel = 0;
        TexFormat fmt_vals[] = {TEX_FMT_UNKNOWN, TEX_FMT_BC1, TEX_FMT_BC2, TEX_FMT_BC3, TEX_FMT_BC4, TEX_FMT_BC5, TEX_FMT_BC7, TEX_FMT_A8R8G8B8, TEX_FMT_B5G5R5A1, TEX_FMT_A8};
        for(int i = 0; i < 10; i++) if (params->fmt == fmt_vals[i]) { sel = i; break; }
        SendMessageW(hF, CB_SETCURSEL, sel, 0);

        HWND hM = GetDlgItem(hDlg, IDC_RES_MIPMODE);
        SendMessageW(hM, CB_ADDSTRING, 0, (LPARAM)L"Original");
        SendMessageW(hM, CB_ADDSTRING, 0, (LPARAM)L"Ideal (Auto)");
        SendMessageW(hM, CB_ADDSTRING, 0, (LPARAM)L"Custom");
        
        if (params->mips == -2) {
            SendMessageW(hM, CB_SETCURSEL, 0, 0);
            EnableWindow(GetDlgItem(hDlg, IDC_RES_MIPVAL), FALSE);
        } else if (params->mips == -1) {
            SendMessageW(hM, CB_SETCURSEL, 1, 0);
            EnableWindow(GetDlgItem(hDlg, IDC_RES_MIPVAL), FALSE);
        } else {
            SendMessageW(hM, CB_SETCURSEL, 2, 0);
            EnableWindow(GetDlgItem(hDlg, IDC_RES_MIPVAL), TRUE);
            _itow(params->mips, buf, 10);
            SetDlgItemTextW(hDlg, IDC_RES_MIPVAL, buf);
        }

        return TRUE;
    }
    case WM_COMMAND:
        if (HIWORD(wp) == CBN_SELCHANGE && LOWORD(wp) == IDC_RES_MIPMODE) {
            int sel = (int)SendDlgItemMessageW(hDlg, IDC_RES_MIPMODE, CB_GETCURSEL, 0, 0);
            EnableWindow(GetDlgItem(hDlg, IDC_RES_MIPVAL), (sel == 2));
        }
        if (LOWORD(wp) == IDOK) {
            wchar_t buf[16];
            GetDlgItemTextW(hDlg, IDC_RES_MAXW, buf, 16); params->max_w = _wtoi(buf);
            GetDlgItemTextW(hDlg, IDC_RES_MAXH, buf, 16); params->max_h = _wtoi(buf);
            
            int sel = (int)SendDlgItemMessageW(hDlg, IDC_RES_FMT, CB_GETCURSEL, 0, 0);
            TexFormat fmt_vals[] = {TEX_FMT_UNKNOWN, TEX_FMT_BC1, TEX_FMT_BC2, TEX_FMT_BC3, TEX_FMT_BC4, TEX_FMT_BC5, TEX_FMT_BC7, TEX_FMT_A8R8G8B8, TEX_FMT_B5G5R5A1, TEX_FMT_A8};
            if (sel > 0) params->fmt = fmt_vals[sel];
            
            int mip_sel = (int)SendDlgItemMessageW(hDlg, IDC_RES_MIPMODE, CB_GETCURSEL, 0, 0);
            if (mip_sel == 0) params->mips = -2;
            else if (mip_sel == 1) params->mips = -1;
            else {
                GetDlgItemTextW(hDlg, IDC_RES_MIPVAL, buf, 16);
                params->mips = _wtoi(buf);
                if (params->mips < 1) params->mips = 1;
            }

            EndDialog(hDlg, IDOK);
            return TRUE;
        }
        if (LOWORD(wp) == IDCANCEL) {
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;
    case WM_CLOSE: EndDialog(hDlg, IDCANCEL); return TRUE;
    }
    return FALSE;
}

static LPDLGTEMPLATE build_custom_resize_template(void) {
    uint8_t *buf = (uint8_t *)calloc(1, 4096);
    uint8_t *p = buf;

    DLGTEMPLATE *dt = (DLGTEMPLATE *)p;
    dt->style = DS_MODALFRAME | DS_CENTER | WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE;
    dt->dwExtendedStyle = 0;
    dt->cdit = 11; 
    dt->x = 0; dt->y = 0; dt->cx = 200; dt->cy = 130;
    p += sizeof(DLGTEMPLATE);
    *(WORD *)p = 0; p += 2; 
    *(WORD *)p = 0; p += 2; 
    const wchar_t *title = L"Custom Resize";
    size_t tlen = (wcslen(title) + 1) * 2;
    memcpy(p, title, tlen); p += tlen;

    p = (uint8_t *)(((uintptr_t)p + 3) & ~3);

    #define ADD_ITEM(style_, x_, y_, cx_, cy_, id_, cls_atom, text_) do { \
        p = (uint8_t *)(((uintptr_t)p + 3) & ~3); \
        DLGITEMTEMPLATE *it = (DLGITEMTEMPLATE *)p; \
        it->style = (style_) | WS_CHILD | WS_VISIBLE; \
        it->dwExtendedStyle = 0; \
        it->x = x_; it->y = y_; it->cx = cx_; it->cy = cy_; \
        it->id = id_; \
        p += sizeof(DLGITEMTEMPLATE); \
        *(WORD *)p = 0xFFFF; p += 2; \
        *(WORD *)p = cls_atom; p += 2; \
        size_t slen = (wcslen(text_) + 1) * 2; \
        memcpy(p, text_, slen); p += slen; \
        *(WORD *)p = 0; p += 2; \
    } while(0)

    ADD_ITEM(SS_LEFT, 10, 10, 60, 12, (WORD)-1, 0x0082, L"Width:");
    ADD_ITEM(SS_LEFT, 10, 30, 60, 12, (WORD)-1, 0x0082, L"Height:");
    ADD_ITEM(SS_LEFT, 10, 50, 60, 12, (WORD)-1, 0x0082, L"Format:");
    ADD_ITEM(SS_LEFT, 10, 70, 60, 12, (WORD)-1, 0x0082, L"Mipmaps:");
    
    ADD_ITEM(CBS_DROPDOWN | WS_TABSTOP | WS_VSCROLL, 80, 8, 110, 100, IDC_RES_MAXW, 0x0085, L"");
    ADD_ITEM(CBS_DROPDOWN | WS_TABSTOP | WS_VSCROLL, 80, 28, 110, 100, IDC_RES_MAXH, 0x0085, L"");
    ADD_ITEM(CBS_DROPDOWNLIST | WS_TABSTOP, 80, 48, 110, 100, IDC_RES_FMT,  0x0085, L"");
    ADD_ITEM(CBS_DROPDOWNLIST | WS_TABSTOP, 80, 70, 75, 100, IDC_RES_MIPMODE, 0x0085, L"");
    ADD_ITEM(WS_BORDER | WS_TABSTOP, 160, 70, 30, 12, IDC_RES_MIPVAL, 0x0081, L"");
    
    ADD_ITEM(BS_DEFPUSHBUTTON | WS_TABSTOP, 50, 100, 50, 16, IDOK,     0x0080, L"Resize");
    ADD_ITEM(BS_PUSHBUTTON | WS_TABSTOP,    110, 100, 50, 16, IDCANCEL, 0x0080, L"Cancel");

    #undef ADD_ITEM
    return (LPDLGTEMPLATE)buf;
}

static void do_smart_optimize(void) {
    if (g_app.ytd_count == 0) {
        gui_update_status("No files loaded");
        return;
    }

    SmartOptParams params = {512, 512, TEX_FMT_UNKNOWN, -2};
    LPDLGTEMPLATE tpl = build_smart_opt_template(L"Smart Optimize");

    INT_PTR result = DialogBoxIndirectParamW(
        GetModuleHandleW(NULL), tpl, g_app.hwnd_main,
        SmartOptDlgProc, (LPARAM)&params);

    free(tpl);

    if (result != IDOK) return;

    LOG("do_smart_optimize: max=%dx%d fmt=%d", params.max_w, params.max_h, params.fmt);
    log_encoder_intent("Smart Optimize");
    gui_update_status("Optimizing... max %dx%d", params.max_w, params.max_h);

    int total_resized = 0;
    for (int i = 0; i < g_app.ytd_count; i++) {
        int r = optimizer_smart_resize(g_app.ytds[i], params.max_w, params.max_h, params.fmt, params.mips);
        LOG("  %s: %d textures resized", g_app.ytds[i]->name, r);
        total_resized += r;
    }

    gui_update_status("Smart Optimize: %d textures resized across %d files", total_resized, g_app.ytd_count);
    InvalidateRect(g_app.hwnd_content, NULL, TRUE);
}

static void do_custom_resize(int ytd_idx, int tex_idx) {
    TextureEntry *te = &g_app.ytds[ytd_idx]->textures[tex_idx];

    CustomResizeParams params = {te->width, te->height, te->format, -2};
    LPDLGTEMPLATE tpl = build_custom_resize_template();

    INT_PTR result = DialogBoxIndirectParamW(
        GetModuleHandleW(NULL), tpl, g_app.hwnd_main,
        CustomResizeDlgProc, (LPARAM)&params);

    free(tpl);

    if (result != IDOK) return;

    do_texture_resize(ytd_idx, tex_idx, params.max_w, params.max_h, params.fmt, params.mips);
    InvalidateRect(g_app.hwnd_content, NULL, TRUE);
}


/* Pick a smaller target format for Fast Recompress.
 * The only block formats that differ in size are 8-byte (BC1/BC4) vs 16-byte
 * (BC2/BC3/BC5/BC7). A BC2/BC3/BC7 texture with no real alpha can drop to BC1
 * and halve its block size. BC5 (two-channel, normal maps) is left alone. */
static TexFormat fast_recompress_target(const TextureEntry *te) {
    switch (te->format) {
        case TEX_FMT_BC2:
        case TEX_FMT_BC3:
        case TEX_FMT_BC7:
            return tex_alpha_in_use(te) ? te->format : TEX_FMT_BC1;
        default:
            return te->format;
    }
}

static void do_fast_recompress(void) {
    if (g_app.ytd_count == 0) {
        gui_update_status("No files loaded");
        return;
    }
    int total = 0, downgraded = 0;
    size_t size_before = 0, size_after = 0;
    log_encoder_intent("Fast Recompress");
    gui_update_status("Fast Recompressing...");
    for (int i = 0; i < g_app.ytd_count; i++) {
        YtdFile *ytd = g_app.ytds[i];
        for (int t = 0; t < ytd->texture_count; t++) {
            TextureEntry *te = &ytd->textures[t];
            if (!tex_format_is_compressed(te->format)) continue;

            TexFormat target = fast_recompress_target(te);
            if (!tex_format_can_encode(target)) target = te->format;
            bool is_downgrade = (target != te->format);

            size_before += te->data_size;
            /* max_mips = -2 preserves the level count but generate_mips still
             * trims any levels past the natural 1x1 chain. */
            do_texture_resize(i, t, te->width, te->height, target, -2);
            size_after += te->data_size;
            total++;
            if (is_downgrade && te->format == target) downgraded++;
        }
    }
    double before_mib = size_before / (1024.0 * 1024.0);
    double after_mib = size_after / (1024.0 * 1024.0);
    double saved_mib = before_mib - after_mib;
    double pct = (size_before > 0) ? (saved_mib / before_mib * 100.0) : 0.0;

    wchar_t msg[512];
    _snwprintf(msg, 512,
        L"Fast Recompress Complete\n\n"
        L"Textures processed: %d\n"
        L"Format downgrades (→ BC1): %d\n"
        L"Before: %.2f MiB\n"
        L"After: %.2f MiB\n"
        L"Saved: %.2f MiB (%.1f%%)",
        total, downgraded, before_mib, after_mib, saved_mib, pct);
    MessageBoxW(g_app.hwnd_main, msg, L"Fast Recompress", MB_OK | MB_ICONINFORMATION);

    gui_update_status("Fast Recompress: %d textures, %d downgraded, saved %.2f MiB (%.1f%%)",
        total, downgraded, saved_mib, pct);
    InvalidateRect(g_app.hwnd_content, NULL, TRUE);
}

/* ── Texture hit testing ───────────────────────────────────────────── */

static void do_same_format_recompress(void) {
    if (g_app.ytd_count == 0) {
        gui_update_status("No files loaded");
        return;
    }

    int processed = 0, skipped = 0;
    size_t size_before = 0, size_after = 0;
    log_encoder_intent("Recompress Same Format");
    gui_update_status("Recompressing with original formats...");

    for (int i = 0; i < g_app.ytd_count; i++) {
        YtdFile *ytd = g_app.ytds[i];
        for (int t = 0; t < ytd->texture_count; t++) {
            TextureEntry *te = &ytd->textures[t];
            if (!tex_format_is_compressed(te->format) || !tex_format_can_encode(te->format) ||
                te->width < 4 || te->height < 4) {
                skipped++;
                continue;
            }

            size_t old_size = te->data_size;
            if (do_texture_resize(i, t, te->width, te->height, te->format, -2)) {
                size_before += old_size;
                size_after += te->data_size;
                processed++;
            } else {
                skipped++;
            }
        }
    }

    double before_mib = size_before / (1024.0 * 1024.0);
    double after_mib = size_after / (1024.0 * 1024.0);
    wchar_t msg[512];
    _snwprintf(msg, 512,
        L"Same Format Recompress Complete\n\n"
        L"Textures processed: %d\n"
        L"Textures skipped: %d\n"
        L"Before: %.2f MiB\n"
        L"After: %.2f MiB\n\n"
        L"Resolution, format and mip count were preserved.",
        processed, skipped, before_mib, after_mib);
    MessageBoxW(g_app.hwnd_main, msg, L"Recompress Same Format", MB_OK | MB_ICONINFORMATION);
    gui_update_status("Same Format Recompress: %d processed, %d skipped", processed, skipped);
    InvalidateRect(g_app.hwnd_content, NULL, TRUE);
}

static void choose_recompress_mode(void) {
    enum {
        /* Must NOT collide with IDOK(1) or IDCANCEL(2); TaskDialog returns those
         * when the user dismisses the dialog via X or Escape. */
        ID_RECOMPRESS_OPTIMIZE    = 100,
        ID_RECOMPRESS_SAME_FORMAT = 101
    };
    const TASKDIALOG_BUTTON buttons[] = {
        {ID_RECOMPRESS_OPTIMIZE,
            L"Optimize formats\nMay change texture formats to reduce size."},
        {ID_RECOMPRESS_SAME_FORMAT,
            L"Recompress same format\nPreserves formats and will probably not change size or performance."}
    };
    TASKDIALOGCONFIG dialog = {sizeof(dialog)};
    int choice = IDCANCEL;

    if (g_app.ytd_count == 0) {
        gui_update_status("No files loaded");
        return;
    }

    dialog.hwndParent = g_app.hwnd_main;
    /* Buttons carry a "title\nnote" layout, which only renders correctly as
     * command-link buttons; without this flag the \n looks broken. */
    dialog.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_USE_COMMAND_LINKS;
    dialog.pszWindowTitle = L"Recompress";
    dialog.pszMainInstruction = L"Choose how to recompress the loaded textures";
    dialog.pszContent =
        L"Optimize formats may change compatible texture formats and reduce file size.\n\n"
        L"Recompress same format preserves format, resolution and mip count. "
        L"It will probably not reduce memory usage or improve performance.";
    dialog.cButtons = ARRAYSIZE(buttons);
    dialog.pButtons = buttons;
    dialog.nDefaultButton = ID_RECOMPRESS_OPTIMIZE;

    if (FAILED(TaskDialogIndirect(&dialog, &choice, NULL, NULL)))
        return;

    if (choice == ID_RECOMPRESS_OPTIMIZE)
        do_fast_recompress();
    else if (choice == ID_RECOMPRESS_SAME_FORMAT)
        do_same_format_recompress();
}

static bool texture_matches_search(TextureEntry *tex) {
    if (!g_app.search_filter[0]) return true;
    char lower_name[EO_MAX_NAME], lower_filter[256];
    strncpy(lower_name, tex->name, EO_MAX_NAME);
    lower_name[EO_MAX_NAME - 1] = 0;
    _strlwr_s(lower_name, EO_MAX_NAME);
    strncpy(lower_filter, g_app.search_filter, 256);
    lower_filter[255] = 0;
    _strlwr_s(lower_filter, 256);
    return strstr(lower_name, lower_filter) != NULL;
}

static bool hit_test_texture_grid(YtdFile *ytd, int ytd_idx, int mx, int my,
                                  int cards_per_row, int *io_y, int *out_ytd,
                                  int *out_tex, int *out_cx, int *out_cy) {
    int col = 0;
    for (int t = 0; t < ytd->texture_count; t++) {
        TextureEntry *tex = &ytd->textures[t];
        if (!texture_matches_search(tex)) continue;
        int cx = 12 + col * (CARD_W + CARD_MARGIN);
        int cy = *io_y;
        if (mx >= cx && mx < cx + CARD_W && my >= cy && my < cy + CARD_H) {
            if (out_ytd) *out_ytd = ytd_idx;
            if (out_tex) *out_tex = t;
            if (out_cx) *out_cx = cx;
            if (out_cy) *out_cy = cy;
            return true;
        }
        if (++col >= cards_per_row) {
            col = 0;
            *io_y += CARD_H + CARD_MARGIN;
        }
    }
    if (col > 0) *io_y += CARD_H + CARD_MARGIN;
    *io_y += 8;
    return false;
}

static bool hit_test_texture(int mx, int my, int *out_ytd, int *out_tex, int *out_cx, int *out_cy) {
    RECT crc;
    GetClientRect(g_app.hwnd_content, &crc);
    int cards_per_row = ((crc.right - 24) + CARD_MARGIN) / (CARD_W + CARD_MARGIN);
    if (cards_per_row < 1) cards_per_row = 1;

    int y = 8;
    for (int i = 0; i < g_app.ytd_count; i++) {
        YtdFile *ytd = g_app.ytds[i];
        if (ytd->from_rpf) continue;
        y += FOLDER_H + 4;
        if (!ytd->expanded) continue;
        if (ytd->is_rpf_group) {
            for (int c = 0; c < g_app.ytd_count; c++) {
                YtdFile *child = g_app.ytds[c];
                if (child->rpf_parent != ytd) continue;
                y += RPF_ENTRY_H + 4;
                if (child->expanded &&
                    hit_test_texture_grid(child, c, mx, my, cards_per_row, &y,
                                          out_ytd, out_tex, out_cx, out_cy))
                    return true;
            }
        } else if (hit_test_texture_grid(ytd, i, mx, my, cards_per_row, &y,
                                         out_ytd, out_tex, out_cx, out_cy)) {
            return true;
        }
    }
    return false;
}

/* ── Texture context menu ──────────────────────────────────────────── */

static void show_texture_context_menu(HWND hwnd, int screen_x, int screen_y, int ytd_idx, int tex_idx) {
    TextureEntry *tex = &g_app.ytds[ytd_idx]->textures[tex_idx];
    LOG("Context menu for '%s' (%dx%d %s)", tex->name, tex->width, tex->height, tex_format_name(tex->format));

    HMENU hMenu = CreatePopupMenu();
    HMENU hResize = CreatePopupMenu();
    HMENU hFmt = CreatePopupMenu();

    wchar_t half_str[64], qrtr_str[64];
    int hw = tex->width > 1 ? tex->width / 2 : 1;
    int hh = tex->height > 1 ? tex->height / 2 : 1;
    int qw = hw > 1 ? hw / 2 : 1;
    int qh = hh > 1 ? hh / 2 : 1;
    _snwprintf(half_str, 64, L"Half (%dx%d)", hw, hh);
    _snwprintf(qrtr_str, 64, L"Quarter (%dx%d)", qw, qh);

    AppendMenuW(hResize, MF_STRING, IDM_CTX_RESIZE_HALF, half_str);
    AppendMenuW(hResize, MF_STRING, IDM_CTX_RESIZE_QRTR, qrtr_str);
    AppendMenuW(hResize, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hResize, MF_STRING, IDM_CTX_RESIZE_CUSTOM,
        trw(L"Custom...", L"Personalizado...", L"Personalizado...", L"Другой..."));

    AppendMenuW(hFmt, MF_STRING, IDM_CTX_FMT_KEEP, L"Keep Original");
    AppendMenuW(hFmt, MF_STRING, IDM_CTX_FMT_BC1,  L"DXT1 (BC1)");
    AppendMenuW(hFmt, MF_STRING, IDM_CTX_FMT_BC3,  L"DXT5 (BC3)");
    AppendMenuW(hFmt, MF_STRING, IDM_CTX_FMT_BC5,  L"ATI2 (BC5)");
    AppendMenuW(hFmt, MF_STRING, IDM_CTX_FMT_BC7,  L"BC7");

    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hResize,
        trw(L"Resize", L"Redimensionar", L"Redimensionar", L"Изменить размер"));
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hFmt,
        trw(L"Convert Format", L"Converter formato", L"Convertir formato", L"Преобразовать формат"));
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, IDM_CTX_EXPORT_DDS,
        trw(L"Export as DDS...", L"Exportar como DDS...", L"Exportar como DDS...", L"Экспортировать DDS..."));
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    /* Unload (revert) is only meaningful once the texture was edited. */
    AppendMenuW(hMenu, MF_STRING | (tex->has_orig ? MF_ENABLED : (MF_GRAYED | MF_DISABLED)),
        IDM_CTX_UNLOAD,
        trw(L"Unload changes (revert to original)", L"Descartar alterações (voltar ao original)",
            L"Descartar cambios (volver al original)", L"Откатить изменения (к оригиналу)"));
    AppendMenuW(hMenu, MF_STRING, IDM_CTX_REMOVE,
        trw(L"Remove Texture", L"Remover textura", L"Eliminar textura", L"Удалить текстуру"));

    /* Store selection for WM_COMMAND */
    g_app.sel_ytd_idx = ytd_idx;
    g_app.sel_tex_idx = tex_idx;

    UINT cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                              screen_x, screen_y, 0, hwnd, NULL);
    DestroyMenu(hMenu);

    if (cmd == 0) return;

    switch (cmd) {
    case IDM_CTX_RESIZE_HALF:
        do_texture_resize(ytd_idx, tex_idx, hw, hh, tex->format, -2);
        break;
    case IDM_CTX_RESIZE_QRTR:
        do_texture_resize(ytd_idx, tex_idx, qw, qh, tex->format, -2);
        break;
    case IDM_CTX_RESIZE_CUSTOM:
        do_custom_resize(ytd_idx, tex_idx);
        break;
    case IDM_CTX_FMT_KEEP:
        break; /* no-op */
    case IDM_CTX_FMT_BC1:
        do_texture_resize(ytd_idx, tex_idx, tex->width, tex->height, TEX_FMT_BC1, -2);
        break;
    case IDM_CTX_FMT_BC3:
        do_texture_resize(ytd_idx, tex_idx, tex->width, tex->height, TEX_FMT_BC3, -2);
        break;
    case IDM_CTX_FMT_BC5:
        do_texture_resize(ytd_idx, tex_idx, tex->width, tex->height, TEX_FMT_BC5, -2);
        break;
    case IDM_CTX_FMT_BC7:
        do_texture_resize(ytd_idx, tex_idx, tex->width, tex->height, TEX_FMT_BC7, -2);
        break;
    case IDM_CTX_EXPORT_DDS:
        do_texture_export_dds(ytd_idx, tex_idx);
        break;
    case IDM_CTX_UNLOAD:
        do_texture_unload(ytd_idx, tex_idx);
        break;
    case IDM_CTX_REMOVE:
        do_texture_remove(ytd_idx, tex_idx);
        break;
    }
}

/* ── Texture actions ───────────────────────────────────────────────── */

static void do_texture_unload(int ytd_idx, int tex_idx) {
    YtdFile *ytd = g_app.ytds[ytd_idx];
    TextureEntry *te = &ytd->textures[tex_idx];
    if (!tex_revert_original(te)) {
        gui_update_status("'%s' has no changes to unload", te->name);
        return;
    }
    /* Mark the archive modified so the reverted (original) bytes are written
     * back on Save All, replacing any previously-saved edit. */
    ytd->modified = true;
    LOG("do_texture_unload: '%s' reverted to %dx%d %s",
        te->name, te->width, te->height, tex_format_name(te->format));
    gui_update_status("Unloaded changes on '%s' (reverted to original)", te->name);
    InvalidateRect(g_app.hwnd_content, NULL, TRUE);
}

static bool do_texture_resize(int ytd_idx, int tex_idx, int new_w, int new_h, TexFormat fmt, int max_mips) {
    YtdFile *ytd = g_app.ytds[ytd_idx];
    TextureEntry *te = &ytd->textures[tex_idx];
    TexFormat enc_fmt = (fmt == TEX_FMT_UNKNOWN) ? te->format : fmt;
    LOG("do_texture_resize: '%s' %dx%d -> %dx%d fmt=%d", te->name, te->width, te->height, new_w, new_h, fmt);
    if (!tex_format_can_encode(enc_fmt)) {
        gui_update_status("Cannot encode '%s' as %s", te->name, tex_format_name(enc_fmt));
        return false;
    }

    if (new_w < 4) new_w = 4;
    if (new_h < 4) new_h = 4;

    bc7enc_init();

    /* Decode to BGRA */
    int dec_w, dec_h;
    uint8_t *bgra = tex_decode_to_bgra(te, 0, &dec_w, &dec_h);
    if (!bgra) { gui_update_status("Failed to decode texture"); return false; }

    /* Convert BGRA → RGBA */
    size_t px_count = (size_t)dec_w * dec_h;
    uint8_t *rgba = (uint8_t *)malloc(px_count * 4);
    if (!rgba) { free(bgra); gui_update_status("Out of memory decoding '%s'", te->name); return false; }
    for (size_t p = 0; p < px_count; p++) {
        rgba[p*4+0] = bgra[p*4+2];
        rgba[p*4+1] = bgra[p*4+1];
        rgba[p*4+2] = bgra[p*4+0];
        rgba[p*4+3] = bgra[p*4+3];
    }
    free(bgra);

    /* Resize */
    uint8_t *resized = NULL;
    if (new_w != dec_w || new_h != dec_h) {
        resized = bc7enc_resize_rgba(rgba, dec_w, dec_h, new_w, new_h, 5);
        free(rgba);
        if (!resized) { gui_update_status("Failed to resize"); return false; }
    } else {
        resized = rgba;
    }

    /* Re-encode with mips */
    int mip_count = 0;
    size_t total_size = 0;
    uint8_t *new_data = tex_generate_mips(resized, new_w, new_h, enc_fmt,
                                           (max_mips == -2 ? te->mip_count : (max_mips == -1 ? 13 : max_mips)), &mip_count, &total_size);
    bc7enc_free(resized);
    if (!new_data) { gui_update_status("Failed to encode"); return false; }

    /* Snapshot the pre-edit state once so Unload can revert instantly. */
    tex_save_original(te);

    /* Replace texture data */
    free(te->data);
    te->data = new_data;
    te->data_size = total_size;
    te->width = new_w;
    te->height = new_h;
    te->format = enc_fmt;
    te->mip_count = mip_count;
    ytd->modified = true;

    LOG("  done: %dx%d %s, %zu bytes, %d mips", new_w, new_h, tex_format_name(enc_fmt), total_size, mip_count);
    gui_update_status("Resized '%s' to %dx%d (%s)", te->name, new_w, new_h, tex_format_name(enc_fmt));
    InvalidateRect(g_app.hwnd_content, NULL, TRUE);
    return true;
}

static void do_texture_export_dds(int ytd_idx, int tex_idx) {
    TextureEntry *tex = &g_app.ytds[ytd_idx]->textures[tex_idx];
    LOG("do_texture_export_dds: '%s'", tex->name);

    wchar_t wname[256];
    MultiByteToWideChar(CP_UTF8, 0, tex->name, -1, wname, 256);
    wchar_t default_name[300];
    _snwprintf(default_name, 300, L"%s.dds", wname);

    wchar_t save_path[MAX_PATH] = {0};
    wcsncpy(save_path, default_name, MAX_PATH - 1);

    OPENFILENAMEW ofn = {sizeof(ofn)};
    ofn.hwndOwner = g_app.hwnd_main;
    ofn.lpstrFilter = L"DDS Files\0*.dds\0All Files\0*.*\0";
    ofn.lpstrFile = save_path;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT;
    ofn.lpstrDefExt = L"dds";

    if (!GetSaveFileNameW(&ofn)) return;

    size_t dds_size = 0;
    uint8_t *dds = dds_build(tex, &dds_size);
    if (!dds) { gui_update_status("Failed to build DDS"); return; }

    FILE *f = _wfopen(save_path, L"wb");
    if (!f) { free(dds); gui_update_status("Failed to open output file"); return; }
    fwrite(dds, 1, dds_size, f);
    fclose(f);
    free(dds);

    LOG("  exported %zu bytes", dds_size);
    gui_update_status("Exported '%s' (%zu bytes)", tex->name, dds_size);
}

static void do_texture_remove(int ytd_idx, int tex_idx) {
    YtdFile *ytd = g_app.ytds[ytd_idx];
    TextureEntry *tex = &ytd->textures[tex_idx];
    LOG("do_texture_remove: '%s' from '%s'", tex->name, ytd->name);

    int result = MessageBoxW(g_app.hwnd_main,
        trw(L"Are you sure you want to remove this texture?",
            L"Tem certeza de que deseja remover esta textura?",
            L"¿Seguro que desea eliminar esta textura?", L"Удалить эту текстуру?"),
        trw(L"Remove Texture", L"Remover textura", L"Eliminar textura", L"Удалить текстуру"),
        MB_YESNO | MB_ICONQUESTION);
    if (result != IDYES) return;

    char removed_name[EO_MAX_NAME];
    strncpy(removed_name, tex->name, EO_MAX_NAME);
    removed_name[EO_MAX_NAME - 1] = 0;

    free(tex->data);
    free(tex->orig_data);
    free(tex->wtd_meta);
    /* Shift remaining textures down */
    for (int i = tex_idx; i < ytd->texture_count - 1; i++) {
        ytd->textures[i] = ytd->textures[i + 1];
    }
    ytd->texture_count--;
    ytd->modified = true;

    gui_update_status("Removed '%s' from %s", removed_name, ytd->name);
    InvalidateRect(g_app.hwnd_content, NULL, TRUE);
    InvalidateRect(g_app.hwnd_sidebar, NULL, TRUE);
}

static void unload_rpf_archive(int ytd_idx) {
    if (ytd_idx < 0 || ytd_idx >= g_app.ytd_count || !g_app.ytds[ytd_idx]->from_rpf)
        return;

    char name[EO_MAX_NAME];
    YtdFile *parent = g_app.ytds[ytd_idx]->rpf_parent;
    strncpy(name, g_app.ytds[ytd_idx]->name, EO_MAX_NAME);
    name[EO_MAX_NAME - 1] = 0;
    ytd_free(g_app.ytds[ytd_idx]);
    for (int i = ytd_idx; i < g_app.ytd_count - 1; i++)
        g_app.ytds[i] = g_app.ytds[i + 1];
    g_app.ytd_count--;
    if (parent && parent->rpf_child_count > 0) parent->rpf_child_count--;
    gui_update_status("Unloaded '%s' from the application; original RPF was not changed", name);
    InvalidateRect(g_app.hwnd_content, NULL, TRUE);
    InvalidateRect(g_app.hwnd_sidebar, NULL, TRUE);
}

/* Unload a whole archive (regular file, RPF group + its entries, or one RPF
 * entry) from the workspace. Disk is never modified. */
static void do_unload_archive(int idx) {
    if (idx < 0 || idx >= g_app.ytd_count) return;
    YtdFile *a = g_app.ytds[idx];
    char name[EO_MAX_NAME];
    strncpy(name, a->name, EO_MAX_NAME);
    name[EO_MAX_NAME - 1] = 0;

    if (a->from_rpf) { unload_rpf_archive(idx); return; }

    if (a->is_rpf_group) {
        /* Free all entries that belong to this RPF group first. */
        for (int i = g_app.ytd_count - 1; i >= 0; i--) {
            if (g_app.ytds[i]->rpf_parent != a) continue;
            ytd_free(g_app.ytds[i]);
            for (int j = i; j < g_app.ytd_count - 1; j++) g_app.ytds[j] = g_app.ytds[j + 1];
            g_app.ytd_count--;
        }
        /* The group's index may have shifted; locate it again. */
        for (int i = 0; i < g_app.ytd_count; i++) {
            if (g_app.ytds[i] != a) continue;
            ytd_free(a);
            for (int j = i; j < g_app.ytd_count - 1; j++) g_app.ytds[j] = g_app.ytds[j + 1];
            g_app.ytd_count--;
            break;
        }
        gui_update_status("Unloaded RPF '%s' and its entries (disk untouched)", name);
    } else {
        if (a->is_preview) reset_migration_state();  /* a preview consolidated: drop pending too */
        ytd_free(a);
        for (int j = idx; j < g_app.ytd_count - 1; j++) g_app.ytds[j] = g_app.ytds[j + 1];
        g_app.ytd_count--;
        gui_update_status("Unloaded '%s' from the workspace (disk untouched)", name);
    }
    InvalidateRect(g_app.hwnd_content, NULL, TRUE);
    InvalidateRect(g_app.hwnd_sidebar, NULL, TRUE);
}

/* Find the archive whose header row (folder card or RPF entry row) is at my. */
static int hit_test_archive_header(int my, int area_w) {
    int y = 8;
    for (int i = 0; i < g_app.ytd_count; i++) {
        YtdFile *ytd = g_app.ytds[i];
        if (ytd->from_rpf) continue;
        if (my >= y && my < y + FOLDER_H) return i;
        y += FOLDER_H + 4;
        if (!ytd->expanded) continue;
        if (ytd->is_rpf_group) {
            for (int c = 0; c < g_app.ytd_count; c++) {
                YtdFile *child = g_app.ytds[c];
                if (child->rpf_parent != ytd) continue;
                if (my >= y && my < y + RPF_ENTRY_H) return c;
                y += RPF_ENTRY_H + 4;
                if (child->expanded) y += texture_grid_height(child, area_w);
            }
        } else {
            y += texture_grid_height(ytd, area_w);
        }
    }
    return -1;
}

static void show_archive_context_menu(HWND hwnd, int sx, int sy, int idx) {
    YtdFile *a = g_app.ytds[idx];
    HMENU m = CreatePopupMenu();
    const wchar_t *label =
        a->is_rpf_group ? trw(L"Unload RPF (remove from workspace)", L"Descarregar RPF (remover do workspace)",
                              L"Descargar RPF (quitar del espacio)", L"Выгрузить RPF (убрать из рабочей области)")
                        : trw(L"Unload (remove from workspace)", L"Descarregar (remover do workspace)",
                              L"Descargar (quitar del espacio)", L"Выгрузить (убрать из рабочей области)");
    AppendMenuW(m, MF_STRING, IDM_ARCH_UNLOAD, label);
    UINT cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, sx, sy, 0, hwnd, NULL);
    DestroyMenu(m);
    if (cmd == IDM_ARCH_UNLOAD) do_unload_archive(idx);
}

/* ── Content painting ──────────────────────────────────────────────── */

static int texture_grid_height(YtdFile *ytd, int area_w) {
    int cards_per_row = ((area_w - 24) + CARD_MARGIN) / (CARD_W + CARD_MARGIN);
    if (cards_per_row < 1) cards_per_row = 1;
    int visible = 0;
    for (int i = 0; i < ytd->texture_count; i++)
        if (texture_matches_search(&ytd->textures[i])) visible++;
    if (visible == 0) return 36;
    return ((visible + cards_per_row - 1) / cards_per_row) * (CARD_H + CARD_MARGIN) + 8;
}

static bool handle_archive_header_click(HWND hwnd, int mx, int my, int y,
                                        int area_w, int ytd_idx) {
    YtdFile *ytd = g_app.ytds[ytd_idx];
    if (my < y || my >= y + FOLDER_H) return false;
    if (ytd->is_preview) {
        int card_w = area_w - 16;
        int bl = 8 + card_w - 130, br = 8 + card_w - 44;
        if (mx >= bl && mx <= br && my >= y + 16 && my <= y + 40) {
            ytd->keep_originals = !ytd->keep_originals;
            gui_update_status("%s: %s originals on migrate",
                ytd->name, ytd->keep_originals ? "keeping" : "removing");
            InvalidateRect(hwnd, NULL, TRUE);
            return true;
        }
    } else if (ytd->from_rpf) {
        int br = area_w - 52, bl = br - 72;
        if (mx >= bl && mx <= br && my >= y + 16 && my <= y + 40) {
            unload_rpf_archive(ytd_idx);
            return true;
        }
    }
    ytd->expanded = !ytd->expanded;
    LOG("Archive '%s' %s (%d textures)", ytd->name,
        ytd->expanded ? "expanded" : "collapsed", ytd->texture_count);
    if (ytd->expanded) {
        RECT view_rc;
        GetClientRect(hwnd, &view_rc);
        int grid_top = y + FOLDER_H + 4;
        int target_scroll = grid_top - 12;
        int max_scroll = g_app.content_height - view_rc.bottom;
        if (max_scroll < 0) max_scroll = 0;
        if (target_scroll > max_scroll) target_scroll = max_scroll;
        if (target_scroll < 0) target_scroll = 0;
        if (target_scroll > g_app.scroll_y)
            g_app.scroll_y = target_scroll;
    }
    InvalidateRect(hwnd, NULL, TRUE);
    return true;
}

static void paint_texture_grid(HDC hdc, RECT *viewport, YtdFile *ytd,
                               int cards_per_row, int *io_y) {
    int visible = 0;
    for (int t = 0; t < ytd->texture_count; t++)
        if (texture_matches_search(&ytd->textures[t])) visible++;

    if (visible == 0) {
        RECT empty = {12, *io_y, viewport->right - 12, *io_y + 28};
        SetTextColor(hdc, CLR_TEXT_SECONDARY);
        SelectObject(hdc, theme_font_small());
        DrawTextW(hdc,
            ytd->texture_count > 0 ? L"No textures match the current search" : L"No previewable textures in this entry",
            -1, &empty, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        *io_y += 36;
        return;
    }

    int col = 0;
    for (int t = 0; t < ytd->texture_count; t++) {
        TextureEntry *tex = &ytd->textures[t];
        if (!texture_matches_search(tex)) continue;
        int x = 12 + col * (CARD_W + CARD_MARGIN);
        if (*io_y + CARD_H >= 0 && *io_y <= viewport->bottom)
            gui_draw_texture_card(hdc, x, *io_y, CARD_W, CARD_H, tex, ytd, false);
        if (++col >= cards_per_row) {
            col = 0;
            *io_y += CARD_H + CARD_MARGIN;
        }
    }
    if (col > 0) *io_y += CARD_H + CARD_MARGIN;
    *io_y += 8;
}

static void paint_content(HWND hwnd, HDC hdc) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    int area_w = rc.right - rc.left;

    HBRUSH bg = CreateSolidBrush(CLR_BG_DARK);
    FillRect(hdc, &rc, bg);
    DeleteObject(bg);

    if (g_app.ytd_count == 0) {
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, CLR_TEXT_SECONDARY);
        SelectObject(hdc, theme_font_title());
        RECT text_rc = rc;
        text_rc.top = rc.bottom / 2 - 20;
        DrawTextW(hdc, trw(L"Drop texture files here or click 'Add File'",
            L"Arraste arquivos de textura aqui ou clique em 'Adicionar arquivo'",
            L"Arrastre archivos de textura aquí o haga clic en 'Añadir archivo'",
            L"Перетащите файлы текстур сюда или нажмите 'Добавить файл'"), -1, &text_rc,
                  DT_CENTER | DT_SINGLELINE);
        return;
    }

    int y = 8 - g_app.scroll_y;
    int content_w = area_w - 24;
    int cards_per_row = (content_w + CARD_MARGIN) / (CARD_W + CARD_MARGIN);
    if (cards_per_row < 1) cards_per_row = 1;

    SetBkMode(hdc, TRANSPARENT);

    for (int i = 0; i < g_app.ytd_count; i++) {
        YtdFile *ytd = g_app.ytds[i];
        if (ytd->from_rpf) continue;

        /* Folder card */
        if (y + FOLDER_H >= 0 && y <= rc.bottom) {
            gui_draw_ytd_card(hdc, 8, y, area_w - 16, ytd,
                g_app.ytds, g_app.ytd_count, false);
        }
        y += FOLDER_H + 4;

        if (!ytd->expanded) continue;

        if (ytd->is_rpf_group) {
            for (int c = 0; c < g_app.ytd_count; c++) {
                YtdFile *child = g_app.ytds[c];
                if (child->rpf_parent != ytd) continue;
                if (y + RPF_ENTRY_H >= 0 && y <= rc.bottom)
                    gui_draw_rpf_entry_row(hdc, 20, y, area_w - 28, child);
                y += RPF_ENTRY_H + 4;
                if (child->expanded)
                    paint_texture_grid(hdc, &rc, child, cards_per_row, &y);
            }
        } else {
            paint_texture_grid(hdc, &rc, ytd, cards_per_row, &y);
        }
    }

    g_app.content_height = y + g_app.scroll_y;

    /* Clamp scroll to valid range; if the view was scrolled past the (now
     * shorter) content, snap back and repaint so cards don't render off-screen. */
    {
        int max_scroll = g_app.content_height - rc.bottom;
        if (max_scroll < 0) max_scroll = 0;
        if (g_app.scroll_y > max_scroll) {
            g_app.scroll_y = max_scroll;
            InvalidateRect(hwnd, NULL, FALSE);
        }
    }

    /* Update scrollbar */
    SCROLLINFO si = {sizeof(si)};
    si.fMask = SIF_RANGE | SIF_PAGE;
    si.nMin = 0;
    si.nMax = g_app.content_height;
    si.nPage = rc.bottom;
    SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
}

/* ── Sidebar painting ──────────────────────────────────────────────── */

static void paint_sidebar(HWND hwnd, HDC hdc) {
    RECT rc;
    GetClientRect(hwnd, &rc);

    HBRUSH bg = CreateSolidBrush(CLR_SURFACE_DARK);
    FillRect(hdc, &rc, bg);
    DeleteObject(bg);

    SetBkMode(hdc, TRANSPARENT);

    /* Draw sidebar buttons */
    for (int i = 0; i < (int)SIDEBAR_BTN_COUNT; i++) {
        SidebarButton *btn = &g_sidebar_btns[i];
        if (!btn->visible) continue;
        HBRUSH fill;
        if (btn->id == ID_SIDEBAR_ADDYTD || btn->id == ID_SIDEBAR_ADDFOLDER)
            fill = CreateSolidBrush(btn->hovered ? 0x00339933 : CLR_PRIMARY);
        else
            fill = CreateSolidBrush(btn->hovered ? CLR_HOVER : CLR_BUTTON_BG);

        HPEN pen = CreatePen(PS_SOLID, 1, CLR_BORDER_DARK);
        HPEN oldPen = (HPEN)SelectObject(hdc, pen);
        HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, fill);
        RoundRect(hdc, btn->rc.left, btn->rc.top, btn->rc.right, btn->rc.bottom, 8, 8);
        SelectObject(hdc, oldPen);
        SelectObject(hdc, oldBrush);
        DeleteObject(pen);
        DeleteObject(fill);

        SetTextColor(hdc, CLR_TEXT_PRIMARY);
        SelectObject(hdc, theme_font_small_bold());
        RECT txt_rc = btn->rc;
        DrawTextW(hdc, btn->text, -1, &txt_rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    /* File list below buttons */
    int visible_btns = 0;
    for (int i = 0; i < (int)SIDEBAR_BTN_COUNT; i++)
        if (g_sidebar_btns[i].visible) visible_btns++;
    int y = 16 + visible_btns * 44 + 16;
    if (g_app.ytd_count > 0) {
        /* Count top-level entries (RPF entries are listed under their group, not here). */
        int total_top = 0;
        for (int i = 0; i < g_app.ytd_count; i++)
            if (!g_app.ytds[i]->from_rpf) total_top++;

        SetTextColor(hdc, CLR_TEXT_SECONDARY);
        SelectObject(hdc, theme_font_small());
        wchar_t hdr[64];
        _snwprintf(hdr, 64, L"LOADED FILES (%d)", total_top);
        RECT label_rc = {12, y, SIDEBAR_WIDTH - 12, y + 20};
        DrawTextW(hdc, hdr, -1, &label_rc, DT_LEFT | DT_SINGLELINE);
        y += 24;

        SetTextColor(hdc, CLR_TEXT_PRIMARY);
        SelectObject(hdc, theme_font_small());
        int shown = 0;
        for (int i = 0; i < g_app.ytd_count; i++) {
            YtdFile *a = g_app.ytds[i];
            if (a->from_rpf) continue;   /* entries shown nested in the content view */

            /* Stop before running off the bottom; leave room for Sponsor btn + "+N more" */
            if (y + 18 > rc.bottom - 58) {
                int remaining = total_top - shown;
                if (remaining > 0) {
                    SetTextColor(hdc, CLR_TEXT_SECONDARY);
                    wchar_t more[48];
                    _snwprintf(more, 48, L"+ %d more...", remaining);
                    RECT more_rc = {16, y, SIDEBAR_WIDTH - 8, y + 18};
                    DrawTextW(hdc, more, -1, &more_rc, DT_LEFT | DT_SINGLELINE);
                }
                break;
            }

            wchar_t wname[256];
            MultiByteToWideChar(CP_UTF8, 0, a->name, -1, wname, 256);
            wchar_t line[300];
            if (a->is_rpf_group)
                _snwprintf(line, 300, L"%s (RPF)", wname);
            else
                _snwprintf(line, 300, L"%s (%d tex)", wname, a->texture_count);
            RECT item_rc = {16, y, SIDEBAR_WIDTH - 8, y + 18};
            DrawTextW(hdc, line, -1, &item_rc, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
            y += 20;
            shown++;
        }
    }

    /* ── Sponsor button — pinned to the bottom of the sidebar ──────── */
    {
        RECT btn = {8, rc.bottom - 40, SIDEBAR_WIDTH - 8, rc.bottom - 8};
        HBRUSH fill = CreateSolidBrush(RGB(0x1F, 0x6F, 0xEB));   /* GitHub-blue */
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(0x1A, 0x5E, 0xCC));
        HPEN oldP = (HPEN)SelectObject(hdc, pen);
        HBRUSH oldB = (HBRUSH)SelectObject(hdc, fill);
        RoundRect(hdc, btn.left, btn.top, btn.right, btn.bottom, 8, 8);
        SelectObject(hdc, oldP); SelectObject(hdc, oldB);
        DeleteObject(pen); DeleteObject(fill);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, CLR_TEXT_PRIMARY);
        SelectObject(hdc, theme_font_small_bold());
        DrawTextW(hdc, L"♥  Sponsor", -1, &btn, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
}

/* ── Header painting ───────────────────────────────────────────────── */

static void paint_header(HWND hwnd, HDC hdc) {
    RECT rc;
    GetClientRect(hwnd, &rc);

    HBRUSH bg = CreateSolidBrush(CLR_SURFACE_DARK);
    FillRect(hdc, &rc, bg);
    DeleteObject(bg);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, CLR_TEXT_PRIMARY);
    SelectObject(hdc, theme_font_title());
    RECT title_rc = {16, 0, 190, HEADER_HEIGHT};
    DrawTextW(hdc, L"EasyOptimizer-V", -1, &title_rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
}

/* ── Window procedures ─────────────────────────────────────────────── */

static LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_SIZE:
        layout_children();
        return 0;

    case WM_DROPFILES: {
        HDROP hDrop = (HDROP)wp;
        int count = DragQueryFileW(hDrop, 0xFFFFFFFF, NULL, 0);
        LOG("WM_DROPFILES: %d files", count);
        if (!select_import_types(hwnd)) {
            DragFinish(hDrop);
            return 0;
        }
        bool prev_bulk = g_bulk_add;
        if (count > 1) g_bulk_add = true;   /* dropping many: collapse */
        for (int i = 0; i < count; i++) {
            wchar_t path[MAX_PATH];
            DragQueryFileW(hDrop, i, path, MAX_PATH);
            if (PathMatchSpecW(path, L"*.ytd;*.wtd;*.ydr;*.yft;*.ydd;*.rpf"))
                gui_add_ytd(path);
        }
        g_bulk_add = prev_bulk;
        DragFinish(hDrop);
        return 0;
    }

    case WM_COMMAND:
        if (HIWORD(wp) == EN_CHANGE && LOWORD(wp) == ID_SEARCH_BOX) {
            wchar_t wbuf[256];
            GetWindowTextW(g_app.hwnd_search, wbuf, 256);
            WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, g_app.search_filter, 256, NULL, NULL);
            InvalidateRect(g_app.hwnd_content, NULL, TRUE);
        }
        return 0;

    case WM_DRAWITEM: {
        DRAWITEMSTRUCT *dis = (DRAWITEMSTRUCT *)lp;
        if (dis->hwndItem == g_app.hwnd_header)
            paint_header(dis->hwndItem, dis->hDC);
        return TRUE;
    }

    case WM_DESTROY:
        for (int i = 0; i < g_app.ytd_count; i++)
            ytd_free(g_app.ytds[i]);
        free(g_pending_removals);
        g_pending_removals = NULL;
        theme_cleanup();
        CoUninitialize();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static LRESULT CALLBACK ContentWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        /* Double buffer */
        RECT rc;
        GetClientRect(hwnd, &rc);
        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBM = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
        HBITMAP oldBM = (HBITMAP)SelectObject(memDC, memBM);

        paint_content(hwnd, memDC);

        BitBlt(hdc, 0, 0, rc.right, rc.bottom, memDC, 0, 0, SRCCOPY);
        SelectObject(memDC, oldBM);
        DeleteObject(memBM);
        DeleteDC(memDC);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_VSCROLL: {
        SCROLLINFO si = {sizeof(si), SIF_ALL};
        GetScrollInfo(hwnd, SB_VERT, &si);
        int old = si.nPos;
        switch (LOWORD(wp)) {
            case SB_LINEUP:   si.nPos -= 30; break;
            case SB_LINEDOWN: si.nPos += 30; break;
            case SB_PAGEUP:   si.nPos -= si.nPage; break;
            case SB_PAGEDOWN: si.nPos += si.nPage; break;
            case SB_THUMBTRACK: si.nPos = si.nTrackPos; break;
        }
        int vmax = si.nMax - (int)si.nPage;   /* may be negative when content fits */
        if (vmax < 0) vmax = 0;
        if (si.nPos > vmax) si.nPos = vmax;
        if (si.nPos < 0) si.nPos = 0;
        (void)old;
        g_app.scroll_y = si.nPos;
        si.fMask = SIF_POS;
        SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }

    case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(wp);
        g_app.scroll_y -= delta / 2;
        RECT wrc; GetClientRect(hwnd, &wrc);
        int wmax = g_app.content_height - wrc.bottom;
        if (wmax < 0) wmax = 0;
        if (g_app.scroll_y > wmax) g_app.scroll_y = wmax;
        if (g_app.scroll_y < 0) g_app.scroll_y = 0;
        SCROLLINFO si = {sizeof(si), SIF_POS};
        si.nPos = g_app.scroll_y;
        SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }

    case WM_LBUTTONDOWN: {
        int mx = GET_X_LPARAM(lp);
        int my = GET_Y_LPARAM(lp) + g_app.scroll_y;

        /* Check if clicking on the Edit button of a texture */
        int ytd_idx, tex_idx, cx, cy;
        if (hit_test_texture(mx, my, &ytd_idx, &tex_idx, &cx, &cy)) {
            /* Edit button rect is: x + card_w - 62, y + card_h - 32, x + card_w - 12, y + card_h - 12 */
            int bx = cx + CARD_W - 62;
            int by = cy + CARD_H - 32;
            int br = cx + CARD_W - 12;
            int bb = cy + CARD_H - 12;
            if (mx >= bx && mx <= br && my >= by && my <= bb) {
                do_custom_resize(ytd_idx, tex_idx);
                return 0;
            }
        }

        /* Check if clicking on a folder card to toggle expand */
        int y = 8;
        RECT crc;
        GetClientRect(hwnd, &crc);
        int area_w = crc.right;

        for (int i = 0; i < g_app.ytd_count; i++) {
            YtdFile *ytd = g_app.ytds[i];
            if (ytd->from_rpf) continue;
            if (handle_archive_header_click(hwnd, mx, my, y, area_w, i)) return 0;
            y += FOLDER_H + 4;
            if (!ytd->expanded) continue;
            if (ytd->is_rpf_group) {
                for (int c = 0; c < g_app.ytd_count; c++) {
                    YtdFile *child = g_app.ytds[c];
                    if (child->rpf_parent != ytd) continue;
                    if (my >= y && my < y + RPF_ENTRY_H) {
                        int unload_left = area_w - 116, unload_right = area_w - 56;
                        if (mx >= unload_left && mx <= unload_right &&
                            my >= y + 9 && my <= y + 33) {
                            unload_rpf_archive(c);
                        } else {
                            child->expanded = !child->expanded;
                            LOG("RPF entry '%s' %s (%d textures)", child->name,
                                child->expanded ? "expanded" : "collapsed", child->texture_count);
                            if (child->expanded) {
                                RECT view_rc;
                                GetClientRect(hwnd, &view_rc);
                                int grid_top = y + RPF_ENTRY_H + 4;
                                int target_scroll = grid_top - 12;
                                int max_scroll = g_app.content_height - view_rc.bottom;
                                if (max_scroll < 0) max_scroll = 0;
                                if (target_scroll > max_scroll) target_scroll = max_scroll;
                                if (target_scroll < 0) target_scroll = 0;
                                if (target_scroll > g_app.scroll_y)
                                    g_app.scroll_y = target_scroll;
                            }
                            InvalidateRect(hwnd, NULL, TRUE);
                        }
                        return 0;
                    }
                    y += RPF_ENTRY_H + 4;
                    if (child->expanded) y += texture_grid_height(child, area_w);
                }
            } else {
                y += texture_grid_height(ytd, area_w);
            }
        }
        return 0;
    }

    case WM_RBUTTONDOWN: {
        int mx = GET_X_LPARAM(lp);
        int my = GET_Y_LPARAM(lp) + g_app.scroll_y;
        int ytd_idx, tex_idx;
        POINT screen_pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ClientToScreen(hwnd, &screen_pt);
        if (hit_test_texture(mx, my, &ytd_idx, &tex_idx, NULL, NULL)) {
            show_texture_context_menu(hwnd, screen_pt.x, screen_pt.y, ytd_idx, tex_idx);
        } else {
            RECT crc; GetClientRect(hwnd, &crc);
            int ai = hit_test_archive_header(my, crc.right);
            if (ai >= 0)
                show_archive_context_menu(hwnd, screen_pt.x, screen_pt.y, ai);
        }
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static LRESULT CALLBACK SidebarWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        paint_sidebar(hwnd, hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_LBUTTONDOWN: {
        int mx = GET_X_LPARAM(lp);
        int my = GET_Y_LPARAM(lp);

        /* Sponsor button pinned to sidebar bottom */
        {
            RECT sb_rc;
            GetClientRect(hwnd, &sb_rc);
            RECT btn = {8, sb_rc.bottom - 40, SIDEBAR_WIDTH - 8, sb_rc.bottom - 8};
            POINT pt = {mx, my};
            if (PtInRect(&btn, pt)) {
                ShellExecuteW(NULL, L"open",
                    L"https://github.com/LN-Development/EasyOptimizer-V",
                    NULL, NULL, SW_SHOWNORMAL);
                return 0;
            }
        }

        for (int i = 0; i < (int)SIDEBAR_BTN_COUNT; i++) {
            if (!g_sidebar_btns[i].visible) continue;
            POINT pt = {mx, my};
            if (PtInRect(&g_sidebar_btns[i].rc, pt)) {
                switch (g_sidebar_btns[i].id) {
                    case ID_SIDEBAR_ADDYTD:   open_file_dialog(g_app.hwnd_main); break;
                    case ID_SIDEBAR_SAVEALL:  save_all(); break;
                    case ID_SIDEBAR_CLEARALL:
                        LOG("Clear all: freeing %d ytds", g_app.ytd_count);
                        for (int j = 0; j < g_app.ytd_count; j++) ytd_free(g_app.ytds[j]);
                        g_app.ytd_count = 0;
                        reset_migration_state();
                        gui_update_status("All files cleared");
                        InvalidateRect(g_app.hwnd_content, NULL, TRUE);
                        InvalidateRect(hwnd, NULL, TRUE);
                        break;
                    case ID_SIDEBAR_DETECT:  do_detect_duplicates(); break;
                    case ID_SIDEBAR_MIGRATE: do_migrate_duplicates(); break;
                    case ID_SIDEBAR_OPTIMIZE: do_smart_optimize(); break;
                    case ID_SIDEBAR_FASTRECOMP: choose_recompress_mode(); break;
                    case ID_SIDEBAR_ADDFOLDER:  open_folder_dialog(g_app.hwnd_main); break;
                    case ID_SIDEBAR_TOGGLE_ENC:
                        g_app.use_gpu_encoding = !g_app.use_gpu_encoding;
                        log_encoder_intent("Encoder toggled");
                        update_sidebar_labels();
                        InvalidateRect(hwnd, NULL, TRUE);
                        break;
                    case ID_SIDEBAR_LANGUAGE: select_language(); break;
                    case ID_SIDEBAR_SORT:
                        g_sort_mode = (ArchiveSortMode)((g_sort_mode + 1) % 8);
                        apply_archive_sort();
                        update_sidebar_labels();
                        InvalidateRect(g_app.hwnd_content, NULL, TRUE);
                        InvalidateRect(hwnd, NULL, TRUE);
                        break;
                    case ID_SIDEBAR_GRID:
                        g_grid_index = (g_grid_index + 1) % 3;   /* Small -> Medium -> Native */
                        g_app.scroll_y = 0;                      /* card sizes changed; reset view */
                        update_sidebar_labels();
                        InvalidateRect(g_app.hwnd_content, NULL, TRUE);
                        InvalidateRect(hwnd, NULL, TRUE);
                        break;
                }
                return 0;
            }
        }
        return 0;
    }

    case WM_MOUSEMOVE: {
        int mx = GET_X_LPARAM(lp);
        int my = GET_Y_LPARAM(lp);
        bool changed = false;
        for (int i = 0; i < (int)SIDEBAR_BTN_COUNT; i++) {
            POINT pt = {mx, my};
            bool h = (g_sidebar_btns[i].visible && PtInRect(&g_sidebar_btns[i].rc, pt)) ? true : false;
            if (h != g_sidebar_btns[i].hovered) { g_sidebar_btns[i].hovered = h; changed = true; }
        }
        if (changed) InvalidateRect(hwnd, NULL, FALSE);

        TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, hwnd, 0};
        TrackMouseEvent(&tme);
        return 0;
    }

    case WM_MOUSELEAVE:
        for (int i = 0; i < (int)SIDEBAR_BTN_COUNT; i++) g_sidebar_btns[i].hovered = false;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_ERASEBKGND:
        return 1;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
