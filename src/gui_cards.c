#include "gui.h"
#include "theme.h"
#include "texture.h"
#include "image.h"
#include <stdio.h>

static size_t gui_archive_total_size(const YtdFile *archive) {
    size_t total = 0;
    if (!archive || !archive->textures) return 0;
    for (int i = 0; i < archive->texture_count; i++)
        total += archive->textures[i].data_size;
    return total;
}

static size_t gui_rpf_total_size(const YtdFile *group, YtdFile **all_archives, int archive_count) {
    size_t total = 0;
    if (!group || !all_archives) return 0;
    for (int i = 0; i < archive_count; i++) {
        YtdFile *child = all_archives[i];
        if (child && child->rpf_parent == group)
            total += gui_archive_total_size(child);
    }
    return total;
}

/* ── YTD Folder Card ───────────────────────────────────────────────── */

void gui_draw_ytd_card(HDC hdc, int x, int y, int w, YtdFile *ytd,
                       YtdFile **all_archives, int archive_count, bool hovered) {
    RECT rc = {x, y, x + w, y + 56};

    HBRUSH fill = CreateSolidBrush(hovered ? CLR_HOVER : CLR_SURFACE_DARK);
    COLORREF border_clr = ytd->is_preview ? RGB(230, 160, 30)
                                          : (ytd->expanded ? CLR_PRIMARY : CLR_BORDER_DARK);
    HPEN pen = CreatePen(PS_SOLID, ytd->is_preview ? 2 : 1, border_clr);
    HPEN oldPen = (HPEN)SelectObject(hdc, pen);
    HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, fill);
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 12, 12);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(pen);
    DeleteObject(fill);

    /* Folder icon (simple colored rectangle) */
    COLORREF icon_clr = CLR_PRIMARY;
    HBRUSH icon_brush = CreateSolidBrush(icon_clr);
    RECT icon_rc = {x + 16, y + 18, x + 48, y + 44};
    FillRect(hdc, &icon_rc, icon_brush);
    RECT tab_rc = {x + 16, y + 12, x + 32, y + 20};
    FillRect(hdc, &tab_rc, icon_brush);
    DeleteObject(icon_brush);

    /* Name */
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, CLR_TEXT_PRIMARY);
    SelectObject(hdc, theme_font_title());

    wchar_t wname[256];
    MultiByteToWideChar(CP_UTF8, 0, ytd->name, -1, wname, 256);
    RECT name_rc = {x + 60, y + 10, x + w - 40, y + 30};
    DrawTextW(hdc, wname, -1, &name_rc, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);

    /* Info line */
    size_t total_size = ytd->is_rpf_group
        ? gui_rpf_total_size(ytd, all_archives, archive_count)
        : gui_archive_total_size(ytd);
    double total_mib = total_size / (1024.0 * 1024.0);

    wchar_t info[128];
    if (ytd->is_rpf_group)
        _snwprintf(info, 128, L"RPF archive | %d files | %.2f MiB total | expand to retrieve list",
            ytd->rpf_child_count, total_mib);
    else if (ytd->is_preview)
        _snwprintf(info, 128, L"PREVIEW | %d textures | %.2f MiB", ytd->texture_count, total_mib);
    else
        _snwprintf(info, 128, L"%d textures | %.2f MiB", ytd->texture_count, total_mib);

    SetTextColor(hdc, theme_archive_size_color(total_mib));
    SelectObject(hdc, theme_font_small());
    RECT info_rc = {x + 60, y + 30, x + w - 40, y + 48};
    DrawTextW(hdc, info, -1, &info_rc, DT_LEFT | DT_SINGLELINE);

    /* Preview consolidated YTDs get a "Maintain" toggle to keep originals. */
    if (ytd->is_preview) {
        RECT btn = {x + w - 130, y + 16, x + w - 44, y + 40};
        bool on = ytd->keep_originals;
        HBRUSH bfill = CreateSolidBrush(on ? RGB(0x16, 0xA3, 0x4A) : RGB(60, 60, 60));
        HPEN bpen = CreatePen(PS_SOLID, 1, on ? RGB(0x4A, 0xDE, 0x80) : CLR_BORDER_DARK);
        HPEN oldP = (HPEN)SelectObject(hdc, bpen);
        HBRUSH oldB = (HBRUSH)SelectObject(hdc, bfill);
        RoundRect(hdc, btn.left, btn.top, btn.right, btn.bottom, 6, 6);
        SelectObject(hdc, oldP);
        SelectObject(hdc, oldB);
        DeleteObject(bpen);
        DeleteObject(bfill);
        SetTextColor(hdc, CLR_TEXT_PRIMARY);
        SelectObject(hdc, theme_font_small_bold());
        DrawTextW(hdc, on ? L"Keeping" : L"Maintain", -1, &btn,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    } else if (ytd->from_rpf) {
        RECT btn = {x + w - 116, y + 16, x + w - 44, y + 40};
        HBRUSH bfill = CreateSolidBrush(RGB(120, 45, 45));
        HPEN bpen = CreatePen(PS_SOLID, 1, RGB(210, 90, 90));
        HPEN oldP = (HPEN)SelectObject(hdc, bpen);
        HBRUSH oldB = (HBRUSH)SelectObject(hdc, bfill);
        RoundRect(hdc, btn.left, btn.top, btn.right, btn.bottom, 6, 6);
        SelectObject(hdc, oldP);
        SelectObject(hdc, oldB);
        DeleteObject(bpen);
        DeleteObject(bfill);
        SetTextColor(hdc, CLR_TEXT_PRIMARY);
        SelectObject(hdc, theme_font_small_bold());
        DrawTextW(hdc, L"Unload", -1, &btn, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    /* Expand arrow */
    SetTextColor(hdc, CLR_TEXT_PRIMARY);
    SelectObject(hdc, theme_font_title());
    const wchar_t *arrow = ytd->expanded ? L"\x25BC" : L"\x25B6";
    RECT arrow_rc = {x + w - 30, y + 16, x + w - 8, y + 40};
    DrawTextW(hdc, arrow, -1, &arrow_rc, DT_CENTER | DT_SINGLELINE);
}

void gui_draw_rpf_entry_row(HDC hdc, int x, int y, int w, YtdFile *ytd) {
    RECT rc = {x, y, x + w, y + RPF_ENTRY_H};
    HBRUSH fill = CreateSolidBrush(RGB(30, 34, 39));
    HPEN pen = CreatePen(PS_SOLID, 1, CLR_BORDER_DARK);
    HPEN old_pen = (HPEN)SelectObject(hdc, pen);
    HBRUSH old_brush = (HBRUSH)SelectObject(hdc, fill);
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 8, 8);
    SelectObject(hdc, old_pen);
    SelectObject(hdc, old_brush);
    DeleteObject(pen);
    DeleteObject(fill);

    wchar_t name[EO_MAX_NAME];
    MultiByteToWideChar(CP_UTF8, 0, ytd->name, -1, name, EO_MAX_NAME);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, CLR_TEXT_PRIMARY);
    SelectObject(hdc, theme_font_small_bold());
    RECT name_rc = {x + 14, y + 6, x + w - 126, y + 22};
    DrawTextW(hdc, name, -1, &name_rc, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);

    size_t total_size = gui_archive_total_size(ytd);
    double total_mib = total_size / (1024.0 * 1024.0);
    wchar_t info[128];
    if (!ytd->textures)
        _snwprintf(info, 128, L"listed only | preview unavailable");
    else
        _snwprintf(info, 128, L"%d textures | %.2f MiB | read-only from RPF",
            ytd->texture_count, total_mib);
    SetTextColor(hdc, CLR_TEXT_SECONDARY);
    SelectObject(hdc, theme_font_small());
    RECT info_rc = {x + 14, y + 22, x + w - 126, y + 38};
    DrawTextW(hdc, info, -1, &info_rc, DT_LEFT | DT_SINGLELINE);

    RECT unload = {x + w - 108, y + 9, x + w - 48, y + 33};
    HBRUSH unload_fill = CreateSolidBrush(RGB(120, 45, 45));
    FillRect(hdc, &unload, unload_fill);
    DeleteObject(unload_fill);
    SetTextColor(hdc, CLR_TEXT_PRIMARY);
    SelectObject(hdc, theme_font_small_bold());
    DrawTextW(hdc, L"Unload", -1, &unload, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    SetTextColor(hdc, CLR_TEXT_PRIMARY);
    SelectObject(hdc, theme_font_title());
    RECT arrow = {x + w - 36, y + 10, x + w - 8, y + 34};
    DrawTextW(hdc, ytd->expanded ? L"\x25BC" : L"\x25B6", -1, &arrow,
              DT_CENTER | DT_SINGLELINE);
}

/* ── Texture Card ──────────────────────────────────────────────────── */

void gui_draw_texture_card(HDC hdc, int x, int y, int card_w, int card_h,
                           TextureEntry *tex, YtdFile *parent, bool hovered) {
    RECT rc = {x, y, x + card_w, y + card_h};

    /* Card background */
    HBRUSH fill = CreateSolidBrush(CLR_SURFACE_DARK);
    HPEN pen = CreatePen(PS_SOLID, 1, hovered ? CLR_PRIMARY : CLR_BORDER_DARK);
    HPEN oldPen = (HPEN)SelectObject(hdc, pen);
    HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, fill);
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 12, 12);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(pen);
    DeleteObject(fill);

    int footer_h = 80;
    int img_h = card_h - footer_h;

    /* Image area (black background) */
    RECT img_rc = {x + 1, y + 1, x + card_w - 1, y + img_h};
    HBRUSH black = CreateSolidBrush(RGB(0, 0, 0));
    FillRect(hdc, &img_rc, black);
    DeleteObject(black);

    /* Decode and draw texture preview */
    int tw = 0, th = 0;
    uint8_t *pixels = tex_decode_to_bgra(tex, 0, &tw, &th);
    if (pixels && tw > 0 && th > 0) {
        HBITMAP hbm = image_create_bitmap(pixels, tw, th);
        if (hbm) {
            HDC memDC = CreateCompatibleDC(hdc);
            HBITMAP oldBM = (HBITMAP)SelectObject(memDC, hbm);

            int avail_w = card_w - 2;
            int avail_h = img_h - 2;
            float ratio_w = (float)avail_w / tw;
            float ratio_h = (float)avail_h / th;
            float ratio = ratio_w < ratio_h ? ratio_w : ratio_h;
            int dw = (int)(tw * ratio);
            int dh = (int)(th * ratio);
            int dx = x + 1 + (avail_w - dw) / 2;
            int dy = y + 1 + (avail_h - dh) / 2;

            SetStretchBltMode(hdc, HALFTONE);
            StretchBlt(hdc, dx, dy, dw, dh, memDC, 0, 0, tw, th, SRCCOPY);

            SelectObject(memDC, oldBM);
            DeleteObject(hbm);
            DeleteDC(memDC);
        }
        free(pixels);
    }

    /* Format badge */
    SetBkMode(hdc, TRANSPARENT);
    const char *fmt_str = tex_format_name(tex->format);
    char short_fmt[64];
    if (strncmp(fmt_str, "D3DFMT_", 7) == 0)
        strncpy(short_fmt, fmt_str + 7, 64);
    else
        strncpy(short_fmt, fmt_str, 64);

    wchar_t wfmt[64];
    MultiByteToWideChar(CP_UTF8, 0, short_fmt, -1, wfmt, 64);

    SelectObject(hdc, theme_font_mono());
    SIZE fmtSize;
    GetTextExtentPoint32W(hdc, wfmt, (int)wcslen(wfmt), &fmtSize);

    RECT badge_rc = {x + card_w - fmtSize.cx - 16, y + 6,
                     x + card_w - 6, y + 6 + fmtSize.cy + 4};
    HBRUSH badge_bg = CreateSolidBrush(RGB(0, 0, 0));
    FillRect(hdc, &badge_rc, badge_bg);
    DeleteObject(badge_bg);
    SetTextColor(hdc, CLR_TEXT_PRIMARY);
    DrawTextW(hdc, wfmt, -1, &badge_rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    /* Footer: name, size, dimensions, mips */
    int text_y = y + img_h + 10;
    int text_x = x + 12;

    /* Name */
    wchar_t wname[256];
    MultiByteToWideChar(CP_UTF8, 0, tex->name, -1, wname, 256);
    SetTextColor(hdc, CLR_TEXT_PRIMARY);
    SelectObject(hdc, theme_font_title());
    RECT name_rc = {text_x, text_y, x + card_w - 80, text_y + 20};
    DrawTextW(hdc, wname, -1, &name_rc, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);

    /* Size */
    double mib = tex->data_size / (1024.0 * 1024.0);
    wchar_t size_str[32];
    _snwprintf(size_str, 32, L"%.2f MiB", mib);
    SetTextColor(hdc, theme_size_color(mib));
    SelectObject(hdc, theme_font_small_bold());
    RECT size_rc = {x + card_w - 80, text_y, x + card_w - 10, text_y + 20};
    DrawTextW(hdc, size_str, -1, &size_rc, DT_RIGHT | DT_SINGLELINE);

    /* Meta info */
    text_y += 22;
    SetTextColor(hdc, CLR_TEXT_SECONDARY);
    SelectObject(hdc, theme_font_small());

    wchar_t meta[128];
    _snwprintf(meta, 128, L"%d x %d", tex->width, tex->height);
    RECT meta_rc = {text_x, text_y, x + card_w - 12, text_y + 14};
    DrawTextW(hdc, meta, -1, &meta_rc, DT_LEFT | DT_SINGLELINE);

    text_y += 14;
    wchar_t mips_str[32];
    _snwprintf(mips_str, 32, L"Mips: %d", tex->mip_count);
    RECT mips_rc = {text_x, text_y, x + card_w - 12, text_y + 14};
    DrawTextW(hdc, mips_str, -1, &mips_rc, DT_LEFT | DT_SINGLELINE);

    /* Parent YTD name */
    text_y += 14;
    wchar_t wparent[256];
    MultiByteToWideChar(CP_UTF8, 0, parent->name, -1, wparent, 256);
    RECT parent_rc = {text_x, text_y, x + card_w - 12, text_y + 14};
    DrawTextW(hdc, wparent, -1, &parent_rc, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);

    /* Edit Button (mimic old tool) */
    RECT edit_rc = {x + card_w - 62, y + card_h - 32, x + card_w - 12, y + card_h - 12};
    HBRUSH edit_bg = CreateSolidBrush(RGB(60, 60, 60)); // Standard button bg
    RoundRect(hdc, edit_rc.left, edit_rc.top, edit_rc.right, edit_rc.bottom, 4, 4);
    FillRect(hdc, &edit_rc, edit_bg);
    DeleteObject(edit_bg);
    
    SetTextColor(hdc, RGB(255, 255, 255));
    SelectObject(hdc, theme_font_small());
    DrawTextW(hdc, L"Edit", -1, &edit_rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}
