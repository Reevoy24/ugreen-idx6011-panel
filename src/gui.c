#include "gui.h"
#include <stdio.h>
#include <string.h>
#include "lvgl/lvgl.h"

#define WIDGET_WIDTH 200
#define WIDGET_HEIGHT 100

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
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_set_size(box, WIDGET_WIDTH, WIDGET_HEIGHT);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(box, 15, 0);
    lv_obj_set_style_radius(box, 15, 0);
    lv_obj_set_style_bg_color(box, lv_color_hex(0x16213e), 0);

    lv_obj_t *label = lv_label_create(box);
    lv_label_set_text(label, title);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_18, 0);

    if (has_bar && bar_out) {
        *bar_out = lv_bar_create(box);
        lv_bar_set_range(*bar_out, 0, 100);
        lv_bar_set_value(*bar_out, 0, LV_ANIM_OFF);
        lv_obj_set_size(*bar_out, WIDGET_WIDTH - 20, 15);
    }

    if (label_out) {
        *label_out = lv_label_create(box);
        lv_label_set_text(*label_out, default_val);
        lv_obj_set_style_text_color(*label_out, lv_color_hex(0xffffff), 0);
        lv_obj_set_style_text_font(*label_out, val_font, 0);
    }

    return box;
}

lv_obj_t *gui_create_dashboard(void) {
    lv_obj_t *screen = lv_scr_act();

    lv_obj_set_style_bg_color(screen, lv_color_hex(0x1a1a2e), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    lv_obj_t *container = lv_obj_create(screen);
    lv_obj_set_size(container, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(container, 10, 0);
    lv_obj_set_style_radius(container, 10, 0);

    create_widget(container, "CPU Usage", 0xe94560, 1, "0%",
                  &lv_font_montserrat_14, &cpu_bar, &cpu_val_label);
    create_widget(container, "RAM Usage", 0x0f3460, 1, "0/0 MB",
                  &lv_font_montserrat_14, &ram_bar, &ram_val_label);
    create_widget(container, "Disk Usage", 0x533483, 1, "0/0 GB",
                  &lv_font_montserrat_14, &disk_bar, &disk_val_label);
    create_widget(container, "Temperature", 0xe94560, 0, "--.- C",
                  &lv_font_montserrat_20, NULL, &temp_val_label);
    create_widget(container, "Uptime", 0x0f3460, 0, "0d 0h 0m",
                  &lv_font_montserrat_16, NULL, &uptime_val_label);

    lv_obj_t *header = lv_label_create(screen);
    lv_label_set_text(header, "Status");
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_set_style_text_color(header, lv_color_hex(0xe94560), 0);
    lv_obj_set_style_text_font(header, &lv_font_montserrat_24, 0);

    lv_obj_t *footer = lv_label_create(screen);
    lv_label_set_text(footer, "Ugreen Display v1.0 | Auto-refresh");
    lv_obj_align(footer, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_text_color(footer, lv_color_hex(0x533483), 0);
    lv_obj_set_style_text_font(footer, &lv_font_montserrat_14, 0);

    return screen;
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
        snprintf(text, sizeof(text), "%.0f/%.0f MB", stats->ram_used_mb, stats->ram_total_mb);
        lv_label_set_text(ram_val_label, text);
    }

    if (disk_bar)
        lv_bar_set_value(disk_bar, (int32_t)stats->disk_usage, LV_ANIM_OFF);
    if (disk_val_label) {
        snprintf(text, sizeof(text), "%.1f/%.1f GB", stats->disk_used_gb, stats->disk_total_gb);
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
    cpu_bar = NULL;
    cpu_val_label = NULL;
    ram_bar = NULL;
    ram_val_label = NULL;
    disk_bar = NULL;
    disk_val_label = NULL;
    temp_val_label = NULL;
    uptime_val_label = NULL;
}
