#include "gui.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "lvgl/lvgl.h"
#include "lvgl/src/draw/lv_image_decoder_private.h"
#include "lvgl/src/draw/lv_draw_buf_private.h"

#define CARD_WIDTH 238
#define CARD_PAD_X 10
#define BAR_HEIGHT 6
#define DISP_W 258
#define DISP_H 960
#define WP_PATH "A:/etc/proxmox-display/wallpaper.png"

/* Wallpaper buffer, we decode this once at startup */
static uint8_t *wp_buf = NULL;
static lv_image_dsc_t wp_dsc;

/* Clock labels */
static lv_obj_t *time_label = NULL;
static lv_obj_t *date_label = NULL;

/* Stat labels and bars */
static lv_obj_t *cpu_bar = NULL;
static lv_obj_t *cpu_val_label = NULL;
static lv_obj_t *ram_bar = NULL;
static lv_obj_t *ram_val_label = NULL;
static lv_obj_t *disk_bar = NULL;
static lv_obj_t *disk_val_label = NULL;
static lv_obj_t *temp_val_label = NULL;
static lv_obj_t *uptime_val_label = NULL;

static lv_obj_t *create_widget(lv_obj_t *parent, const char *title, uint32_t color,
                                int has_bar, const char *default_val,
                                const lv_font_t *val_font,
                                lv_obj_t **bar_out, lv_obj_t **label_out)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, CARD_WIDTH, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_bg_opa(card, 180, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_pad_all(card, 10, 0);
    lv_obj_set_style_pad_row(card, 4, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);

    /* Title + value row */
    lv_obj_t *row = lv_obj_create(card);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);

    lv_obj_t *title_lbl = lv_label_create(row);
    lv_label_set_text(title_lbl, title);
    lv_obj_set_style_text_color(title_lbl, lv_color_hex(color), 0);
    lv_obj_set_style_text_font(title_lbl, &lv_font_montserrat_16, 0);

    if (label_out) {
        *label_out = lv_label_create(row);
        lv_label_set_text(*label_out, default_val);
        lv_obj_set_style_text_color(*label_out, lv_color_hex(0xffffff), 0);
        lv_obj_set_style_text_font(*label_out, val_font, 0);
    }

    /* Optional bar below text row */
    if (has_bar && bar_out) {
        *bar_out = lv_bar_create(card);
        lv_bar_set_range(*bar_out, 0, 100);
        lv_bar_set_value(*bar_out, 0, LV_ANIM_OFF);
        lv_obj_set_size(*bar_out, LV_PCT(100), BAR_HEIGHT);
        lv_obj_set_style_bg_color(*bar_out, lv_color_hex(0x2a2a3e), LV_PART_MAIN);
        lv_obj_set_style_bg_color(*bar_out, lv_color_hex(color), LV_PART_INDICATOR);
        lv_obj_set_style_radius(*bar_out, 3, 0);
    }

    return card;
}

