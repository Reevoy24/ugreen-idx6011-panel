#include "gui.h"
#include "i18n.h"
#include "leds.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include <dirent.h>
#include <sys/stat.h>
#include "lvgl/lvgl.h"
#include "lvgl/src/draw/lv_image_decoder_private.h"
#include "lvgl/src/draw/lv_draw_buf_private.h"

/* ---- Layout ---- */
#define DISP_W 258
#define DISP_H 960
#define PAGE_PAD 10
#define CARD_W (DISP_W - 2 * PAGE_PAD)
#define BAR_HEIGHT 6
#define SPARK_POINTS 36
#define PANEL_H 484
#define EDGE_STRIP_H 32
#define SWIPE_TRIGGER 55

#define WP_DIR "/usr/share/ug-paneld/wallpapers"
#define WP_CUSTOM_PATH "/etc/ug-paneld/wallpaper.png"
#define WP_MAX_OPTS 7

/* ---- UGOS-inspired palette ---- */
#define COL_BG      0x0a0a0c
#define COL_CARD    0x1b1b1e
#define COL_PANEL   0x141416
#define COL_BTN     0x26262b
#define COL_TEXT    0xf2f2f2
#define COL_SUB     0x8e8e93
#define COL_TRACK   0x2c2c2e
#define COL_CYAN    0x35d1e2
#define COL_YELLOW  0xffd231
#define COL_GREEN   0x34c759
#define COL_RED     0xff453a
#define COL_DOT_OFF 0x48484c
/* black @ opa 120 blended over COL_BG — used when no wallpaper is set, so
 * the glass tiles can be drawn as cheap opaque fills with the same look */
#define COL_GLASS_SOLID 0x050506

#define HOME_TILE_W ((CARD_W - 12) / 2)
#define HOME_TILE_H 156
#define HOME_RING   212
#define GLASS_OPA   120
#define GLASS_RADIUS 16

/* ---- Pages ---- */
#define MAX_PAGES 6
static gui_setup_t setup;
static ui_state_t fallback_state;
static lv_obj_t *tileview = NULL;
static lv_obj_t *tiles[MAX_PAGES];
static lv_obj_t *dots[MAX_PAGES];
static lv_obj_t *dots_box = NULL;
static int page_count = 0;

/* ---- i18n label registry (static labels that re-translate live) ---- */
#define TR_REG_MAX 72
static struct { lv_obj_t *obj; tr_key_t key; } tr_regs[TR_REG_MAX];
static int tr_reg_count = 0;

/* ---- Home ---- */
static uint8_t *wp_buf = NULL;
static lv_image_dsc_t wp_dsc;
static lv_obj_t *wp_img = NULL;
static lv_obj_t *time_label = NULL;
static lv_obj_t *date_label = NULL;
static lv_obj_t *home_cpu_canvas = NULL; /* pre-rendered CPU ring */
static int home_ring_pct = -1;
static uint8_t cpu_ring_buf[HOME_RING * HOME_RING * 4];
static lv_obj_t *glass_tiles[8];         /* tiles baked into the wallpaper */
static int glass_tile_count = 0;
static lv_obj_t *home_cpu_val = NULL;
static lv_obj_t *home_ram_val = NULL;
static lv_obj_t *home_ram_bar = NULL;
static lv_obj_t *home_ram_sub = NULL;
static lv_obj_t *home_temp_val = NULL;
static lv_obj_t *home_temp_bar = NULL;
static lv_obj_t *home_temp_sub = NULL;
static lv_obj_t *home_sys_val = NULL;
static lv_obj_t *home_sys_bar = NULL;
static lv_obj_t *home_sys_sub = NULL;
static lv_obj_t *home_net_rx = NULL;
static lv_obj_t *home_net_tx = NULL;
static lv_obj_t *home_up_val = NULL;

/* ---- Hardware ---- */
static lv_obj_t *hw_cpu_val = NULL;
static lv_obj_t *hw_cpu_chart = NULL;
static lv_chart_series_t *hw_cpu_ser = NULL;
static lv_obj_t *hw_temp_val = NULL;
static lv_obj_t *hw_temp_bar = NULL;
static lv_obj_t *hw_ram_val = NULL;
static lv_obj_t *hw_ram_bar = NULL;
static lv_obj_t *hw_gpu_val = NULL;
static lv_obj_t *hw_gpu_sub = NULL;
static lv_obj_t *hw_gpu_chart = NULL;
static lv_chart_series_t *hw_gpu_ser = NULL;
static lv_obj_t *hw_up_val = NULL;

/* ---- Netzwerk ---- */
static lv_obj_t *net_dl_val = NULL;
static lv_obj_t *net_ul_val = NULL;
static lv_obj_t *net_card[NET_MAX_IFACES];
static lv_obj_t *net_name[NET_MAX_IFACES];
static lv_obj_t *net_status[NET_MAX_IFACES];
static lv_obj_t *net_ip4[NET_MAX_IFACES];
static lv_obj_t *net_ip6[NET_MAX_IFACES];
static lv_obj_t *net_rate[NET_MAX_IFACES];

/* ---- Festplatten ---- */
static lv_obj_t *disk_card[DISK_MAX];
static lv_obj_t *disk_name[DISK_MAX];
static lv_obj_t *disk_status[DISK_MAX];
static lv_obj_t *disk_size_val[DISK_MAX];
static lv_obj_t *disk_temp_val[DISK_MAX];
static lv_obj_t *disk_empty = NULL;
static disk_stats_t last_disks;
static int have_last_disks = 0;

/* ---- Proxmox ---- */
static lv_obj_t *pve_vm_val = NULL;
static lv_obj_t *pve_lxc_val = NULL;
static lv_obj_t *pve_row[PVE_MAX_GUESTS];
static lv_obj_t *pve_dot[PVE_MAX_GUESTS];
static lv_obj_t *pve_name[PVE_MAX_GUESTS];
static lv_obj_t *pve_id[PVE_MAX_GUESTS];
static lv_obj_t *pve_hint = NULL;

/* ---- OPNsense ---- */
static lv_obj_t *wan_dl_arc = NULL;
static lv_obj_t *wan_ul_arc = NULL;
static lv_obj_t *wan_dl_label = NULL;
static lv_obj_t *wan_ul_label = NULL;
static lv_obj_t *gw_val_label = NULL;
static lv_obj_t *update_val_label = NULL;
static lv_obj_t *dhcp_val_label = NULL;
static lv_obj_t *dns_val_label = NULL;
static lv_obj_t *dns_bar = NULL;

/* ---- Settings panel ---- */
static lv_obj_t *edge_strip = NULL;
static lv_obj_t *scrim = NULL;
static lv_obj_t *panel = NULL;
static int panel_is_open = 0;
static lv_obj_t *bri_val_label = NULL;
static lv_obj_t *bri_slider = NULL;
static lv_obj_t *timeout_sub = NULL;
static lv_obj_t *wp_sub = NULL;
static lv_obj_t *lang_sub = NULL;
static lv_obj_t *led_sub = NULL;
static lv_obj_t *led_night_sub = NULL;
static int panel_h = PANEL_H; /* grows when the LED rows are shown */
static lv_obj_t *confirm_scrim = NULL;
static lv_obj_t *confirm_text = NULL;
static int confirm_is_shutdown = 0;
static lv_point_t press_start;
static lv_obj_t *sleep_overlay = NULL;

static char wp_opts[WP_MAX_OPTS][20];
static int wp_opt_count = 0;
static int wp_cur = 0;

static const int timeout_presets[] = { 60, 300, 900, 1800, 0 };
#define TIMEOUT_PRESET_COUNT 5

/* ================= helpers ================= */

static void fmt_bps(float bps, char *buf, size_t n)
{
    if (bps < 0) bps = 0;
    if (bps < 1000.0f)            snprintf(buf, n, "%.0f B/s", bps);
    else if (bps < 1000000.0f)    snprintf(buf, n, "%.1f KB/s", bps / 1000.0f);
    else if (bps < 1000000000.0f) snprintf(buf, n, "%.1f MB/s", bps / 1000000.0f);
    else                          snprintf(buf, n, "%.2f GB/s", bps / 1000000000.0f);
}

static lv_obj_t *label_new(lv_obj_t *parent, const char *text,
                           const lv_font_t *font, uint32_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    return l;
}

/* translated label that updates on language change */
static lv_obj_t *label_tr(lv_obj_t *parent, tr_key_t key,
                          const lv_font_t *font, uint32_t color)
{
    lv_obj_t *l = label_new(parent, tr(key), font, color);
    if (tr_reg_count < TR_REG_MAX) {
        tr_regs[tr_reg_count].obj = l;
        tr_regs[tr_reg_count].key = key;
        tr_reg_count++;
    }
    return l;
}

static lv_obj_t *row_new(lv_obj_t *parent)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);
    return row;
}

static lv_obj_t *vcol_new(lv_obj_t *parent, int pct)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_set_size(c, LV_PCT(pct), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_opa(c, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_pad_all(c, 0, 0);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_CLICKABLE);
    return c;
}

static lv_obj_t *card_new(lv_obj_t *parent)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, CARD_W, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(card, lv_color_hex(COL_CARD), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 12, 0);
    lv_obj_set_style_pad_row(card, 5, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_CLICKABLE);
    return card;
}

