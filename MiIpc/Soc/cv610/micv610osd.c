#include "micv610osd.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MI_CV610_OSD_HANDLE_BASE 0
#define MI_CV610_OSD_CANVAS_NUM 2
#define MI_CV610_OSD_MAX_WIDTH 640
#define MI_CV610_OSD_MIN_WIDTH 16
#define MI_CV610_OSD_MIN_HEIGHT 12

#define MI_CV610_OSD_COLOR_TRANSPARENT 0x0000u
#define MI_CV610_OSD_COLOR_BLACK       0x8000u
#define MI_CV610_OSD_COLOR_WHITE       0xffffu

static const uint8_t g_glyph_space[7] = {0, 0, 0, 0, 0, 0, 0};
static const uint8_t g_glyph_unknown[7] = {0x0e, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04};

static const uint8_t* cv610_osd_glyph(char c)
{
    static const uint8_t glyph_0[7] = {0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e};
    static const uint8_t glyph_1[7] = {0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e};
    static const uint8_t glyph_2[7] = {0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f};
    static const uint8_t glyph_3[7] = {0x1e, 0x01, 0x01, 0x0e, 0x01, 0x01, 0x1e};
    static const uint8_t glyph_4[7] = {0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02};
    static const uint8_t glyph_5[7] = {0x1f, 0x10, 0x10, 0x1e, 0x01, 0x01, 0x1e};
    static const uint8_t glyph_6[7] = {0x06, 0x08, 0x10, 0x1e, 0x11, 0x11, 0x0e};
    static const uint8_t glyph_7[7] = {0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08};
    static const uint8_t glyph_8[7] = {0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e};
    static const uint8_t glyph_9[7] = {0x0e, 0x11, 0x11, 0x0f, 0x01, 0x02, 0x0c};
    static const uint8_t glyph_a[7] = {0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11};
    static const uint8_t glyph_b[7] = {0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e};
    static const uint8_t glyph_c[7] = {0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e};
    static const uint8_t glyph_d[7] = {0x1e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1e};
    static const uint8_t glyph_e[7] = {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f};
    static const uint8_t glyph_f[7] = {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10};
    static const uint8_t glyph_g[7] = {0x0e, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0f};
    static const uint8_t glyph_h[7] = {0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11};
    static const uint8_t glyph_i[7] = {0x0e, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e};
    static const uint8_t glyph_j[7] = {0x07, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0c};
    static const uint8_t glyph_k[7] = {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11};
    static const uint8_t glyph_l[7] = {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f};
    static const uint8_t glyph_m[7] = {0x11, 0x1b, 0x15, 0x15, 0x11, 0x11, 0x11};
    static const uint8_t glyph_n[7] = {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11};
    static const uint8_t glyph_o[7] = {0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e};
    static const uint8_t glyph_p[7] = {0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10};
    static const uint8_t glyph_q[7] = {0x0e, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0d};
    static const uint8_t glyph_r[7] = {0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11};
    static const uint8_t glyph_s[7] = {0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e};
    static const uint8_t glyph_t[7] = {0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04};
    static const uint8_t glyph_u[7] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e};
    static const uint8_t glyph_v[7] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x0a, 0x04};
    static const uint8_t glyph_w[7] = {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0a};
    static const uint8_t glyph_x[7] = {0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11};
    static const uint8_t glyph_y[7] = {0x11, 0x11, 0x0a, 0x04, 0x04, 0x04, 0x04};
    static const uint8_t glyph_z[7] = {0x1f, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1f};
    static const uint8_t glyph_dash[7] = {0x00, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x00};
    static const uint8_t glyph_colon[7] = {0x00, 0x04, 0x04, 0x00, 0x04, 0x04, 0x00};
    static const uint8_t glyph_dot[7] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x0c};
    static const uint8_t glyph_slash[7] = {0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10};
    static const uint8_t glyph_under[7] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1f};

    if (c >= 'a' && c <= 'z') {
        c = (char)toupper((unsigned char)c);
    }

    switch (c) {
    case ' ': return g_glyph_space;
    case '0': return glyph_0;
    case '1': return glyph_1;
    case '2': return glyph_2;
    case '3': return glyph_3;
    case '4': return glyph_4;
    case '5': return glyph_5;
    case '6': return glyph_6;
    case '7': return glyph_7;
    case '8': return glyph_8;
    case '9': return glyph_9;
    case 'A': return glyph_a;
    case 'B': return glyph_b;
    case 'C': return glyph_c;
    case 'D': return glyph_d;
    case 'E': return glyph_e;
    case 'F': return glyph_f;
    case 'G': return glyph_g;
    case 'H': return glyph_h;
    case 'I': return glyph_i;
    case 'J': return glyph_j;
    case 'K': return glyph_k;
    case 'L': return glyph_l;
    case 'M': return glyph_m;
    case 'N': return glyph_n;
    case 'O': return glyph_o;
    case 'P': return glyph_p;
    case 'Q': return glyph_q;
    case 'R': return glyph_r;
    case 'S': return glyph_s;
    case 'T': return glyph_t;
    case 'U': return glyph_u;
    case 'V': return glyph_v;
    case 'W': return glyph_w;
    case 'X': return glyph_x;
    case 'Y': return glyph_y;
    case 'Z': return glyph_z;
    case '-': return glyph_dash;
    case ':': return glyph_colon;
    case '.': return glyph_dot;
    case '/': return glyph_slash;
    case '_': return glyph_under;
    default: return g_glyph_unknown;
    }
}