lv_obj_t *gui_create_dashboard(void) {
    lv_obj_t *screen = lv_screen_active();

    lv_obj_set_style_bg_color(screen, lv_color_hex(0x1a1a2e), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    /* Wallpaper:
    We decode this once, scale and crop to display size, then we freeze in buffer
    Means we don't need to ask user to preprocess
    but also maintains performance as we don't need to render additional frames */
    lv_image_decoder_dsc_t dsc;
    lv_image_header_t header;
    if (lv_image_decoder_get_info(WP_PATH, &header) == LV_RESULT_OK) {
        fprintf(stderr, "Wallpaper: %dx%d cf=%d\n", header.w, header.h, header.cf);
        if (lv_image_decoder_open(&dsc, WP_PATH, NULL) == LV_RESULT_OK) {
            uint32_t src_w = header.w;
            uint32_t src_h = header.h;
            const uint8_t *src_data = dsc.decoded->data;
            uint32_t src_stride = dsc.decoded->header.stride;
            uint32_t bpp = LV_COLOR_FORMAT_GET_BPP(dsc.decoded->header.cf) / 8;
            fprintf(stderr, "Wallpaper decoded: data=%p stride=%u bpp=%u\n",
                    (void *)src_data, src_stride, bpp);

            /* Allocate the output buffer: ARGB8888 = 4 bytes per pixel */
            uint32_t dst_stride = DISP_W * 4;
            wp_buf = malloc(dst_stride * DISP_H);
            if (wp_buf && src_data) {
                /* Scaling: we fill display, then crop overflow (if aspect ratio requires) */
                float scale_w = (float)DISP_W / src_w;
                float scale_h = (float)DISP_H / src_h;
                float scale = scale_w > scale_h ? scale_w : scale_h;

                uint32_t scaled_w = (uint32_t)(src_w * scale + 0.5f);
                uint32_t scaled_h = (uint32_t)(src_h * scale + 0.5f);

                /* Center-crop offsets */
                int off_x = ((int)scaled_w - DISP_W) / 2;
                int off_y = ((int)scaled_h - DISP_H) / 2;

                /* Nearest-neighbor scale and crop */
                for (int y = 0; y < DISP_H; y++) {
                    uint32_t sy = (uint32_t)((y + off_y) / scale);
                    if (sy >= src_h) sy = src_h - 1;
                    for (int x = 0; x < DISP_W; x++) {
                        uint32_t sx = (uint32_t)((x + off_x) / scale);
                        if (sx >= src_w) sx = src_w - 1;

                        const uint8_t *sp = src_data + sy * src_stride + sx * bpp;
                        uint8_t *dp = wp_buf + y * dst_stride + x * 4;

                        if (bpp == 4) {
                            /* ARGB8888 or XRGB8888 */
                            dp[0] = sp[0]; dp[1] = sp[1];
                            dp[2] = sp[2]; dp[3] = sp[3];
                        } else if (bpp == 3) {
                            /* RGB888 to XRGB8888 */
                            dp[0] = sp[0]; dp[1] = sp[1];
                            dp[2] = sp[2]; dp[3] = 0xFF;
                        }
                    }
                }

                /* static image descriptor */
                wp_dsc.header.w = DISP_W;
                wp_dsc.header.h = DISP_H;
                wp_dsc.header.cf = LV_COLOR_FORMAT_ARGB8888;
                wp_dsc.header.stride = dst_stride;
                wp_dsc.data_size = dst_stride * DISP_H;
                wp_dsc.data = wp_buf;

                lv_obj_t *wp = lv_image_create(screen);
                lv_image_set_src(wp, &wp_dsc);
                lv_obj_set_pos(wp, 0, 0);

                fprintf(stderr, "Wallpaper rendered to buffer\n");
            }

            lv_image_decoder_close(&dsc);
        } else {
            fprintf(stderr, "Wallpaper: decoder_open failed\n");
        }
    } else {
        fprintf(stderr, "Wallpaper: get_info failed for %s\n", WP_PATH);
    }

    /* Main column container */
    lv_obj_t *container = lv_obj_create(screen);
    lv_obj_set_size(container, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(container, CARD_PAD_X, 0);
    lv_obj_set_style_pad_row(container, 8, 0);
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(container, 0, 0);

    /* Clock section */
    lv_obj_t *clock_box = lv_obj_create(container);
    lv_obj_set_size(clock_box, CARD_WIDTH, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(clock_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(clock_box, 0, 0);
    lv_obj_set_style_pad_all(clock_box, 0, 0);
    lv_obj_set_style_pad_top(clock_box, 20, 0);
    lv_obj_set_style_pad_bottom(clock_box, 10, 0);
    lv_obj_set_flex_flow(clock_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(clock_box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    time_label = lv_label_create(clock_box);
    lv_label_set_text(time_label, "00:00");
    lv_obj_set_style_text_color(time_label, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(time_label, &lv_font_montserrat_48, 0);

    date_label = lv_label_create(clock_box);
    lv_label_set_text(date_label, "");
    lv_obj_set_style_text_color(date_label, lv_color_hex(0xb0b0b0), 0);
    lv_obj_set_style_text_font(date_label, &lv_font_montserrat_18, 0);

    /* Stat cards */
    create_widget(container, "CPU", 0xe94560, 1, "0%",
                  &lv_font_montserrat_14, &cpu_bar, &cpu_val_label);
    create_widget(container, "RAM", 0x0f3460, 1, "0 / 0 GB",
                  &lv_font_montserrat_14, &ram_bar, &ram_val_label);
    create_widget(container, "Disk", 0x533483, 1, "0 / 0 GB",
                  &lv_font_montserrat_14, &disk_bar, &disk_val_label);
    create_widget(container, "Temp", 0xe94560, 0, "--.- C",
                  &lv_font_montserrat_14, NULL, &temp_val_label);
    create_widget(container, "Uptime", 0x0f3460, 0, "0d 0h 0m",
                  &lv_font_montserrat_14, NULL, &uptime_val_label);

    return screen;
}

void gui_update_clock(void) {
    if (!time_label || !date_label) return;

    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    char buf[64];

    strftime(buf, sizeof(buf), "%H:%M", tm);
    lv_label_set_text(time_label, buf);

    strftime(buf, sizeof(buf), "%A, %B %-d", tm);
    lv_label_set_text(date_label, buf);
}

void gui_update_dashboard(lv_obj_t *screen, const system_stats_t *stats) {
    (void)screen;
    char text[64];

    if (cpu_bar)
        lv_bar_set_value(cpu_bar, (int32_t)stats->cpu_usage, LV_ANIM_OFF);
    if (cpu_val_label) {
        snprintf(text, sizeof(text), "%.0f%%", stats->cpu_usage);
        lv_label_set_text(cpu_val_label, text);
    }

    if (ram_bar)
        lv_bar_set_value(ram_bar, (int32_t)stats->ram_usage, LV_ANIM_OFF);
    if (ram_val_label) {
        snprintf(text, sizeof(text), "%.1f / %.1f GB",
                 stats->ram_used_mb / 1024.0f, stats->ram_total_mb / 1024.0f);
        lv_label_set_text(ram_val_label, text);
    }

    if (disk_bar)
        lv_bar_set_value(disk_bar, (int32_t)stats->disk_usage, LV_ANIM_OFF);
    if (disk_val_label) {
        snprintf(text, sizeof(text), "%.1f / %.1f GB", stats->disk_used_gb, stats->disk_total_gb);
        lv_label_set_text(disk_val_label, text);
    }

    if (temp_val_label) {
        if (stats->temp_c > 0)
            snprintf(text, sizeof(text), "%.1f C", stats->temp_c);
        else
            snprintf(text, sizeof(text), "--.- C");
        lv_label_set_text(temp_val_label, text);
    }

    if (uptime_val_label) {
        uint64_t days = stats->uptime_seconds / 86400;
        uint64_t hours = (stats->uptime_seconds % 86400) / 3600;
        uint64_t mins = (stats->uptime_seconds % 3600) / 60;
        snprintf(text, sizeof(text), "%llud %lluh %llum",
                 (unsigned long long)days,
                 (unsigned long long)hours,
                 (unsigned long long)mins);
        lv_label_set_text(uptime_val_label, text);
    }
}

void gui_cleanup(void) {
    if (wp_buf) { free(wp_buf); wp_buf = NULL; }
    time_label = NULL;
    date_label = NULL;
    cpu_bar = NULL;
    cpu_val_label = NULL;
    ram_bar = NULL;
    ram_val_label = NULL;
    disk_bar = NULL;
    disk_val_label = NULL;
    temp_val_label = NULL;
    uptime_val_label = NULL;
}
