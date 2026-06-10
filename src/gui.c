#include "gui.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
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
#define WP_PATH "A:/etc/ug-paneld/wallpaper.png"

/* ---- UGOS-inspired palette ---- */
#define COL_BG      0x0a0a0c
#define COL_CARD    0x1b1b1e
#define COL_TEXT    0xf2f2f2
#define COL_SUB     0x8e8e93
#define COL_TRACK   0x2c2c2e
#define COL_CYAN    0x35d1e2
#define COL_YELLOW  0xffd231
#define COL_GREEN   0x34c759
#define COL_RED     0xff453a
#define COL_DOT_OFF 0x48484c

/* ---- Pages ---- */
#define MAX_PAGES 6
static lv_obj_t *tileview = NULL;
static lv_obj_t *tiles[MAX_PAGES];
static lv_obj_t *dots[MAX_PAGES];
static lv_obj_t *dots_box = NULL;
static int page_count = 0;

/* ---- Home ---- */
static uint8_t *wp_buf = NULL;
static lv_image_dsc_t wp_dsc;
static lv_obj_t *time_label = NULL;
static lv_obj_t *date_label = NULL;
static lv_obj_t *home_cpu_arc = NULL;
static lv_obj_t *home_cpu_val = NULL;
static lv_obj_t *home_ram_val = NULL;
static lv_obj_t *home_temp_val = NULL;

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

/* key (gray, left) / value (white, right) row */
static lv_obj_t *kv_new(lv_obj_t *parent, const char *key, const lv_font_t *vfont,
                        lv_obj_t **val_out)
{
    lv_obj_t *row = row_new(parent);
    label_new(row, key, &lv_font_montserrat_14, COL_SUB);
    *val_out = label_new(row, "--", vfont, COL_TEXT);
    return row;
}

/* Adds a new tile and returns its padded flex-column content container. */
static lv_obj_t *page_new(int col, const char *title)
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

    if (title) {
        lv_obj_t *hdr = row_new(cont);
        lv_obj_set_style_pad_top(hdr, 4, 0);
        lv_obj_set_style_pad_bottom(hdr, 2, 0);
        label_new(hdr, title, &lv_font_montserrat_20, COL_TEXT);
    }
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

/* ================= pages ================= */

static void load_wallpaper(lv_obj_t *parent)
{
    lv_image_decoder_dsc_t dsc;
    lv_image_header_t header;
    if (lv_image_decoder_get_info(WP_PATH, &header) != LV_RESULT_OK) {
        fprintf(stderr, "Wallpaper: none at %s (optional)\n", WP_PATH);
        return;
    }
    if (lv_image_decoder_open(&dsc, WP_PATH, NULL) != LV_RESULT_OK) {
        fprintf(stderr, "Wallpaper: decoder_open failed\n");
        return;
    }

    uint32_t src_w = header.w, src_h = header.h;
    const uint8_t *src_data = dsc.decoded->data;
    uint32_t src_stride = dsc.decoded->header.stride;
    uint32_t bpp = LV_COLOR_FORMAT_GET_BPP(dsc.decoded->header.cf) / 8;

    uint32_t dst_stride = DISP_W * 4;
    wp_buf = malloc(dst_stride * DISP_H);
    if (wp_buf && src_data && (bpp == 3 || bpp == 4)) {
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
                uint8_t *dp = wp_buf + y * dst_stride + x * 4;
                if (bpp == 4) {
                    dp[0] = sp[0]; dp[1] = sp[1]; dp[2] = sp[2]; dp[3] = sp[3];
                } else {
                    dp[0] = sp[0]; dp[1] = sp[1]; dp[2] = sp[2]; dp[3] = 0xFF;
                }
            }
        }

        wp_dsc.header.w = DISP_W;
        wp_dsc.header.h = DISP_H;
        wp_dsc.header.cf = LV_COLOR_FORMAT_ARGB8888;
        wp_dsc.header.stride = dst_stride;
        wp_dsc.data_size = dst_stride * DISP_H;
        wp_dsc.data = wp_buf;

        lv_obj_t *wp = lv_image_create(parent);
        lv_image_set_src(wp, &wp_dsc);
        lv_obj_set_pos(wp, 0, 0);
    }
    lv_image_decoder_close(&dsc);
}