static lv_obj_t *bar_new(lv_obj_t *parent, uint32_t color)
{
    lv_obj_t *bar = lv_bar_create(parent);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);
    lv_obj_set_size(bar, LV_PCT(100), BAR_HEIGHT);
    lv_obj_set_style_bg_color(bar, lv_color_hex(COL_TRACK), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, lv_color_hex(color), LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 3, 0);
    return bar;
}

static lv_obj_t *spark_new(lv_obj_t *parent, uint32_t color, lv_chart_series_t **ser_out)
{
    lv_obj_t *chart = lv_chart_create(parent);
    lv_obj_set_size(chart, LV_PCT(100), 46);
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(chart, SPARK_POINTS);
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    lv_chart_set_div_line_count(chart, 0, 0);
    lv_chart_set_update_mode(chart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_obj_set_style_bg_opa(chart, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(chart, 0, 0);
    lv_obj_set_style_pad_all(chart, 0, 0);
    lv_obj_set_style_line_width(chart, 2, LV_PART_ITEMS);
    lv_obj_set_style_size(chart, 0, 0, LV_PART_INDICATOR);
    lv_obj_remove_flag(chart, LV_OBJ_FLAG_CLICKABLE);
    *ser_out = lv_chart_add_series(chart, lv_color_hex(color), LV_CHART_AXIS_PRIMARY_Y);
    for (int i = 0; i < SPARK_POINTS; i++)
        lv_chart_set_next_value(chart, *ser_out, 0);
    return chart;
}

static lv_obj_t *dot_new(lv_obj_t *parent, uint32_t color, int size)
{
    lv_obj_t *d = lv_obj_create(parent);
    lv_obj_set_size(d, size, size);
    lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(d, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(d, 0, 0);
    lv_obj_remove_flag(d, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(d, LV_OBJ_FLAG_CLICKABLE);
    return d;
}

static lv_obj_t *page_new(int col, tr_key_t title_key)
{
    lv_obj_t *tile = lv_tileview_add_tile(tileview, col, 0, LV_DIR_HOR);
    tiles[col] = tile;
    lv_obj_set_style_bg_color(tile, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);

    lv_obj_t *cont = lv_obj_create(tile);
    lv_obj_set_size(cont, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, PAGE_PAD, 0);
    lv_obj_set_style_pad_row(cont, 8, 0);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(cont, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *hdr = row_new(cont);
    lv_obj_set_style_pad_top(hdr, 4, 0);
    lv_obj_set_style_pad_bottom(hdr, 2, 0);
    label_tr(hdr, title_key, &lv_font_montserrat_20, COL_TEXT);
    return cont;
}

static void update_dots(void)
{
    if (!tileview || !dots_box) return;
    lv_obj_t *active = lv_tileview_get_tile_active(tileview);
    for (int i = 0; i < page_count; i++) {
        if (!dots[i]) continue;
        lv_obj_set_style_bg_color(dots[i],
            lv_color_hex(tiles[i] == active ? 0xffffff : COL_DOT_OFF), 0);
    }
}

static void tile_changed_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    update_dots();
}

/* ================= wallpaper ================= */

/* defined with the home page below */
static void bake_glass_into_wallpaper(void);
static void glass_set_baked(int baked);

static int file_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

/* decode + cover-scale + center-crop into a fresh ARGB8888 buffer */
static uint8_t *decode_wallpaper(const char *fs_path)
{
    char lv_path[256];
    snprintf(lv_path, sizeof(lv_path), "A:%s", fs_path);

    lv_image_header_t header;
    if (lv_image_decoder_get_info(lv_path, &header) != LV_RESULT_OK) {
        fprintf(stderr, "Wallpaper: cannot read %s\n", fs_path);
        return NULL;
    }
    lv_image_decoder_dsc_t dsc;
    if (lv_image_decoder_open(&dsc, lv_path, NULL) != LV_RESULT_OK) {
        fprintf(stderr, "Wallpaper: decode failed for %s\n", fs_path);
        return NULL;
    }

    uint8_t *out = NULL;
    uint32_t src_w = header.w, src_h = header.h;
    const uint8_t *src_data = dsc.decoded ? dsc.decoded->data : NULL;
    uint32_t src_stride = dsc.decoded ? dsc.decoded->header.stride : 0;
    uint32_t bpp = dsc.decoded ? LV_COLOR_FORMAT_GET_BPP(dsc.decoded->header.cf) / 8 : 0;
    uint32_t dst_stride = DISP_W * 4;

    if (src_data && (bpp == 3 || bpp == 4) && src_w && src_h)
        out = malloc(dst_stride * DISP_H);

    if (out) {
        float scale_w = (float)DISP_W / src_w;
        float scale_h = (float)DISP_H / src_h;
        float scale = scale_w > scale_h ? scale_w : scale_h;
        uint32_t scaled_w = (uint32_t)(src_w * scale + 0.5f);
        uint32_t scaled_h = (uint32_t)(src_h * scale + 0.5f);
        int off_x = ((int)scaled_w - DISP_W) / 2;
        int off_y = ((int)scaled_h - DISP_H) / 2;

        for (int y = 0; y < DISP_H; y++) {
            uint32_t sy = (uint32_t)((y + off_y) / scale);
            if (sy >= src_h) sy = src_h - 1;
            for (int x = 0; x < DISP_W; x++) {
                uint32_t sx = (uint32_t)((x + off_x) / scale);
                if (sx >= src_w) sx = src_w - 1;
                const uint8_t *sp = src_data + sy * src_stride + sx * bpp;
                uint8_t *dp = out + y * dst_stride + x * 4;
                /* alpha is forced opaque — the buffer is presented as
                 * XRGB8888 so the blit is a copy, not a per-pixel blend */
                dp[0] = sp[0]; dp[1] = sp[1]; dp[2] = sp[2]; dp[3] = 0xFF;
            }
        }
    }
    lv_image_decoder_close(&dsc);
    return out;
}

static const char *wp_display_name(const char *opt)
{
    if (strcmp(opt, "none") == 0) return tr(TR_NONE);
    if (strcmp(opt, "custom") == 0) return tr(TR_CUSTOM);
    return opt;
}

static void apply_wallpaper(const char *name)
{
    if (!wp_img) return;

    if (strcmp(name, "none") == 0) {
        lv_obj_add_flag(wp_img, LV_OBJ_FLAG_HIDDEN);
        glass_set_baked(0); /* opaque tiles over the solid background */
        return;
    }

    char path[160];
    if (strcmp(name, "custom") == 0)
        snprintf(path, sizeof(path), "%s", WP_CUSTOM_PATH);
    else
        snprintf(path, sizeof(path), "%s/%.32s.png", WP_DIR, name);

    uint8_t *nbuf = decode_wallpaper(path);
    if (!nbuf) {
        lv_obj_add_flag(wp_img, LV_OBJ_FLAG_HIDDEN);
        glass_set_baked(0);
        return;
    }

    lv_image_set_src(wp_img, NULL);
    lv_image_cache_drop(&wp_dsc);
    if (wp_buf) free(wp_buf);
    wp_buf = nbuf;

    bake_glass_into_wallpaper();
    glass_set_baked(1);

    wp_dsc.header.w = DISP_W;
    wp_dsc.header.h = DISP_H;
    wp_dsc.header.cf = LV_COLOR_FORMAT_XRGB8888; /* opaque: fast copy blit */
    wp_dsc.header.stride = DISP_W * 4;
    wp_dsc.data_size = (uint32_t)(DISP_W * 4) * DISP_H;
    wp_dsc.data = wp_buf;

    lv_image_set_src(wp_img, &wp_dsc);
    lv_obj_remove_flag(wp_img, LV_OBJ_FLAG_HIDDEN);
    lv_obj_invalidate(wp_img);
}

static void build_wp_options(void)
{
    wp_opt_count = 0;
    snprintf(wp_opts[wp_opt_count++], sizeof(wp_opts[0]), "none");

    struct dirent **list = NULL;
    int n = scandir(WP_DIR, &list, NULL, alphasort);
    for (int i = 0; i < n; i++) {
        const char *d = list[i]->d_name;
        size_t len = strlen(d);
        if (len > 4 && strcasecmp(d + len - 4, ".png") == 0 &&
            wp_opt_count < WP_MAX_OPTS - 1) {
            size_t base = len - 4;
            if (base > sizeof(wp_opts[0]) - 1) base = sizeof(wp_opts[0]) - 1;
            memcpy(wp_opts[wp_opt_count], d, base);
            wp_opts[wp_opt_count][base] = '\0';
            wp_opt_count++;
        }
        free(list[i]);
    }
    if (n >= 0) free(list);

    if (file_exists(WP_CUSTOM_PATH) && wp_opt_count < WP_MAX_OPTS)
        snprintf(wp_opts[wp_opt_count++], sizeof(wp_opts[0]), "custom");

    /* resolve startup choice; "" = legacy auto (custom file if present) */
    const char *want = setup.state->wallpaper;
    if (!want[0])
        want = file_exists(WP_CUSTOM_PATH) ? "custom" : "none";
    wp_cur = 0;
    for (int i = 0; i < wp_opt_count; i++)
        if (strcmp(wp_opts[i], want) == 0) { wp_cur = i; break; }
    snprintf(setup.state->wallpaper, sizeof(setup.state->wallpaper), "%s",
             wp_opts[wp_cur]);
}

/* ================= pages ================= */

/* translucent "glass" tile for the home page (sits on the wallpaper).
 * The translucent look is NOT rendered live: with a wallpaper the darkening
 * is baked into the wallpaper bitmap (bake_glass_into_wallpaper) and the
 * tile background stays transparent; without one the tile is an opaque fill
 * of the equivalent blended color. Both avoid per-frame alpha blending,
 * which made swipes over the home page expensive. */
static lv_obj_t *glass_new(lv_obj_t *parent, int w)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_set_size(c, w, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(c, lv_color_hex(COL_GLASS_SOLID), 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(c, GLASS_RADIUS, 0);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_pad_all(c, 14, 0);
    lv_obj_set_style_pad_row(c, 8, 0);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_CLICKABLE);
    if (glass_tile_count < (int)(sizeof(glass_tiles) / sizeof(glass_tiles[0])))
        glass_tiles[glass_tile_count++] = c;
    return c;
}

/* glass tiles: transparent over a baked wallpaper / opaque solid without */
static void glass_set_baked(int baked)
{
    for (int i = 0; i < glass_tile_count; i++)
        lv_obj_set_style_bg_opa(glass_tiles[i],
                                baked ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
}

/* Burn the tiles' darkening into the wallpaper bitmap. Identical math to
 * LVGL's blend (dst * (255-opa) / 255 with black src), incl. a 1px soft
 * edge on the rounded corners — but it runs once per wallpaper switch
 * instead of on every frame. */
static void bake_glass_into_wallpaper(void)
{
    if (!wp_buf || glass_tile_count == 0 || !tiles[0]) return;

    lv_obj_update_layout(tiles[0]);

    lv_area_t base;
    lv_obj_get_coords(tiles[0], &base);

    for (int t = 0; t < glass_tile_count; t++) {
        lv_area_t a;
        lv_obj_get_coords(glass_tiles[t], &a);
        int x0 = a.x1 - base.x1;
        int y0 = a.y1 - base.y1;
        int w = lv_area_get_width(&a);
        int h = lv_area_get_height(&a);
        const float r = (float)GLASS_RADIUS;

        for (int y = 0; y < h; y++) {
            int py = y0 + y;
            if (py < 0 || py >= DISP_H) continue;
            uint8_t *line = wp_buf + (size_t)py * DISP_W * 4;
            for (int x = 0; x < w; x++) {
                int px = x0 + x;
                if (px < 0 || px >= DISP_W) continue;

                float dx = 0.0f, dy = 0.0f;
                if (x + 0.5f < r)          dx = r - (x + 0.5f);
                else if (x + 0.5f > w - r) dx = (x + 0.5f) - (w - r);
                if (y + 0.5f < r)          dy = r - (y + 0.5f);
                else if (y + 0.5f > h - r) dy = (y + 0.5f) - (h - r);

                float cov = 1.0f;
                if (dx > 0.0f && dy > 0.0f) {
                    float d = sqrtf(dx * dx + dy * dy);
                    cov = r - d + 0.5f;
                    if (cov <= 0.0f) continue;   /* outside the corner */
                    if (cov > 1.0f) cov = 1.0f;
                }

                int keep = 255 - (int)(GLASS_OPA * cov + 0.5f);
                uint8_t *p = line + (size_t)px * 4;
                p[0] = (uint8_t)((p[0] * keep) / 255);
                p[1] = (uint8_t)((p[1] * keep) / 255);
                p[2] = (uint8_t)((p[2] * keep) / 255);
            }
        }
    }
}

/* Pre-render the CPU ring into a canvas. Rasterizing a 212px anti-aliased
 * arc on every frame was the single most expensive widget during swipes;
 * the cached image blits cheaply. Redrawn only when the percentage moves. */
static void home_ring_draw(int pct)
{
    if (!home_cpu_canvas) return;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    if (pct == home_ring_pct) return;
    home_ring_pct = pct;

    lv_canvas_fill_bg(home_cpu_canvas, lv_color_black(), LV_OPA_TRANSP);

    lv_layer_t layer;
    lv_canvas_init_layer(home_cpu_canvas, &layer);

    lv_draw_arc_dsc_t a;
    lv_draw_arc_dsc_init(&a);
    a.center.x = HOME_RING / 2;
    a.center.y = HOME_RING / 2;
    a.radius = HOME_RING / 2;
    a.width = 12;
    a.opa = LV_OPA_COVER;
    a.color = lv_color_hex(COL_TRACK);
    a.start_angle = 0;
    a.end_angle = 360;
    lv_draw_arc(&layer, &a);

    if (pct > 0) {
        a.color = lv_color_hex(COL_CYAN);
        a.rounded = 1;
        a.start_angle = 270;
        a.end_angle = 270 + (pct * 360) / 100;
        lv_draw_arc(&layer, &a);
    }

    lv_canvas_finish_layer(home_cpu_canvas, &layer);
}

/* small "● Caption" header row inside a glass tile */
static void glass_head(lv_obj_t *tile, uint32_t dot_color, const char *text,
                       tr_key_t tr_key, int use_tr)
{
    lv_obj_t *row = lv_obj_create(tile);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, 6, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);
    dot_new(row, dot_color, 7);
    if (use_tr)
        label_tr(row, tr_key, &lv_font_montserrat_14, COL_SUB);
    else
        label_new(row, text, &lv_font_montserrat_14, COL_SUB);
}

static void build_home(int col)
{
    lv_obj_t *tile = lv_tileview_add_tile(tileview, col, 0, LV_DIR_HOR);
    tiles[col] = tile;
    lv_obj_set_style_bg_color(tile, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);

    wp_img = lv_image_create(tile);
    lv_obj_set_pos(wp_img, 0, 0);
    lv_obj_add_flag(wp_img, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *cont = lv_obj_create(tile);
    lv_obj_set_size(cont, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, PAGE_PAD, 0);
    lv_obj_set_style_pad_row(cont, 16, 0);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_remove_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

    date_label = label_new(cont, "", &lv_font_montserrat_16, 0xd8d8d8);
    lv_obj_set_style_pad_top(date_label, 22, 0);

    time_label = label_new(cont, "00:00", &lv_font_montserrat_48, 0xffffff);

    /* big CPU ring */
    lv_obj_t *cpu_card = glass_new(cont, CARD_W);
    lv_obj_set_flex_align(cpu_card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_margin_top(cpu_card, 4, 0);

    lv_obj_t *arc_wrap = lv_obj_create(cpu_card);
    lv_obj_set_size(arc_wrap, HOME_RING, HOME_RING);
    lv_obj_set_style_bg_opa(arc_wrap, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(arc_wrap, 0, 0);
    lv_obj_set_style_pad_all(arc_wrap, 0, 0);
    lv_obj_remove_flag(arc_wrap, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(arc_wrap, LV_OBJ_FLAG_CLICKABLE);

    home_cpu_canvas = lv_canvas_create(arc_wrap);
    lv_canvas_set_buffer(home_cpu_canvas, cpu_ring_buf, HOME_RING, HOME_RING,
                         LV_COLOR_FORMAT_ARGB8888);
    lv_obj_center(home_cpu_canvas);
    lv_obj_remove_flag(home_cpu_canvas, LV_OBJ_FLAG_CLICKABLE);
    home_ring_pct = -1;
    home_ring_draw(0);

    home_cpu_val = label_new(arc_wrap, "0 %", &lv_font_montserrat_32, COL_TEXT);
    lv_obj_align(home_cpu_val, LV_ALIGN_CENTER, 0, -12);
    lv_obj_t *cpu_cap = label_new(arc_wrap, "CPU", &lv_font_montserrat_14, COL_SUB);
    lv_obj_align(cpu_cap, LV_ALIGN_CENTER, 0, 22);

    /* RAM | temperature tiles */
    lv_obj_t *row = row_new(cont);
    lv_obj_set_width(row, CARD_W);

    lv_obj_t *t = glass_new(row, HOME_TILE_W);
    lv_obj_set_height(t, HOME_TILE_H);
    glass_head(t, COL_YELLOW, "RAM", 0, 0);
    home_ram_val = label_new(t, "-- %", &lv_font_montserrat_32, COL_TEXT);
    home_ram_bar = bar_new(t, COL_YELLOW);
    home_ram_sub = label_new(t, "--", &lv_font_montserrat_14, COL_SUB);

    t = glass_new(row, HOME_TILE_W);
    lv_obj_set_height(t, HOME_TILE_H);
    glass_head(t, COL_RED, NULL, TR_TEMP_SHORT, 1);
    home_temp_val = label_new(t, "-- °C", &lv_font_montserrat_32, COL_TEXT);
    home_temp_bar = bar_new(t, COL_RED);
    home_temp_sub = label_new(t, "CPU", &lv_font_montserrat_14, COL_SUB);

    /* network | system tiles */
    row = row_new(cont);
    lv_obj_set_width(row, CARD_W);

    t = glass_new(row, HOME_TILE_W);
    lv_obj_set_height(t, HOME_TILE_H);
    glass_head(t, COL_CYAN, NULL, TR_NETWORK, 1);
    home_net_rx = label_new(t, LV_SYMBOL_DOWN " --", &lv_font_montserrat_16, COL_TEXT);
    lv_obj_set_style_pad_top(home_net_rx, 6, 0);
    home_net_tx = label_new(t, LV_SYMBOL_UP " --", &lv_font_montserrat_16, COL_TEXT);

    t = glass_new(row, HOME_TILE_W);
    lv_obj_set_height(t, HOME_TILE_H);
    glass_head(t, COL_GREEN, "System", 0, 0);
    home_sys_val = label_new(t, "-- %", &lv_font_montserrat_32, COL_TEXT);
    home_sys_bar = bar_new(t, COL_GREEN);
    home_sys_sub = label_new(t, "--", &lv_font_montserrat_14, COL_SUB);

    /* uptime strip */
    lv_obj_t *up_card = glass_new(cont, CARD_W);
    lv_obj_t *up_row = row_new(up_card);
    label_tr(up_row, TR_UPTIME, &lv_font_montserrat_14, COL_SUB);
    home_up_val = label_new(up_row, "--", &lv_font_montserrat_16, COL_TEXT);
}

static void build_hardware(int col)
{
    lv_obj_t *cont = page_new(col, TR_HARDWARE);

    lv_obj_t *card = card_new(cont);
    label_new(card, "CPU", &lv_font_montserrat_14, COL_SUB);
    hw_cpu_val = label_new(card, "0 %", &lv_font_montserrat_32, COL_TEXT);
    label_tr(card, TR_LOAD, &lv_font_montserrat_14, COL_SUB);
    hw_cpu_chart = spark_new(card, COL_CYAN, &hw_cpu_ser);

    card = card_new(cont);
    label_tr(card, TR_TEMPERATURE, &lv_font_montserrat_14, COL_SUB);
    hw_temp_val = label_new(card, "--,- °C", &lv_font_montserrat_32, COL_TEXT);
    hw_temp_bar = bar_new(card, COL_YELLOW);

    card = card_new(cont);
    label_tr(card, TR_MEMORY, &lv_font_montserrat_14, COL_SUB);
    hw_ram_val = label_new(card, "0,0 / 0,0 GB", &lv_font_montserrat_24, COL_TEXT);
    hw_ram_bar = bar_new(card, COL_YELLOW);

    card = card_new(cont);
    label_new(card, "GPU", &lv_font_montserrat_14, COL_SUB);
    hw_gpu_val = label_new(card, "--", &lv_font_montserrat_32, COL_TEXT);
    hw_gpu_sub = label_tr(card, TR_LOAD, &lv_font_montserrat_14, COL_SUB);
    hw_gpu_chart = spark_new(card, COL_CYAN, &hw_gpu_ser);

    card = card_new(cont);
    lv_obj_t *row = row_new(card);
    label_tr(row, TR_UPTIME, &lv_font_montserrat_14, COL_SUB);
    hw_up_val = label_new(row, "0d 0h 0m", &lv_font_montserrat_16, COL_TEXT);
}

static void build_network(int col)
{
    lv_obj_t *cont = page_new(col, TR_NETWORK);

    lv_obj_t *card = card_new(cont);
    label_tr(card, TR_TOTAL, &lv_font_montserrat_14, COL_SUB);
    lv_obj_t *row = row_new(card);
    label_new(row, LV_SYMBOL_DOWN, &lv_font_montserrat_16, COL_CYAN);
    net_dl_val = label_new(row, "0 B/s", &lv_font_montserrat_20, COL_TEXT);
    row = row_new(card);
    label_new(row, LV_SYMBOL_UP, &lv_font_montserrat_16, COL_YELLOW);
    net_ul_val = label_new(row, "0 B/s", &lv_font_montserrat_20, COL_TEXT);

    for (int i = 0; i < NET_MAX_IFACES; i++) {
        net_card[i] = card_new(cont);
        row = row_new(net_card[i]);
        net_name[i] = label_new(row, "LAN", &lv_font_montserrat_16, COL_TEXT);
        net_status[i] = label_new(row, LV_SYMBOL_OK, &lv_font_montserrat_16, COL_GREEN);

        row = row_new(net_card[i]);
        label_new(row, "IPv4", &lv_font_montserrat_14, COL_SUB);
        net_ip4[i] = label_new(row, "--", &lv_font_montserrat_14, COL_TEXT);

        label_new(net_card[i], "IPv6", &lv_font_montserrat_14, COL_SUB);
        net_ip6[i] = label_new(net_card[i], "--", &lv_font_montserrat_14, COL_TEXT);
        lv_obj_set_width(net_ip6[i], LV_PCT(100));
        lv_label_set_long_mode(net_ip6[i], LV_LABEL_LONG_DOT);

        net_rate[i] = label_new(net_card[i], LV_SYMBOL_DOWN " 0 B/s   " LV_SYMBOL_UP " 0 B/s",
                                &lv_font_montserrat_14, COL_SUB);
        lv_obj_add_flag(net_card[i], LV_OBJ_FLAG_HIDDEN);
    }
}

static void build_disks(int col)
{
    lv_obj_t *cont = page_new(col, TR_DISKS);

    disk_empty = label_tr(cont, TR_NO_DISKS, &lv_font_montserrat_14, COL_SUB);

    for (int i = 0; i < DISK_MAX; i++) {
        disk_card[i] = card_new(cont);
        lv_obj_t *row = row_new(disk_card[i]);
        disk_name[i] = label_new(row, "--", &lv_font_montserrat_16, COL_TEXT);
        disk_status[i] = label_new(row, LV_SYMBOL_OK, &lv_font_montserrat_16, COL_GREEN);

        row = row_new(disk_card[i]);
        lv_obj_t *colbox = vcol_new(row, 48);
        disk_size_val[i] = label_new(colbox, "--", &lv_font_montserrat_20, COL_TEXT);
        label_tr(colbox, TR_CAP, &lv_font_montserrat_14, COL_SUB);

        colbox = vcol_new(row, 48);
        disk_temp_val[i] = label_new(colbox, "--", &lv_font_montserrat_20, COL_TEXT);
        label_tr(colbox, TR_TEMP_SHORT, &lv_font_montserrat_14, COL_SUB);

        lv_obj_add_flag(disk_card[i], LV_OBJ_FLAG_HIDDEN);
    }
}

static void build_pve(int col)
{
    lv_obj_t *cont = page_new(col, TR_PROXMOX);

    lv_obj_t *card = card_new(cont);
    lv_obj_t *row = row_new(card);
    lv_obj_t *colbox = vcol_new(row, 48);
    pve_vm_val = label_new(colbox, "--", &lv_font_montserrat_24, COL_TEXT);
    label_tr(colbox, TR_VMS_ACTIVE, &lv_font_montserrat_14, COL_SUB);

    colbox = vcol_new(row, 48);
    pve_lxc_val = label_new(colbox, "--", &lv_font_montserrat_24, COL_TEXT);
    label_tr(colbox, TR_LXC_ACTIVE, &lv_font_montserrat_14, COL_SUB);

    pve_hint = label_tr(cont, TR_NO_PVE, &lv_font_montserrat_14, COL_SUB);
    lv_obj_add_flag(pve_hint, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *list_card = card_new(cont);
    lv_obj_set_style_pad_row(list_card, 8, 0);
    for (int i = 0; i < PVE_MAX_GUESTS; i++) {
        pve_row[i] = lv_obj_create(list_card);
        lv_obj_set_size(pve_row[i], LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(pve_row[i], LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(pve_row[i], LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_bg_opa(pve_row[i], LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(pve_row[i], 0, 0);
        lv_obj_set_style_pad_all(pve_row[i], 0, 0);
        lv_obj_set_style_pad_column(pve_row[i], 8, 0);
        lv_obj_remove_flag(pve_row[i], LV_OBJ_FLAG_SCROLLABLE);

        pve_dot[i] = dot_new(pve_row[i], COL_DOT_OFF, 8);
        pve_name[i] = label_new(pve_row[i], "--", &lv_font_montserrat_14, COL_TEXT);
        lv_obj_set_flex_grow(pve_name[i], 1);
        lv_label_set_long_mode(pve_name[i], LV_LABEL_LONG_DOT);
        pve_id[i] = label_new(pve_row[i], "", &lv_font_montserrat_14, COL_SUB);

        lv_obj_add_flag(pve_row[i], LV_OBJ_FLAG_HIDDEN);
    }
}

static void build_opnsense(int col, int wan_max_mbps)
{
    lv_obj_t *cont = page_new(col, TR_OPNSENSE);
    int arc_max = wan_max_mbps > 0 ? wan_max_mbps : 1000;

    lv_obj_t *arc_box = lv_obj_create(cont);
    lv_obj_set_size(arc_box, CARD_W, 180);
    lv_obj_set_style_bg_color(arc_box, lv_color_hex(COL_CARD), 0);
    lv_obj_set_style_bg_opa(arc_box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(arc_box, 12, 0);
    lv_obj_set_style_border_width(arc_box, 0, 0);
    lv_obj_set_style_pad_all(arc_box, 0, 0);
    lv_obj_remove_flag(arc_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(arc_box, LV_OBJ_FLAG_CLICKABLE);

    wan_dl_arc = lv_arc_create(arc_box);
    lv_obj_set_size(wan_dl_arc, 150, 150);
    lv_obj_center(wan_dl_arc);
    lv_arc_set_rotation(wan_dl_arc, 135);
    lv_arc_set_bg_angles(wan_dl_arc, 0, 270);
    lv_arc_set_range(wan_dl_arc, 0, arc_max);
    lv_arc_set_value(wan_dl_arc, 0);
    lv_obj_remove_flag(wan_dl_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_color(wan_dl_arc, lv_color_hex(COL_TRACK), LV_PART_MAIN);
    lv_obj_set_style_arc_color(wan_dl_arc, lv_color_hex(COL_CYAN), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(wan_dl_arc, 10, LV_PART_MAIN);
    lv_obj_set_style_arc_width(wan_dl_arc, 10, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(wan_dl_arc, true, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(wan_dl_arc, LV_OPA_TRANSP, LV_PART_KNOB);

    wan_ul_arc = lv_arc_create(arc_box);
    lv_obj_set_size(wan_ul_arc, 110, 110);
    lv_obj_center(wan_ul_arc);
    lv_arc_set_rotation(wan_ul_arc, 135);
    lv_arc_set_bg_angles(wan_ul_arc, 0, 270);
    lv_arc_set_range(wan_ul_arc, 0, arc_max);
    lv_arc_set_value(wan_ul_arc, 0);
    lv_obj_remove_flag(wan_ul_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_color(wan_ul_arc, lv_color_hex(COL_TRACK), LV_PART_MAIN);
    lv_obj_set_style_arc_color(wan_ul_arc, lv_color_hex(COL_YELLOW), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(wan_ul_arc, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_width(wan_ul_arc, 8, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(wan_ul_arc, true, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(wan_ul_arc, LV_OPA_TRANSP, LV_PART_KNOB);

    wan_dl_label = label_new(arc_box, LV_SYMBOL_DOWN " 0 Mbps", &lv_font_montserrat_14, COL_CYAN);
    lv_obj_align(wan_dl_label, LV_ALIGN_CENTER, 0, -8);
    wan_ul_label = label_new(arc_box, LV_SYMBOL_UP " 0 Mbps", &lv_font_montserrat_14, COL_YELLOW);
    lv_obj_align(wan_ul_label, LV_ALIGN_CENTER, 0, 8);

    lv_obj_t *card = card_new(cont);
    lv_obj_t *row = row_new(card);
    label_new(row, "Gateway", &lv_font_montserrat_14, COL_SUB);
    gw_val_label = label_new(row, "--", &lv_font_montserrat_14, COL_TEXT);
    card = card_new(cont);
    row = row_new(card);
    label_new(row, "Updates", &lv_font_montserrat_14, COL_SUB);
    update_val_label = label_new(row, "--", &lv_font_montserrat_14, COL_TEXT);
    card = card_new(cont);
    row = row_new(card);
    label_new(row, "DHCP", &lv_font_montserrat_14, COL_SUB);
    dhcp_val_label = label_new(row, "--", &lv_font_montserrat_14, COL_TEXT);
    card = card_new(cont);
    row = row_new(card);
    label_new(row, "DNS", &lv_font_montserrat_14, COL_SUB);
    dns_val_label = label_new(row, "--", &lv_font_montserrat_14, COL_TEXT);
    dns_bar = bar_new(card, COL_RED);
}

/* ================= settings panel ================= */

static void retranslate(void)
{
    for (int i = 0; i < tr_reg_count; i++)
        lv_label_set_text(tr_regs[i].obj, tr(tr_regs[i].key));
    if (have_last_disks)
        gui_update_disks(&last_disks);
    if (wp_sub)
        lv_label_set_text(wp_sub, wp_display_name(wp_opts[wp_cur]));
    if (lang_sub)
        lv_label_set_text(lang_sub,
            strcmp(i18n_get_language(), "en") == 0 ? "English" : "Deutsch");
    gui_leds_refresh();
    gui_update_clock();
}

static void timeout_sub_refresh(void)
{
    if (!timeout_sub) return;
    int s = setup.state->backlight_timeout;
    char buf[24];
    if (s <= 0)
        snprintf(buf, sizeof(buf), "%s", tr(TR_NEVER));
    else
        snprintf(buf, sizeof(buf), "%d min", s / 60);
    lv_label_set_text(timeout_sub, buf);
}

static void anim_y_cb(void *obj, int32_t v)
{
    lv_obj_set_y((lv_obj_t *)obj, v);
}

static void panel_animate(int open)
{
    if (!panel) return;
    panel_is_open = open;
    if (scrim) {
        if (open) lv_obj_remove_flag(scrim, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(scrim, LV_OBJ_FLAG_HIDDEN);
    }
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, panel);
    lv_anim_set_values(&a, lv_obj_get_y(panel), open ? 0 : -panel_h);
    lv_anim_set_duration(&a, 220);
    lv_anim_set_exec_cb(&a, anim_y_cb);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

void gui_settings_open(void) { if (!panel_is_open) panel_animate(1); }
void gui_settings_close(void) { if (panel_is_open) panel_animate(0); }

static void confirm_show(int shutdown)
{
    confirm_is_shutdown = shutdown;
    if (!confirm_scrim) return;
    lv_label_set_text(confirm_text,
        tr(shutdown ? TR_CONFIRM_SHUTDOWN : TR_CONFIRM_RESTART));
    lv_obj_remove_flag(confirm_scrim, LV_OBJ_FLAG_HIDDEN);
}

static void confirm_yes_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    lv_obj_add_flag(confirm_scrim, LV_OBJ_FLAG_HIDDEN);
    fprintf(stderr, "panel: %s confirmed\n",
            confirm_is_shutdown ? "shutdown" : "reboot");
    if (confirm_is_shutdown) {
        if (setup.do_poweroff) setup.do_poweroff();
    } else {
        if (setup.do_reboot) setup.do_reboot();
    }
}

static void confirm_cancel_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    lv_obj_add_flag(confirm_scrim, LV_OBJ_FLAG_HIDDEN);
}

static void chevron_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    gui_settings_close();
}

static void scrim_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    gui_settings_close();
}

static void bri_slider_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    int v = (int)lv_slider_get_value(bri_slider);
    if (code == LV_EVENT_VALUE_CHANGED) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d %%", v);
        lv_label_set_text(bri_val_label, buf);
        if (setup.set_brightness) setup.set_brightness(v);
    } else if (code == LV_EVENT_RELEASED) {
        setup.state->brightness = v;
        settings_save(setup.state);
    }
}

static void timeout_btn_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    int cur = 0;
    for (int i = 0; i < TIMEOUT_PRESET_COUNT; i++)
        if (timeout_presets[i] == setup.state->backlight_timeout) { cur = i; break; }
    cur = (cur + 1) % TIMEOUT_PRESET_COUNT;
    setup.state->backlight_timeout = timeout_presets[cur];
    timeout_sub_refresh();
    if (setup.set_timeout) setup.set_timeout(setup.state->backlight_timeout);
    settings_save(setup.state);
}

static void wp_btn_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    if (wp_opt_count == 0) return;
    wp_cur = (wp_cur + 1) % wp_opt_count;
    snprintf(setup.state->wallpaper, sizeof(setup.state->wallpaper), "%s",
             wp_opts[wp_cur]);
    apply_wallpaper(wp_opts[wp_cur]);
    lv_label_set_text(wp_sub, wp_display_name(wp_opts[wp_cur]));
    settings_save(setup.state);
}

static void lang_btn_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    const char *next = strcmp(i18n_get_language(), "de") == 0 ? "en" : "de";
    i18n_set_language(next);
    snprintf(setup.state->language, sizeof(setup.state->language), "%s", next);
    retranslate();
    timeout_sub_refresh();
    settings_save(setup.state);
}

static void restart_btn_cb(lv_event_t *e) { LV_UNUSED(e); confirm_show(0); }
static void shutdown_btn_cb(lv_event_t *e) { LV_UNUSED(e); confirm_show(1); }

void gui_leds_refresh(void)
{
    if (led_sub)
        lv_label_set_text(led_sub, tr(leds_effective_on() ? TR_ON : TR_OFF));
    if (led_night_sub)
        lv_label_set_text(led_night_sub,
            leds_night_enabled() ? leds_night_window() : tr(TR_OFF));
}

static void leds_btn_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    leds_toggle();
    setup.state->leds_on = leds_user_on();
    gui_leds_refresh();
    settings_save(setup.state);
}

static void led_night_btn_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    leds_set_night(!leds_night_enabled());
    setup.state->led_night = leds_night_enabled();
    gui_leds_refresh();
    settings_save(setup.state);
}

static void edge_strip_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    if (code == LV_EVENT_PRESSED) {
        press_start = p;
    } else if (code == LV_EVENT_PRESSING) {
        if (!panel_is_open && p.y - press_start.y > SWIPE_TRIGGER)
            gui_settings_open();
    }
}

static void panel_swipe_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    if (code == LV_EVENT_PRESSED) {
        press_start = p;
    } else if (code == LV_EVENT_PRESSING) {
        if (panel_is_open && press_start.y - p.y > SWIPE_TRIGGER)
            gui_settings_close();
    }
}

/* pill button: title left, optional sub-value right (restart/shutdown) */
static lv_obj_t *settings_btn(lv_obj_t *parent, tr_key_t key, uint32_t bg,
                              uint32_t fg, const char *symbol,
                              lv_obj_t **sub_out, lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, LV_PCT(100), 56);
    lv_obj_set_style_bg_color(btn, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn, 28, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_pad_hor(btn, 18, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *left_box = lv_obj_create(btn);
    lv_obj_set_size(left_box, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(left_box, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_bg_opa(left_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(left_box, 0, 0);
    lv_obj_set_style_pad_all(left_box, 0, 0);
    lv_obj_set_style_pad_column(left_box, 10, 0);
    lv_obj_align(left_box, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_remove_flag(left_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(left_box, LV_OBJ_FLAG_CLICKABLE);

    if (symbol)
        label_new(left_box, symbol, &lv_font_montserrat_18, fg);
    label_tr(left_box, key, &lv_font_montserrat_18, fg);

    if (sub_out) {
        *sub_out = label_new(btn, "", &lv_font_montserrat_14, COL_SUB);
        lv_obj_align(*sub_out, LV_ALIGN_RIGHT_MID, 0, 0);
    }
    return btn;
}

/* small grey section caption above a settings group */
static void section_label(lv_obj_t *parent, tr_key_t key)
{
    lv_obj_t *l = label_tr(parent, key, &lv_font_montserrat_14, COL_SUB);
    lv_obj_set_width(l, LV_PCT(100)); /* full width so the text sits left */
    lv_obj_set_style_pad_left(l, 8, 0);
    lv_obj_set_style_pad_top(l, 4, 0);
}

/* grouped list card; rows are added with list_row() */
static lv_obj_t *list_card_new(lv_obj_t *parent)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(card, lv_color_hex(COL_BTN), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_style_clip_corner(card, true, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_set_style_pad_row(card, 0, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

/* one tappable row in a list card: title left, value right, hairline below */
static lv_obj_t *list_row(lv_obj_t *card, tr_key_t key, lv_obj_t **sub_out,
                          lv_event_cb_t cb, int last)
{
    lv_obj_t *btn = lv_button_create(card);
    lv_obj_set_size(btn, LV_PCT(100), 56);
    lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0xffffff), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(btn, 25, LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn, 0, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_pad_hor(btn, 16, 0);
    if (!last) {
        lv_obj_set_style_border_color(btn, lv_color_hex(0x3a3a40), 0);
        lv_obj_set_style_border_opa(btn, 160, 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_border_side(btn, LV_BORDER_SIDE_BOTTOM, 0);
    }
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *title = label_tr(btn, key, &lv_font_montserrat_18, COL_TEXT);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 0, 0);

    if (sub_out) {
        *sub_out = label_new(btn, "", &lv_font_montserrat_14, COL_SUB);
        lv_obj_align(*sub_out, LV_ALIGN_RIGHT_MID, 0, 0);
    }
    return btn;
}

static void build_settings_panel(lv_obj_t *screen)
{
    /* invisible grab strip along the top edge — drag down to open */
    edge_strip = lv_obj_create(screen);
    lv_obj_set_size(edge_strip, DISP_W, EDGE_STRIP_H);
    lv_obj_set_pos(edge_strip, 0, 0);
    lv_obj_set_style_bg_opa(edge_strip, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(edge_strip, 0, 0);
    lv_obj_add_flag(edge_strip, LV_OBJ_FLAG_FLOATING | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(edge_strip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(edge_strip, edge_strip_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(edge_strip, edge_strip_cb, LV_EVENT_PRESSING, NULL);

    /* dimmed background while the panel is open (tap to close) */
    scrim = lv_obj_create(screen);
    lv_obj_set_size(scrim, DISP_W, DISP_H);
    lv_obj_set_pos(scrim, 0, 0);
    lv_obj_set_style_bg_color(scrim, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(scrim, 140, 0);
    lv_obj_set_style_border_width(scrim, 0, 0);
    lv_obj_add_flag(scrim, LV_OBJ_FLAG_FLOATING | LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(scrim, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(scrim, scrim_cb, LV_EVENT_CLICKED, NULL);

    /* the LED group adds a section caption + a two-row list card */
    panel_h = setup.show_leds ? PANEL_H + 150 : PANEL_H;

    panel = lv_obj_create(screen);
    lv_obj_set_size(panel, DISP_W, panel_h);
    lv_obj_set_pos(panel, 0, -panel_h);
    lv_obj_set_style_bg_color(panel, lv_color_hex(COL_PANEL), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(panel, 18, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_pad_all(panel, 12, 0);
    lv_obj_set_style_pad_row(panel, 9, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_FLOATING | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(panel, panel_swipe_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(panel, panel_swipe_cb, LV_EVENT_PRESSING, NULL);

    /* close handle */
    lv_obj_t *chev = lv_button_create(panel);
    lv_obj_set_size(chev, LV_PCT(100), 26);
    lv_obj_set_style_bg_opa(chev, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(chev, 0, 0);
    lv_obj_add_event_cb(chev, chevron_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *chev_lbl = label_new(chev, LV_SYMBOL_UP, &lv_font_montserrat_16, COL_SUB);
    lv_obj_center(chev_lbl);

    /* brightness card with slider */
    lv_obj_t *card = lv_obj_create(panel);
    lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(card, lv_color_hex(COL_BTN), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 14, 0);
    lv_obj_set_style_pad_row(card, 12, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *row = row_new(card);
    label_tr(row, TR_BRIGHTNESS, &lv_font_montserrat_18, COL_TEXT);
    bri_val_label = label_new(row, "", &lv_font_montserrat_16, COL_SUB);

    bri_slider = lv_slider_create(card);
    lv_obj_set_size(bri_slider, LV_PCT(100), 18);
    lv_slider_set_range(bri_slider, 5, 100);
    lv_slider_set_value(bri_slider, setup.state->brightness, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bri_slider, lv_color_hex(COL_TRACK), LV_PART_MAIN);
    lv_obj_set_style_bg_color(bri_slider, lv_color_hex(COL_CYAN), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(bri_slider, lv_color_hex(0xffffff), LV_PART_KNOB);
    lv_obj_set_style_pad_all(bri_slider, -2, LV_PART_KNOB);
    lv_obj_add_event_cb(bri_slider, bri_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(bri_slider, bri_slider_cb, LV_EVENT_RELEASED, NULL);
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d %%", setup.state->brightness);
        lv_label_set_text(bri_val_label, buf);
    }

    section_label(panel, TR_SEC_DISPLAY);
    lv_obj_t *lc = list_card_new(panel);
    list_row(lc, TR_SCREEN_OFF, &timeout_sub, timeout_btn_cb, 0);
    list_row(lc, TR_WALLPAPER, &wp_sub, wp_btn_cb, 0);
    list_row(lc, TR_LANGUAGE, &lang_sub, lang_btn_cb, 1);
    timeout_sub_refresh();
    lv_label_set_text(wp_sub, wp_display_name(wp_opts[wp_cur]));
    lv_label_set_text(lang_sub,
        strcmp(i18n_get_language(), "en") == 0 ? "English" : "Deutsch");

    if (setup.show_leds) {
        section_label(panel, TR_SEC_LEDS);
        lc = list_card_new(panel);
        list_row(lc, TR_LEDS, &led_sub, leds_btn_cb, 0);
        list_row(lc, TR_LED_NIGHT, &led_night_sub, led_night_btn_cb, 1);
        gui_leds_refresh();
    }

    settings_btn(panel, TR_RESTART, 0x1e7a3c, 0xffffff, LV_SYMBOL_REFRESH,
                 NULL, restart_btn_cb);
    settings_btn(panel, TR_SHUTDOWN, 0x8a2020, 0xffffff, LV_SYMBOL_POWER,
                 NULL, shutdown_btn_cb);

    /* confirmation dialog (shared by restart/shutdown) */
    confirm_scrim = lv_obj_create(screen);
    lv_obj_set_size(confirm_scrim, DISP_W, DISP_H);
    lv_obj_set_pos(confirm_scrim, 0, 0);
    lv_obj_set_style_bg_color(confirm_scrim, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(confirm_scrim, 170, 0);
    lv_obj_set_style_border_width(confirm_scrim, 0, 0);
    lv_obj_add_flag(confirm_scrim, LV_OBJ_FLAG_FLOATING | LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(confirm_scrim, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *dlg = lv_obj_create(confirm_scrim);
    lv_obj_set_size(dlg, CARD_W, LV_SIZE_CONTENT);
    lv_obj_center(dlg);
    lv_obj_set_style_bg_color(dlg, lv_color_hex(COL_PANEL), 0);
    lv_obj_set_style_bg_opa(dlg, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(dlg, 16, 0);
    lv_obj_set_style_border_width(dlg, 0, 0);
    lv_obj_set_style_pad_all(dlg, 16, 0);
    lv_obj_set_style_pad_row(dlg, 14, 0);
    lv_obj_set_flex_flow(dlg, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(dlg, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(dlg, LV_OBJ_FLAG_SCROLLABLE);

    confirm_text = label_new(dlg, "", &lv_font_montserrat_16, COL_TEXT);
    lv_obj_set_width(confirm_text, LV_PCT(100));
    lv_label_set_long_mode(confirm_text, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(confirm_text, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *btn_row = row_new(dlg);
    lv_obj_t *yes = lv_button_create(btn_row);
    lv_obj_set_size(yes, LV_PCT(47), 42);
    lv_obj_set_style_bg_color(yes, lv_color_hex(0x8a2020), 0);
    lv_obj_set_style_radius(yes, 21, 0);
    lv_obj_set_style_shadow_width(yes, 0, 0);
    lv_obj_add_event_cb(yes, confirm_yes_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *yes_lbl = label_tr(yes, TR_YES, &lv_font_montserrat_16, 0xffffff);
    lv_obj_center(yes_lbl);

    lv_obj_t *no = lv_button_create(btn_row);
    lv_obj_set_size(no, LV_PCT(47), 42);
    lv_obj_set_style_bg_color(no, lv_color_hex(COL_BTN), 0);
    lv_obj_set_style_radius(no, 21, 0);
    lv_obj_set_style_shadow_width(no, 0, 0);
    lv_obj_add_event_cb(no, confirm_cancel_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *no_lbl = label_tr(no, TR_CANCEL, &lv_font_montserrat_16, COL_TEXT);
    lv_obj_center(no_lbl);
}

/* ================= public API ================= */

lv_obj_t *gui_create_dashboard(const gui_setup_t *s)
{
    setup = *s;
    if (!setup.state) {
        memset(&fallback_state, 0, sizeof(fallback_state));
        fallback_state.brightness = 100;
        fallback_state.backlight_timeout = 30;
        snprintf(fallback_state.language, sizeof(fallback_state.language), "de");
        setup.state = &fallback_state;
    }

    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    tileview = lv_tileview_create(screen);
    lv_obj_set_size(tileview, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(tileview, LV_OPA_TRANSP, 0);
    lv_obj_set_scrollbar_mode(tileview, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(tileview, tile_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    int col = 0;
    build_home(col++);
    build_hardware(col++);
    build_network(col++);
    build_disks(col++);
    if (setup.show_pve)
        build_pve(col++);
    if (setup.show_opnsense)
        build_opnsense(col++, setup.wan_max_mbps);
    page_count = col;

    build_wp_options();
    apply_wallpaper(wp_opts[wp_cur]);

    dots_box = lv_obj_create(screen);
    lv_obj_set_size(dots_box, LV_PCT(100), 16);
    lv_obj_align(dots_box, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_obj_set_style_bg_opa(dots_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(dots_box, 0, 0);
    lv_obj_set_style_pad_all(dots_box, 0, 0);
    lv_obj_set_style_pad_column(dots_box, 7, 0);
    lv_obj_set_flex_flow(dots_box, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(dots_box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(dots_box, LV_OBJ_FLAG_FLOATING);
    lv_obj_remove_flag(dots_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(dots_box, LV_OBJ_FLAG_CLICKABLE);
    for (int i = 0; i < page_count; i++)
        dots[i] = dot_new(dots_box, COL_DOT_OFF, 6);
    update_dots();

    build_settings_panel(screen);

    /* topmost black layer shown while the screen sleeps: nothing is
     * displayed (no burn-in), but the panel rail keeps the touch chip alive */
    sleep_overlay = lv_obj_create(screen);
    lv_obj_set_size(sleep_overlay, DISP_W, DISP_H);
    lv_obj_set_pos(sleep_overlay, 0, 0);
    lv_obj_set_style_bg_color(sleep_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(sleep_overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sleep_overlay, 0, 0);
    lv_obj_set_style_radius(sleep_overlay, 0, 0);
    lv_obj_add_flag(sleep_overlay, LV_OBJ_FLAG_FLOATING | LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(sleep_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(sleep_overlay, LV_OBJ_FLAG_CLICKABLE);

    return screen;
}

void gui_set_sleep(int on)
{
    if (!sleep_overlay) return;
    if (on)
        lv_obj_remove_flag(sleep_overlay, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(sleep_overlay, LV_OBJ_FLAG_HIDDEN);
}

void gui_update_clock(void)
{
    if (!time_label || !date_label) return;

    static const char *wd_de[7] = { "So", "Mo", "Di", "Mi", "Do", "Fr", "Sa" };
    static const char *wd_en[7] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
    static const char *mon_de[12] = { "Jan", "Feb", "Mrz", "Apr", "Mai", "Jun",
                                      "Jul", "Aug", "Sep", "Okt", "Nov", "Dez" };
    static const char *mon_en[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                      "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

    /* This runs every loop iteration; lv_label_set_text() invalidates the
     * label even for identical text, which would repaint a slice of the
     * (expensive) home tile every frame — so only set on actual change. */
    static char prev_time[64];
    static char prev_date[64];

    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    char buf[64];

    strftime(buf, sizeof(buf), "%H:%M", &tm);
    if (strcmp(buf, prev_time) != 0) {
        snprintf(prev_time, sizeof(prev_time), "%s", buf);
        lv_label_set_text(time_label, buf);
    }

    if (strcmp(i18n_get_language(), "en") == 0)
        snprintf(buf, sizeof(buf), "%s, %s %d",
                 wd_en[tm.tm_wday], mon_en[tm.tm_mon], tm.tm_mday);
    else
        snprintf(buf, sizeof(buf), "%s., %d. %s",
                 wd_de[tm.tm_wday], tm.tm_mday, mon_de[tm.tm_mon]);
    if (strcmp(buf, prev_date) != 0) {
        snprintf(prev_date, sizeof(prev_date), "%s", buf);
        lv_label_set_text(date_label, buf);
    }
}

void gui_update_dashboard(const system_stats_t *stats)
{
    char text[64];

    home_ring_draw((int)stats->cpu_usage);
    if (home_cpu_val) {
        snprintf(text, sizeof(text), "%.0f %%", stats->cpu_usage);
        lv_label_set_text(home_cpu_val, text);
    }
    if (home_ram_val) {
        snprintf(text, sizeof(text), "%.0f %%", stats->ram_usage);
        lv_label_set_text(home_ram_val, text);
    }
    if (home_ram_bar)
        lv_bar_set_value(home_ram_bar, (int32_t)stats->ram_usage, LV_ANIM_OFF);
    if (home_ram_sub) {
        snprintf(text, sizeof(text), "%.1f / %.0f GB",
                 stats->ram_used_mb / 1024.0f, stats->ram_total_mb / 1024.0f);
        lv_label_set_text(home_ram_sub, text);
    }
    if (home_temp_val) {
        if (stats->temp_c > 0)
            snprintf(text, sizeof(text), "%.0f °C", stats->temp_c);
        else
            snprintf(text, sizeof(text), "-- °C");
        lv_label_set_text(home_temp_val, text);
    }
    if (home_temp_bar)
        lv_bar_set_value(home_temp_bar, (int32_t)stats->temp_c, LV_ANIM_OFF);
    if (home_sys_val) {
        snprintf(text, sizeof(text), "%.0f %%", stats->disk_usage);
        lv_label_set_text(home_sys_val, text);
    }
    if (home_sys_bar)
        lv_bar_set_value(home_sys_bar, (int32_t)stats->disk_usage, LV_ANIM_OFF);
    if (home_sys_sub) {
        snprintf(text, sizeof(text), "%.0f / %.0f GB",
                 stats->disk_used_gb, stats->disk_total_gb);
        lv_label_set_text(home_sys_sub, text);
    }
    if (home_up_val) {
        uint64_t d = stats->uptime_seconds / 86400;
        uint64_t h = (stats->uptime_seconds % 86400) / 3600;
        uint64_t m = (stats->uptime_seconds % 3600) / 60;
        snprintf(text, sizeof(text), "%llud %lluh %llum",
                 (unsigned long long)d, (unsigned long long)h,
                 (unsigned long long)m);
        lv_label_set_text(home_up_val, text);
    }

    if (hw_cpu_val) {
        snprintf(text, sizeof(text), "%.1f %%", stats->cpu_usage);
        lv_label_set_text(hw_cpu_val, text);
    }
    if (hw_cpu_chart && hw_cpu_ser)
        lv_chart_set_next_value(hw_cpu_chart, hw_cpu_ser, (int32_t)stats->cpu_usage);

    if (hw_temp_val) {
        if (stats->temp_c > 0)
            snprintf(text, sizeof(text), "%.1f °C", stats->temp_c);
        else
            snprintf(text, sizeof(text), "--,- °C");
        lv_label_set_text(hw_temp_val, text);
    }
    if (hw_temp_bar)
        lv_bar_set_value(hw_temp_bar, (int32_t)stats->temp_c, LV_ANIM_OFF);

    if (hw_ram_val) {
        snprintf(text, sizeof(text), "%.1f / %.1f GB",
                 stats->ram_used_mb / 1024.0f, stats->ram_total_mb / 1024.0f);
        lv_label_set_text(hw_ram_val, text);
    }
    if (hw_ram_bar)
        lv_bar_set_value(hw_ram_bar, (int32_t)stats->ram_usage, LV_ANIM_OFF);

    if (hw_up_val) {
        uint64_t days = stats->uptime_seconds / 86400;
        uint64_t hours = (stats->uptime_seconds % 86400) / 3600;
        uint64_t mins = (stats->uptime_seconds % 3600) / 60;
        snprintf(text, sizeof(text), "%llud %lluh %llum",
                 (unsigned long long)days, (unsigned long long)hours,
                 (unsigned long long)mins);
        lv_label_set_text(hw_up_val, text);
    }
}

void gui_update_gpu(float usage_pct)
{
    char text[32];
    if (!hw_gpu_val) return;

    if (usage_pct < 0) {
        lv_label_set_text(hw_gpu_val, "--");
        if (hw_gpu_sub) lv_label_set_text(hw_gpu_sub, tr(TR_NOT_AVAILABLE));
        return;
    }
    snprintf(text, sizeof(text), "%.1f %%", usage_pct);
    lv_label_set_text(hw_gpu_val, text);
    if (hw_gpu_sub) lv_label_set_text(hw_gpu_sub, tr(TR_LOAD));
    if (hw_gpu_chart && hw_gpu_ser)
        lv_chart_set_next_value(hw_gpu_chart, hw_gpu_ser, (int32_t)usage_pct);
}

void gui_update_net(const net_stats_t *net)
{
    char text[96], rx[24], tx[24];

    if (net_dl_val) {
        fmt_bps(net->total_rx_bps, rx, sizeof(rx));
        lv_label_set_text(net_dl_val, rx);
    }
    if (net_ul_val) {
        fmt_bps(net->total_tx_bps, tx, sizeof(tx));
        lv_label_set_text(net_ul_val, tx);
    }
    if (home_net_rx) {
        fmt_bps(net->total_rx_bps, rx, sizeof(rx));
        snprintf(text, sizeof(text), LV_SYMBOL_DOWN " %s", rx);
        lv_label_set_text(home_net_rx, text);
    }
    if (home_net_tx) {
        fmt_bps(net->total_tx_bps, tx, sizeof(tx));
        snprintf(text, sizeof(text), LV_SYMBOL_UP " %s", tx);
        lv_label_set_text(home_net_tx, text);
    }

    for (int i = 0; i < NET_MAX_IFACES; i++) {
        if (!net_card[i]) continue;
        if (i >= net->iface_count) {
            lv_obj_add_flag(net_card[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        const net_iface_t *nif = &net->ifaces[i];
        lv_obj_remove_flag(net_card[i], LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(net_name[i], nif->name);
        lv_label_set_text(net_status[i], nif->link_up ? LV_SYMBOL_OK : LV_SYMBOL_CLOSE);
        lv_obj_set_style_text_color(net_status[i],
            lv_color_hex(nif->link_up ? COL_GREEN : COL_RED), 0);
        lv_label_set_text(net_ip4[i], nif->ipv4[0] ? nif->ipv4 : "--");
        lv_label_set_text(net_ip6[i], nif->ipv6[0] ? nif->ipv6 : "--");
        fmt_bps(nif->rx_bps, rx, sizeof(rx));
        fmt_bps(nif->tx_bps, tx, sizeof(tx));
        snprintf(text, sizeof(text), LV_SYMBOL_DOWN " %s   " LV_SYMBOL_UP " %s", rx, tx);
        lv_label_set_text(net_rate[i], text);
    }
}

void gui_update_disks(const disk_stats_t *disks)
{
    char text[32];

    if (disks != &last_disks) {
        last_disks = *disks;
        have_last_disks = 1;
    }

    if (disk_empty) {
        if (disks->count == 0)
            lv_obj_remove_flag(disk_empty, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(disk_empty, LV_OBJ_FLAG_HIDDEN);
    }

    for (int i = 0; i < DISK_MAX; i++) {
        if (!disk_card[i]) continue;
        if (i >= disks->count) {
            lv_obj_add_flag(disk_card[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        const disk_info_t *d = &disks->disks[i];
        lv_obj_remove_flag(disk_card[i], LV_OBJ_FLAG_HIDDEN);
        snprintf(text, sizeof(text), tr(d->is_nvme ? TR_NVME_N : TR_DISK_N), d->idx);
        lv_label_set_text(disk_name[i], text);
        lv_label_set_text(disk_status[i], d->online ? LV_SYMBOL_OK : LV_SYMBOL_CLOSE);
        lv_obj_set_style_text_color(disk_status[i],
            lv_color_hex(d->online ? COL_GREEN : COL_RED), 0);

        if (d->size_tb >= 1.0f)
            snprintf(text, sizeof(text), "%.1f TB", d->size_tb);
        else
            snprintf(text, sizeof(text), "%.0f GB", d->size_tb * 1000.0f);
        lv_label_set_text(disk_size_val[i], text);

        if (d->temp_c >= 0)
            snprintf(text, sizeof(text), "%.0f °C", d->temp_c);
        else
            snprintf(text, sizeof(text), "--");
        lv_label_set_text(disk_temp_val[i], text);
    }
}

void gui_update_pve(const pve_stats_t *pve)
{
    char text[32];

    if (!pve->available) {
        if (pve_hint) lv_obj_remove_flag(pve_hint, LV_OBJ_FLAG_HIDDEN);
        if (pve_vm_val) lv_label_set_text(pve_vm_val, "--");
        if (pve_lxc_val) lv_label_set_text(pve_lxc_val, "--");
        for (int i = 0; i < PVE_MAX_GUESTS; i++)
            if (pve_row[i]) lv_obj_add_flag(pve_row[i], LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if (pve_hint) lv_obj_add_flag(pve_hint, LV_OBJ_FLAG_HIDDEN);

    if (pve_vm_val) {
        snprintf(text, sizeof(text), "%d / %d", pve->vm_running, pve->vm_total);
        lv_label_set_text(pve_vm_val, text);
    }
    if (pve_lxc_val) {
        snprintf(text, sizeof(text), "%d / %d", pve->lxc_running, pve->lxc_total);
        lv_label_set_text(pve_lxc_val, text);
    }

    for (int i = 0; i < PVE_MAX_GUESTS; i++) {
        if (!pve_row[i]) continue;
        if (i >= pve->count) {
            lv_obj_add_flag(pve_row[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        const pve_guest_t *g = &pve->guests[i];
        lv_obj_remove_flag(pve_row[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(pve_dot[i],
            lv_color_hex(g->running ? COL_GREEN : COL_DOT_OFF), 0);
        lv_label_set_text(pve_name[i], g->name);
        snprintf(text, sizeof(text), "%d", g->vmid);
        lv_label_set_text(pve_id[i], text);
    }
}

void gui_update_opnsense(const opnsense_stats_t *stats)
{
    char text[64];

    if (gw_val_label) {
        if (stats->gw_rtt_ms >= 0)
            snprintf(text, sizeof(text), "%s %dms", stats->gw_status, stats->gw_rtt_ms);
        else
            snprintf(text, sizeof(text), "%s", stats->gw_status);
        lv_label_set_text(gw_val_label, text);
    }
    if (update_val_label)
        lv_label_set_text(update_val_label, stats->update_status);
    if (dhcp_val_label) {
        snprintf(text, sizeof(text), "%d leases", stats->dhcp_leases);
        lv_label_set_text(dhcp_val_label, text);
    }
    if (dns_val_label) {
        snprintf(text, sizeof(text), "%d%% blocked", stats->dns_blocked_pct);
        lv_label_set_text(dns_val_label, text);
    }
    if (dns_bar)
        lv_bar_set_value(dns_bar, (int32_t)stats->dns_blocked_pct, LV_ANIM_OFF);
}

void gui_update_wan_throughput(float wan_in_bps, float wan_out_bps)
{
    char text[32];
    float dl_mbps = wan_in_bps / 1000000.0f;
    float ul_mbps = wan_out_bps / 1000000.0f;

    if (wan_dl_arc)
        lv_arc_set_value(wan_dl_arc, (int32_t)(dl_mbps + 0.5f));
    if (wan_ul_arc)
        lv_arc_set_value(wan_ul_arc, (int32_t)(ul_mbps + 0.5f));
    if (wan_dl_label) {
        snprintf(text, sizeof(text), LV_SYMBOL_DOWN " %.1f Mbps", dl_mbps);
        lv_label_set_text(wan_dl_label, text);
    }
    if (wan_ul_label) {
        snprintf(text, sizeof(text), LV_SYMBOL_UP " %.1f Mbps", ul_mbps);
        lv_label_set_text(wan_ul_label, text);
    }
}

int gui_page_count(void)
{
    return page_count;
}

void gui_show_page(int idx)
{
    if (!tileview || idx < 0 || idx >= page_count) return;
    lv_tileview_set_tile_by_index(tileview, idx, 0, LV_ANIM_OFF);
    update_dots();
}

void gui_cleanup(void)
{
    if (wp_buf) { free(wp_buf); wp_buf = NULL; }
    tileview = NULL;
    dots_box = NULL;
    page_count = 0;
    tr_reg_count = 0;
    have_last_disks = 0;
    memset(tiles, 0, sizeof(tiles));
    memset(dots, 0, sizeof(dots));
    wp_img = NULL;
    time_label = date_label = NULL;
    home_cpu_canvas = home_cpu_val = home_ram_val = home_temp_val = NULL;
    home_ring_pct = -1;
    glass_tile_count = 0;
    memset(glass_tiles, 0, sizeof(glass_tiles));
    home_ram_bar = home_ram_sub = home_temp_bar = home_temp_sub = NULL;
    home_sys_val = home_sys_bar = home_sys_sub = NULL;
    home_net_rx = home_net_tx = home_up_val = NULL;
    hw_cpu_val = hw_cpu_chart = hw_temp_val = hw_temp_bar = NULL;
    hw_ram_val = hw_ram_bar = hw_gpu_val = hw_gpu_sub = hw_gpu_chart = hw_up_val = NULL;
    hw_cpu_ser = hw_gpu_ser = NULL;
    net_dl_val = net_ul_val = NULL;
    memset(net_card, 0, sizeof(net_card));
    memset(disk_card, 0, sizeof(disk_card));
    disk_empty = NULL;
    pve_vm_val = pve_lxc_val = pve_hint = NULL;
    memset(pve_row, 0, sizeof(pve_row));
    wan_dl_arc = wan_ul_arc = wan_dl_label = wan_ul_label = NULL;
    gw_val_label = update_val_label = dhcp_val_label = dns_val_label = dns_bar = NULL;
    edge_strip = scrim = panel = NULL;
    panel_is_open = 0;
    bri_val_label = bri_slider = timeout_sub = wp_sub = lang_sub = NULL;
    led_sub = led_night_sub = NULL;
    panel_h = PANEL_H;
    confirm_scrim = confirm_text = NULL;
}