static int cv610_osd_align2(int value)
{
    if (value < 0) {
        value = 0;
    }
    return (value + 1) & ~1;
}

static int cv610_osd_clamp(int value, int min_value, int max_value)
{
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static int cv610_osd_strnlen(const char* text, int max_len)
{
    int len = 0;

    if (text == NULL || max_len <= 0) {
        return 0;
    }
    while (len < max_len && text[len] != '\0') {
        ++len;
    }
    return len;
}

static int cv610_osd_scale_from_font(int font_size, int video_height)
{
    if (font_size <= 0) {
        return video_height >= 360 ? 2 : 1;
    }
    if (font_size <= 12) {
        return 1;
    }
    if (font_size <= 20) {
        return 2;
    }
    if (font_size <= 28) {
        return 3;
    }
    return 4;
}

static mi_cv610_osd_entry_t* cv610_osd_get_entry(mi_cv610_osd_t* osd, int index)
{
    if (osd == NULL || index < 0 || index >= MI_CV610_OSD_MAX_ENTRIES) {
        return NULL;
    }
    return &osd->entry[index];
}

static void cv610_osd_default_entry(mi_cv610_osd_t* osd, mi_cv610_osd_entry_t* entry, int index)
{
    memset(entry, 0, sizeof(*entry));
    entry->index = index;
    entry->handle = (hi_rgn_handle)((osd != NULL ? osd->handle_base : MI_CV610_OSD_HANDLE_BASE) + index);
    entry->show = index == 0 ? 1 : 0;
    entry->timestamp_mode = index == 0 ? 1 : 0;
    entry->font_size = 0;
    entry->align = 0;
    entry->pos_x = index == 0 ? 16 : 16;
    entry->pos_y = index == 0 ? 16 : (16 + index * 32);
}

static void cv610_osd_make_time_text(char* dst, size_t dst_size)
{
    time_t now;
    struct tm tm_now;

    if (dst == NULL || dst_size == 0) {
        return;
    }

    now = time(NULL);
    if (localtime_r(&now, &tm_now) == NULL) {
        snprintf(dst, dst_size, "0000-00-00 00:00:00");
        return;
    }
    if (strftime(dst, dst_size, "%Y-%m-%d %H:%M:%S", &tm_now) == 0) {
        snprintf(dst, dst_size, "0000-00-00 00:00:00");
    }
}

static void cv610_osd_put_pixel(mi_cv610_osd_entry_t* entry, int x, int y, uint16_t color)
{
    if (entry == NULL || entry->pixels == NULL) {
        return;
    }
    if (x < 0 || y < 0 || x >= entry->bmp_width || y >= entry->bmp_height) {
        return;
    }
    entry->pixels[(size_t)y * (size_t)entry->bmp_width + (size_t)x] = color;
}

static void cv610_osd_fill_rect(mi_cv610_osd_entry_t* entry, int x, int y, int w, int h, uint16_t color)
{
    int yy;
    int xx;

    for (yy = 0; yy < h; ++yy) {
        for (xx = 0; xx < w; ++xx) {
            cv610_osd_put_pixel(entry, x + xx, y + yy, color);
        }
    }
}

static void cv610_osd_draw_glyph(mi_cv610_osd_entry_t* entry,
    const uint8_t* glyph,
    int x,
    int y,
    int scale,
    uint16_t color)
{
    int row;
    int col;

    if (glyph == NULL || scale <= 0) {
        return;
    }

    for (row = 0; row < 7; ++row) {
        for (col = 0; col < 5; ++col) {
            if ((glyph[row] & (uint8_t)(1u << (4 - col))) != 0) {
                cv610_osd_fill_rect(entry, x + col * scale, y + row * scale, scale, scale, color);
            }
        }
    }
}

static int cv610_osd_prepare_canvas(mi_cv610_osd_t* osd,
    mi_cv610_osd_entry_t* entry,
    const char* text,
    int* text_chars)
{
    int scale;
    int padding;
    int chars;
    int max_chars;
    int needed_width;
    int needed_height;
    size_t need_pixels;
    uint16_t* next_pixels;

    if (osd == NULL || entry == NULL || text == NULL || text_chars == NULL) {
        return -1;
    }

    scale = cv610_osd_scale_from_font(entry->font_size, osd->video_height);
    padding = scale * 2;
    chars = cv610_osd_strnlen(text, MI_CV610_OSD_MAX_TEXT - 1);
    max_chars = (MI_CV610_OSD_MAX_WIDTH - padding * 2) / (6 * scale);
    if (max_chars < 1) {
        max_chars = 1;
    }
    if (chars > max_chars) {
        chars = max_chars;
    }

    needed_width = padding * 2 + chars * 6 * scale;
    needed_height = padding * 2 + 7 * scale + scale;
    if (entry->forced_width > 0) {
        needed_width = entry->forced_width;
    }
    if (entry->forced_height > 0) {
        needed_height = entry->forced_height;
    }

    needed_width = cv610_osd_align2(cv610_osd_clamp(needed_width,
        MI_CV610_OSD_MIN_WIDTH, MI_CV610_OSD_MAX_WIDTH));
    needed_height = cv610_osd_align2(cv610_osd_clamp(needed_height,
        MI_CV610_OSD_MIN_HEIGHT, osd->video_height > 0 ? osd->video_height : 64));
    need_pixels = (size_t)needed_width * (size_t)needed_height;

    if (entry->pixel_capacity < need_pixels) {
        next_pixels = (uint16_t*)realloc(entry->pixels, need_pixels * sizeof(uint16_t));
        if (next_pixels == NULL) {
            return -1;
        }
        entry->pixels = next_pixels;
        entry->pixel_capacity = need_pixels;
    }

    entry->bmp_width = needed_width;
    entry->bmp_height = needed_height;
    memset(entry->pixels, 0, need_pixels * sizeof(uint16_t));
    *text_chars = chars;
    return 0;
}

static int cv610_osd_render(mi_cv610_osd_t* osd, mi_cv610_osd_entry_t* entry, const char* text)
{
    int scale;
    int padding;
    int chars;
    int text_width;
    int x;
    int y;
    int i;

    if (cv610_osd_prepare_canvas(osd, entry, text, &chars) != 0) {
        return -1;
    }

    scale = cv610_osd_scale_from_font(entry->font_size, osd->video_height);
    padding = scale * 2;
    text_width = chars * 6 * scale;
    x = padding;
    y = padding;
    if (entry->align == 1 && entry->bmp_width > text_width) {
        x = (entry->bmp_width - text_width) / 2;
    } else if (entry->align == 2 && entry->bmp_width > text_width + padding) {
        x = entry->bmp_width - text_width - padding;
    }

    for (i = 0; i < chars; ++i) {
        const uint8_t* glyph = cv610_osd_glyph(text[i]);
        int gx = x + i * 6 * scale;
        cv610_osd_draw_glyph(entry, glyph, gx + scale, y + scale, scale, MI_CV610_OSD_COLOR_BLACK);
        cv610_osd_draw_glyph(entry, glyph, gx, y, scale, MI_CV610_OSD_COLOR_WHITE);
    }

    return 0;
}

static void cv610_osd_destroy_entry_locked(mi_cv610_osd_t* osd, mi_cv610_osd_entry_t* entry)
{
    hi_mpp_chn chn;

    if (osd == NULL || entry == NULL) {
        return;
    }

    memset(&chn, 0, sizeof(chn));
    chn.mod_id = HI_ID_VENC;
    chn.dev_id = 0;
    chn.chn_id = osd->venc_chn;

    if (entry->attached) {
        (void)hi_mpi_rgn_detach_from_chn(entry->handle, &chn);
        entry->attached = 0;
    }
    if (entry->created) {
        (void)hi_mpi_rgn_destroy(entry->handle);
        entry->created = 0;
    }
}

static void cv610_osd_force_cleanup_handle_locked(mi_cv610_osd_t* osd, mi_cv610_osd_entry_t* entry)
{
    hi_mpp_chn chn;

    if (osd == NULL || entry == NULL) {
        return;
    }

    memset(&chn, 0, sizeof(chn));
    chn.mod_id = HI_ID_VENC;
    chn.dev_id = 0;
    chn.chn_id = osd->venc_chn;

    (void)hi_mpi_rgn_detach_from_chn(entry->handle, &chn);
    (void)hi_mpi_rgn_destroy(entry->handle);
    entry->attached = 0;
    entry->created = 0;
}

static int cv610_osd_create_entry_locked(mi_cv610_osd_t* osd, mi_cv610_osd_entry_t* entry)
{
    hi_rgn_attr attr;
    hi_rgn_chn_attr chn_attr;
    hi_mpp_chn chn;
    hi_bmp bmp;
    hi_s32 ret;

    if (osd == NULL || entry == NULL || entry->pixels == NULL) {
        return -1;
    }

    memset(&attr, 0, sizeof(attr));
    attr.type = HI_RGN_OVERLAY;
    attr.attr.overlay.pixel_format = HI_PIXEL_FORMAT_ARGB_1555;
    attr.attr.overlay.bg_color = 0;
    attr.attr.overlay.size.width = (hi_u32)entry->bmp_width;
    attr.attr.overlay.size.height = (hi_u32)entry->bmp_height;
    attr.attr.overlay.canvas_num = MI_CV610_OSD_CANVAS_NUM;

    ret = hi_mpi_rgn_create(entry->handle, &attr);
    if (ret != HI_SUCCESS) {
        hi_s32 first_ret = ret;

        cv610_osd_force_cleanup_handle_locked(osd, entry);
        ret = hi_mpi_rgn_create(entry->handle, &attr);
        if (ret == HI_SUCCESS) {
            printf("CV610 OSD recovered stale handle index=%d handle=%u first_ret=%#x\n",
                entry->index, entry->handle, first_ret);
        }
    }
    if (ret != HI_SUCCESS) {
        printf("CV610 OSD create failed index=%d handle=%u ret=%#x\n",
            entry->index, entry->handle, ret);
        return -1;
    }
    entry->created = 1;

    memset(&bmp, 0, sizeof(bmp));
    bmp.pixel_format = HI_PIXEL_FORMAT_ARGB_1555;
    bmp.width = (hi_u32)entry->bmp_width;
    bmp.height = (hi_u32)entry->bmp_height;
    bmp.data = entry->pixels;
    ret = hi_mpi_rgn_set_bmp(entry->handle, &bmp);
    if (ret != HI_SUCCESS) {
        printf("CV610 OSD set bmp failed index=%d ret=%#x\n", entry->index, ret);
        cv610_osd_destroy_entry_locked(osd, entry);
        return -1;
    }

    memset(&chn, 0, sizeof(chn));
    chn.mod_id = HI_ID_VENC;
    chn.dev_id = 0;
    chn.chn_id = osd->venc_chn;

    memset(&chn_attr, 0, sizeof(chn_attr));
    chn_attr.is_show = entry->show ? HI_TRUE : HI_FALSE;
    chn_attr.type = HI_RGN_OVERLAY;
    chn_attr.attr.overlay_chn.point.x = cv610_osd_align2(entry->pos_x);
    chn_attr.attr.overlay_chn.point.y = cv610_osd_align2(entry->pos_y);
    chn_attr.attr.overlay_chn.fg_alpha = 255;
    chn_attr.attr.overlay_chn.bg_alpha = 0;
    chn_attr.attr.overlay_chn.layer = (hi_u32)entry->index;
    chn_attr.attr.overlay_chn.qp_info.enable = HI_FALSE;
    chn_attr.attr.overlay_chn.dst = HI_RGN_ATTACH_JPEG_MAIN;

    ret = hi_mpi_rgn_attach_to_chn(entry->handle, &chn, &chn_attr);
    if (ret != HI_SUCCESS) {
        hi_s32 first_ret = ret;

        (void)hi_mpi_rgn_detach_from_chn(entry->handle, &chn);
        ret = hi_mpi_rgn_attach_to_chn(entry->handle, &chn, &chn_attr);
        if (ret == HI_SUCCESS) {
            printf("CV610 OSD recovered stale attach index=%d handle=%u chn=%d first_ret=%#x\n",
                entry->index, entry->handle, osd->venc_chn, first_ret);
        }
    }
    if (ret != HI_SUCCESS) {
        printf("CV610 OSD attach failed index=%d chn=%d ret=%#x\n",
            entry->index, osd->venc_chn, ret);
        cv610_osd_destroy_entry_locked(osd, entry);
        return -1;
    }
    entry->attached = 1;
    printf("CV610 OSD attached index=%d handle=%u chn=%d pos=%d,%d bmp=%dx%d show=%d time=%d\n",
        entry->index,
        entry->handle,
        osd->venc_chn,
        entry->pos_x,
        entry->pos_y,
        entry->bmp_width,
        entry->bmp_height,
        entry->show,
        entry->timestamp_mode);
    fflush(stdout);
    return 0;
}

static int cv610_osd_update_entry_locked(mi_cv610_osd_t* osd, mi_cv610_osd_entry_t* entry)
{
    char time_text[32];
    const char* text;
    hi_bmp bmp;
    hi_s32 ret;
    int old_width;
    int old_height;

    if (osd == NULL || entry == NULL || !osd->started) {
        return -1;
    }

    if (!entry->show) {
        return 0;
    }

    if (entry->timestamp_mode) {
        cv610_osd_make_time_text(time_text, sizeof(time_text));
        text = time_text;
    } else {
        text = entry->text;
    }
    if (text == NULL || text[0] == '\0') {
        return 0;
    }

    old_width = entry->bmp_width;
    old_height = entry->bmp_height;
    if (cv610_osd_render(osd, entry, text) != 0) {
        printf("CV610 OSD render failed index=%d\n", entry->index);
        return -1;
    }

    if (!entry->created || old_width != entry->bmp_width || old_height != entry->bmp_height) {
        cv610_osd_destroy_entry_locked(osd, entry);
        return cv610_osd_create_entry_locked(osd, entry);
    }

    memset(&bmp, 0, sizeof(bmp));
    bmp.pixel_format = HI_PIXEL_FORMAT_ARGB_1555;
    bmp.width = (hi_u32)entry->bmp_width;
    bmp.height = (hi_u32)entry->bmp_height;
    bmp.data = entry->pixels;
    ret = hi_mpi_rgn_set_bmp(entry->handle, &bmp);
    if (ret != HI_SUCCESS) {
        printf("CV610 OSD update bmp failed index=%d ret=%#x\n", entry->index, ret);
        return -1;
    }
    return 0;
}

static void cv610_osd_lock(mi_cv610_osd_t* osd)
{
    if (osd != NULL && osd->lock_ready) {
        pthread_mutex_lock(&osd->lock);
    }
}

static void cv610_osd_unlock(mi_cv610_osd_t* osd)
{
    if (osd != NULL && osd->lock_ready) {
        pthread_mutex_unlock(&osd->lock);
    }
}

void mi_cv610_osd_init(mi_cv610_osd_t* osd)
{
    mi_cv610_osd_init_ex(osd, MI_CV610_OSD_HANDLE_BASE);
}

void mi_cv610_osd_init_ex(mi_cv610_osd_t* osd, int handle_base)
{
    int i;

    if (osd == NULL) {
        return;
    }

    memset(osd, 0, sizeof(*osd));
    if (pthread_mutex_init(&osd->lock, NULL) == 0) {
        osd->lock_ready = 1;
    }
    osd->handle_base = handle_base >= 0 ? handle_base : MI_CV610_OSD_HANDLE_BASE;
    osd->venc_chn = 0;
    osd->video_width = 1920;
    osd->video_height = 1080;
    for (i = 0; i < MI_CV610_OSD_MAX_ENTRIES; ++i) {
        cv610_osd_default_entry(osd, &osd->entry[i], i);
    }
}

void mi_cv610_osd_deinit(mi_cv610_osd_t* osd)
{
    int i;

    if (osd == NULL) {
        return;
    }
    mi_cv610_osd_stop(osd);
    for (i = 0; i < MI_CV610_OSD_MAX_ENTRIES; ++i) {
        free(osd->entry[i].pixels);
        osd->entry[i].pixels = NULL;
        osd->entry[i].pixel_capacity = 0;
    }
    if (osd->lock_ready) {
        pthread_mutex_destroy(&osd->lock);
        osd->lock_ready = 0;
    }
}

int mi_cv610_osd_start(mi_cv610_osd_t* osd, int venc_chn, int video_width, int video_height)
{
    int ret = 0;
    int i;

    if (osd == NULL) {
        return -1;
    }

    cv610_osd_lock(osd);
    osd->venc_chn = venc_chn;
    osd->video_width = video_width > 0 ? video_width : 1920;
    osd->video_height = video_height > 0 ? video_height : 1080;
    osd->started = 1;
    osd->last_update_sec = 0;
    if (osd->entry[0].text[0] == '\0') {
        osd->entry[0].timestamp_mode = 1;
        osd->entry[0].show = 1;
    }
    for (i = 0; i < MI_CV610_OSD_MAX_ENTRIES; ++i) {
        if (osd->entry[i].show && cv610_osd_update_entry_locked(osd, &osd->entry[i]) != 0) {
            ret = -1;
        }
    }
    cv610_osd_unlock(osd);

    printf("CV610 OSD start %s chn=%d size=%dx%d ret=%d\n",
        ret == 0 ? "ok" : "failed",
        venc_chn,
        video_width,
        video_height,
        ret);
    fflush(stdout);
    return ret;
}

void mi_cv610_osd_stop(mi_cv610_osd_t* osd)
{
    int i;

    if (osd == NULL) {
        return;
    }

    cv610_osd_lock(osd);
    for (i = 0; i < MI_CV610_OSD_MAX_ENTRIES; ++i) {
        cv610_osd_destroy_entry_locked(osd, &osd->entry[i]);
    }
    osd->started = 0;
    cv610_osd_unlock(osd);
}

void mi_cv610_osd_tick(mi_cv610_osd_t* osd)
{
    time_t now;
    int i;

    if (osd == NULL || !osd->started) {
        return;
    }

    now = time(NULL);
    cv610_osd_lock(osd);
    if (now == osd->last_update_sec) {
        cv610_osd_unlock(osd);
        return;
    }
    osd->last_update_sec = now;
    for (i = 0; i < MI_CV610_OSD_MAX_ENTRIES; ++i) {
        if (osd->entry[i].show && osd->entry[i].timestamp_mode) {
            (void)cv610_osd_update_entry_locked(osd, &osd->entry[i]);
        }
    }
    cv610_osd_unlock(osd);
}

void mi_cv610_osd_update(mi_cv610_osd_t* osd, int index)
{
    mi_cv610_osd_entry_t* entry;

    entry = cv610_osd_get_entry(osd, index);
    if (entry == NULL) {
        return;
    }
    cv610_osd_lock(osd);
    (void)cv610_osd_update_entry_locked(osd, entry);
    cv610_osd_unlock(osd);
}

int mi_cv610_osd_set_show(mi_cv610_osd_t* osd, int index, int show)
{
    mi_cv610_osd_entry_t* entry;
    hi_rgn_chn_attr chn_attr;
    hi_mpp_chn chn;
    hi_s32 ret;
    int result = 0;

    entry = cv610_osd_get_entry(osd, index);
    if (entry == NULL) {
        return -1;
    }

    cv610_osd_lock(osd);
    entry->show = show ? 1 : 0;
    if (entry->created && entry->attached) {
        memset(&chn, 0, sizeof(chn));
        chn.mod_id = HI_ID_VENC;
        chn.dev_id = 0;
        chn.chn_id = osd->venc_chn;

        memset(&chn_attr, 0, sizeof(chn_attr));
        chn_attr.is_show = entry->show ? HI_TRUE : HI_FALSE;
        chn_attr.type = HI_RGN_OVERLAY;
        chn_attr.attr.overlay_chn.point.x = cv610_osd_align2(entry->pos_x);
        chn_attr.attr.overlay_chn.point.y = cv610_osd_align2(entry->pos_y);
        chn_attr.attr.overlay_chn.fg_alpha = 255;
        chn_attr.attr.overlay_chn.bg_alpha = 0;
        chn_attr.attr.overlay_chn.layer = (hi_u32)entry->index;
        chn_attr.attr.overlay_chn.qp_info.enable = HI_FALSE;
        chn_attr.attr.overlay_chn.dst = HI_RGN_ATTACH_JPEG_MAIN;
        ret = hi_mpi_rgn_set_chn_display_attr(entry->handle, &chn, &chn_attr);
        if (ret != HI_SUCCESS) {
            result = -1;
        }
    } else if (entry->show && osd->started) {
        result = cv610_osd_update_entry_locked(osd, entry);
    }
    cv610_osd_unlock(osd);
    return result;
}

int mi_cv610_osd_get_show(mi_cv610_osd_t* osd, int index)
{
    mi_cv610_osd_entry_t* entry;
    int show;

    entry = cv610_osd_get_entry(osd, index);
    if (entry == NULL) {
        return 0;
    }

    cv610_osd_lock(osd);
    show = entry->show;
    cv610_osd_unlock(osd);
    return show;
}

int mi_cv610_osd_set_text(mi_cv610_osd_t* osd, int index, const char* text)
{
    mi_cv610_osd_entry_t* entry;
    int result = 0;

    entry = cv610_osd_get_entry(osd, index);
    if (entry == NULL) {
        return -1;
    }

    cv610_osd_lock(osd);
    if (text == NULL || text[0] == '\0') {
        entry->text[0] = '\0';
        entry->timestamp_mode = index == 0 ? 1 : 0;
    } else {
        snprintf(entry->text, sizeof(entry->text), "%s", text);
        entry->timestamp_mode = 0;
        entry->show = 1;
    }
    if (osd->started && entry->show) {
        result = cv610_osd_update_entry_locked(osd, entry);
    }
    cv610_osd_unlock(osd);
    return result;
}

int mi_cv610_osd_set_font_size(mi_cv610_osd_t* osd, int index, int font_size)
{
    mi_cv610_osd_entry_t* entry;
    int result = 0;

    entry = cv610_osd_get_entry(osd, index);
    if (entry == NULL) {
        return -1;
    }

    cv610_osd_lock(osd);
    entry->font_size = cv610_osd_clamp(font_size, 0, 36);
    if (osd->started && entry->show) {
        result = cv610_osd_update_entry_locked(osd, entry);
    }
    cv610_osd_unlock(osd);
    return result;
}

int mi_cv610_osd_get_font_size(mi_cv610_osd_t* osd, int index)
{
    mi_cv610_osd_entry_t* entry;
    int font_size;

    entry = cv610_osd_get_entry(osd, index);
    if (entry == NULL) {
        return 0;
    }
    cv610_osd_lock(osd);
    font_size = entry->font_size;
    cv610_osd_unlock(osd);
    return font_size;
}

int mi_cv610_osd_reset_size(mi_cv610_osd_t* osd, int index, int pos_x, int pos_y, int width, int height)
{
    mi_cv610_osd_entry_t* entry;
    int result = 0;

    entry = cv610_osd_get_entry(osd, index);
    if (entry == NULL) {
        return -1;
    }

    cv610_osd_lock(osd);
    entry->pos_x = cv610_osd_align2(pos_x);
    entry->pos_y = cv610_osd_align2(pos_y);
    entry->forced_width = width > 0 ? cv610_osd_align2(width) : 0;
    entry->forced_height = height > 0 ? cv610_osd_align2(height) : 0;
    if (entry->forced_width > MI_CV610_OSD_MAX_WIDTH) {
        entry->forced_width = MI_CV610_OSD_MAX_WIDTH;
    }
    if (osd->started && entry->show) {
        result = cv610_osd_update_entry_locked(osd, entry);
    }
    cv610_osd_unlock(osd);
    return result;
}

int mi_cv610_osd_set_align(mi_cv610_osd_t* osd, int index, int align)
{
    mi_cv610_osd_entry_t* entry;
    int result = 0;

    entry = cv610_osd_get_entry(osd, index);
    if (entry == NULL) {
        return -1;
    }

    cv610_osd_lock(osd);
    entry->align = cv610_osd_clamp(align, 0, 2);
    if (osd->started && entry->show) {
        result = cv610_osd_update_entry_locked(osd, entry);
    }
    cv610_osd_unlock(osd);
    return result;
}