static void build_home(int col)
{
    lv_obj_t *tile = lv_tileview_add_tile(tileview, col, 0, LV_DIR_HOR);
    tiles[col] = tile;
    lv_obj_set_style_bg_color(tile, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);

    load_wallpaper(tile);

    lv_obj_t *cont = lv_obj_create(tile);
    lv_obj_set_size(cont, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, PAGE_PAD, 0);
    lv_obj_set_style_pad_row(cont, 8, 0);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_remove_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

    date_label = label_new(cont, "", &lv_font_montserrat_16, 0xd8d8d8);
    lv_obj_set_style_pad_top(date_label, 16, 0);

    time_label = label_new(cont, "00:00", &lv_font_montserrat_48, 0xffffff);

    /* translucent stats panel with CPU ring */
    lv_obj_t *panel = lv_obj_create(cont);
    lv_obj_set_size(panel, CARD_W, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(panel, 110, 0);
    lv_obj_set_style_radius(panel, 16, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_pad_all(panel, 14, 0);
    lv_obj_set_style_pad_row(panel, 10, 0);
    lv_obj_set_style_margin_top(panel, 14, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *arc_wrap = lv_obj_create(panel);
    lv_obj_set_size(arc_wrap, 132, 132);
    lv_obj_set_style_bg_opa(arc_wrap, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(arc_wrap, 0, 0);
    lv_obj_set_style_pad_all(arc_wrap, 0, 0);
    lv_obj_remove_flag(arc_wrap, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(arc_wrap, LV_OBJ_FLAG_CLICKABLE);

    home_cpu_arc = lv_arc_create(arc_wrap);
    lv_obj_set_size(home_cpu_arc, 132, 132);
    lv_obj_center(home_cpu_arc);
    lv_arc_set_rotation(home_cpu_arc, 270);
    lv_arc_set_bg_angles(home_cpu_arc, 0, 360);
    lv_arc_set_range(home_cpu_arc, 0, 100);
    lv_arc_set_value(home_cpu_arc, 0);
    lv_obj_remove_flag(home_cpu_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_color(home_cpu_arc, lv_color_hex(COL_TRACK), LV_PART_MAIN);
    lv_obj_set_style_arc_color(home_cpu_arc, lv_color_hex(COL_CYAN), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(home_cpu_arc, 10, LV_PART_MAIN);
    lv_obj_set_style_arc_width(home_cpu_arc, 10, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(home_cpu_arc, true, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(home_cpu_arc, LV_OPA_TRANSP, LV_PART_KNOB);

    home_cpu_val = label_new(arc_wrap, "0 %", &lv_font_montserrat_24, COL_TEXT);
    lv_obj_align(home_cpu_val, LV_ALIGN_CENTER, 0, -8);
    lv_obj_t *cpu_cap = label_new(arc_wrap, "CPU", &lv_font_montserrat_14, COL_SUB);
    lv_obj_align(cpu_cap, LV_ALIGN_CENTER, 0, 16);

    lv_obj_t *row = row_new(panel);
    lv_obj_t *left = lv_obj_create(row);
    lv_obj_set_size(left, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(left, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(left, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(left, 0, 0);
    lv_obj_set_style_pad_all(left, 0, 0);
    lv_obj_set_style_pad_column(left, 6, 0);
    lv_obj_remove_flag(left, LV_OBJ_FLAG_SCROLLABLE);
    dot_new(left, COL_YELLOW, 8);
    home_ram_val = label_new(left, "RAM --", &lv_font_montserrat_14, COL_TEXT);

    lv_obj_t *right = lv_obj_create(row);
    lv_obj_set_size(right, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(right, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(right, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(right, 0, 0);
    lv_obj_set_style_pad_all(right, 0, 0);
    lv_obj_set_style_pad_column(right, 6, 0);
    lv_obj_remove_flag(right, LV_OBJ_FLAG_SCROLLABLE);
    dot_new(right, COL_RED, 8);
    home_temp_val = label_new(right, "--,- °C", &lv_font_montserrat_14, COL_TEXT);
}

static void build_hardware(int col)
{
    lv_obj_t *cont = page_new(col, "Hardware");

    /* CPU */
    lv_obj_t *card = card_new(cont);
    label_new(card, "CPU", &lv_font_montserrat_14, COL_SUB);
    hw_cpu_val = label_new(card, "0 %", &lv_font_montserrat_32, COL_TEXT);
    label_new(card, "Auslastung", &lv_font_montserrat_14, COL_SUB);
    hw_cpu_chart = spark_new(card, COL_CYAN, &hw_cpu_ser);

    /* Temperatur */
    card = card_new(cont);
    label_new(card, "Temperatur", &lv_font_montserrat_14, COL_SUB);
    hw_temp_val = label_new(card, "--,- °C", &lv_font_montserrat_32, COL_TEXT);
    hw_temp_bar = bar_new(card, COL_YELLOW);

    /* RAM */
    card = card_new(cont);
    label_new(card, "Arbeitsspeicher", &lv_font_montserrat_14, COL_SUB);
    hw_ram_val = label_new(card, "0,0 / 0,0 GB", &lv_font_montserrat_24, COL_TEXT);
    hw_ram_bar = bar_new(card, COL_YELLOW);

    /* GPU */
    card = card_new(cont);
    label_new(card, "GPU", &lv_font_montserrat_14, COL_SUB);
    hw_gpu_val = label_new(card, "--", &lv_font_montserrat_32, COL_TEXT);
    hw_gpu_sub = label_new(card, "Auslastung", &lv_font_montserrat_14, COL_SUB);
    hw_gpu_chart = spark_new(card, COL_CYAN, &hw_gpu_ser);

    /* Uptime */
    card = card_new(cont);
    lv_obj_t *row = row_new(card);
    label_new(row, "Uptime", &lv_font_montserrat_14, COL_SUB);
    hw_up_val = label_new(row, "0d 0h 0m", &lv_font_montserrat_16, COL_TEXT);
}

static void build_network(int col)
{
    lv_obj_t *cont = page_new(col, "Netzwerk");

    lv_obj_t *card = card_new(cont);
    label_new(card, "Insgesamt", &lv_font_montserrat_14, COL_SUB);
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

        kv_new(net_card[i], "IPv4", &lv_font_montserrat_14, &net_ip4[i]);

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
    lv_obj_t *cont = page_new(col, "Festplatten");

    disk_empty = label_new(cont, "Keine Laufwerke gefunden", &lv_font_montserrat_14, COL_SUB);

    for (int i = 0; i < DISK_MAX; i++) {
        disk_card[i] = card_new(cont);
        lv_obj_t *row = row_new(disk_card[i]);
        disk_name[i] = label_new(row, "Festplatte", &lv_font_montserrat_16, COL_TEXT);
        disk_status[i] = label_new(row, LV_SYMBOL_OK, &lv_font_montserrat_16, COL_GREEN);

        row = row_new(disk_card[i]);
        lv_obj_t *colbox = lv_obj_create(row);
        lv_obj_set_size(colbox, LV_PCT(48), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(colbox, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_bg_opa(colbox, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(colbox, 0, 0);
        lv_obj_set_style_pad_all(colbox, 0, 0);
        lv_obj_remove_flag(colbox, LV_OBJ_FLAG_SCROLLABLE);
        disk_size_val[i] = label_new(colbox, "--", &lv_font_montserrat_20, COL_TEXT);
        label_new(colbox, "Kap.", &lv_font_montserrat_14, COL_SUB);

        colbox = lv_obj_create(row);
        lv_obj_set_size(colbox, LV_PCT(48), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(colbox, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_bg_opa(colbox, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(colbox, 0, 0);
        lv_obj_set_style_pad_all(colbox, 0, 0);
        lv_obj_remove_flag(colbox, LV_OBJ_FLAG_SCROLLABLE);
        disk_temp_val[i] = label_new(colbox, "--", &lv_font_montserrat_20, COL_TEXT);
        label_new(colbox, "Temp.", &lv_font_montserrat_14, COL_SUB);

        lv_obj_add_flag(disk_card[i], LV_OBJ_FLAG_HIDDEN);
    }
}

static void build_pve(int col)
{
    lv_obj_t *cont = page_new(col, "Proxmox");

    lv_obj_t *card = card_new(cont);
    lv_obj_t *row = row_new(card);
    lv_obj_t *colbox = lv_obj_create(row);
    lv_obj_set_size(colbox, LV_PCT(48), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(colbox, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_opa(colbox, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(colbox, 0, 0);
    lv_obj_set_style_pad_all(colbox, 0, 0);
    lv_obj_remove_flag(colbox, LV_OBJ_FLAG_SCROLLABLE);
    pve_vm_val = label_new(colbox, "--", &lv_font_montserrat_24, COL_TEXT);
    label_new(colbox, "VMs aktiv", &lv_font_montserrat_14, COL_SUB);

    colbox = lv_obj_create(row);
    lv_obj_set_size(colbox, LV_PCT(48), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(colbox, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_opa(colbox, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(colbox, 0, 0);
    lv_obj_set_style_pad_all(colbox, 0, 0);
    lv_obj_remove_flag(colbox, LV_OBJ_FLAG_SCROLLABLE);
    pve_lxc_val = label_new(colbox, "--", &lv_font_montserrat_24, COL_TEXT);
    label_new(colbox, "LXC aktiv", &lv_font_montserrat_14, COL_SUB);

    pve_hint = label_new(cont, "Kein Proxmox erkannt", &lv_font_montserrat_14, COL_SUB);
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
    lv_obj_t *cont = page_new(col, "OPNsense");
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
    kv_new(card, "Gateway", &lv_font_montserrat_14, &gw_val_label);
    card = card_new(cont);
    kv_new(card, "Updates", &lv_font_montserrat_14, &update_val_label);
    card = card_new(cont);
    kv_new(card, "DHCP", &lv_font_montserrat_14, &dhcp_val_label);
    card = card_new(cont);
    kv_new(card, "DNS", &lv_font_montserrat_14, &dns_val_label);
    dns_bar = bar_new(card, COL_RED);
}

/* ================= public API ================= */

lv_obj_t *gui_create_dashboard(int show_opnsense, int wan_max_mbps)
{
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
    build_pve(col++);
    if (show_opnsense)
        build_opnsense(col++, wan_max_mbps);
    page_count = col;

    /* page indicator dots */
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

    return screen;
}

void gui_update_clock(void)
{
    if (!time_label || !date_label) return;

    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    char buf[64];

    strftime(buf, sizeof(buf), "%H:%M", &tm);
    lv_label_set_text(time_label, buf);

    strftime(buf, sizeof(buf), "%A, %B %-d", &tm);
    lv_label_set_text(date_label, buf);
}

void gui_update_dashboard(const system_stats_t *stats)
{
    char text[64];

    /* Home */
    if (home_cpu_arc)
        lv_arc_set_value(home_cpu_arc, (int32_t)stats->cpu_usage);
    if (home_cpu_val) {
        snprintf(text, sizeof(text), "%.0f %%", stats->cpu_usage);
        lv_label_set_text(home_cpu_val, text);
    }
    if (home_ram_val) {
        snprintf(text, sizeof(text), "RAM %.0f %%", stats->ram_usage);
        lv_label_set_text(home_ram_val, text);
    }
    if (home_temp_val) {
        if (stats->temp_c > 0)
            snprintf(text, sizeof(text), "%.1f °C", stats->temp_c);
        else
            snprintf(text, sizeof(text), "--,- °C");
        lv_label_set_text(home_temp_val, text);
    }

    /* Hardware */
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
        if (hw_gpu_sub) lv_label_set_text(hw_gpu_sub, "nicht verfuegbar");
        return;
    }
    snprintf(text, sizeof(text), "%.1f %%", usage_pct);
    lv_label_set_text(hw_gpu_val, text);
    if (hw_gpu_sub) lv_label_set_text(hw_gpu_sub, "Auslastung");
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
        snprintf(text, sizeof(text), d->is_nvme ? "NVMe %d" : "Festplatte %d", d->idx);
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
    memset(tiles, 0, sizeof(tiles));
    memset(dots, 0, sizeof(dots));
    time_label = date_label = NULL;
    home_cpu_arc = home_cpu_val = home_ram_val = home_temp_val = NULL;
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
}
