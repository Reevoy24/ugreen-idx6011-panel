/* Renders every dashboard page with mock data into raw ARGB framebuffers.
 * Host-only development tool — lets the GUI be designed without the NAS.
 * Convert the .raw files to PNG with tools/raw2png.py. */
#include "lvgl/lvgl.h"
#include "gui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 258
#define H 960
#define SPARK_HISTORY_SEED 40

static uint32_t fake_tick;
static uint32_t tick_cb(void) { return fake_tick; }

static void flush_cb(lv_display_t *d, const lv_area_t *a, uint8_t *p)
{
    LV_UNUSED(a);
    LV_UNUSED(p);
    lv_display_flush_ready(d);
}

static void pump(int frames)
{
    for (int i = 0; i < frames; i++) {
        fake_tick += 33;
        lv_timer_handler();
    }
}

static uint8_t fb[W * H * 4];

int main(void)
{
    lv_init();
    lv_tick_set_cb(tick_cb);

    lv_display_t *disp = lv_display_create(W, H);
    lv_display_set_buffers(disp, fb, NULL, sizeof(fb), LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(disp, flush_cb);

    static ui_state_t state = {
        .brightness = 80,
        .backlight_timeout = 300,
        .wallpaper = "aurora",
        .language = "de",
    };
    gui_setup_t setup = {
        .show_opnsense = 1,
        .show_pve = 1,
        .wan_max_mbps = 1000,
        .state = &state,
    };
    gui_create_dashboard(&setup);

    /* --- mock data, roughly matching the UGOS marketing screens --- */
    system_stats_t st = {
        .cpu_usage = 50.6f, .ram_usage = 27.0f,
        .ram_used_mb = 1740.0f, .ram_total_mb = 63800.0f,
        .disk_usage = 6.4f, .disk_used_gb = 6.0f, .disk_total_gb = 94.0f,
        .temp_c = 36.6f,
        .uptime_seconds = 3 * 86400 + 4 * 3600 + 12 * 60,
    };
    for (int i = 0; i < SPARK_HISTORY_SEED; i++) {
        st.cpu_usage = 22.0f + (float)((i * 13) % 58);
        gui_update_dashboard(&st);
        gui_update_gpu(12.0f + (float)((i * 17) % 55));
    }
    st.cpu_usage = 50.6f;
    gui_update_dashboard(&st);
    gui_update_gpu(40.5f);

    net_stats_t net = {
        .total_rx_bps = 58980.0f, .total_tx_bps = 10980.0f,
        .iface_count = 2,
        .ifaces = {
            { .name = "vmbr0", .link_up = 1, .ipv4 = "192.168.178.25",
              .ipv6 = "fe80::1d80:7a0b:fe8c:9b21", .rx_bps = 51230.0f, .tx_bps = 9480.0f },
            { .name = "eno1", .link_up = 1, .ipv4 = "192.168.178.26",
              .ipv6 = "fe80::9a01:a7ff:fe11:2a4f", .rx_bps = 7750.0f, .tx_bps = 1500.0f },
        },
    };
    gui_update_net(&net);

    disk_stats_t dk = {
        .count = 5,
        .disks = {
            { .dev = "sda",     .is_nvme = 0, .idx = 1, .size_tb = 1.8f, .temp_c = 29, .online = 1 },
            { .dev = "sdb",     .is_nvme = 0, .idx = 2, .size_tb = 1.8f, .temp_c = 29, .online = 1 },
            { .dev = "sdc",     .is_nvme = 0, .idx = 3, .size_tb = 1.8f, .temp_c = 30, .online = 1 },
            { .dev = "sdd",     .is_nvme = 0, .idx = 4, .size_tb = 1.8f, .temp_c = 28, .online = 1 },
            { .dev = "nvme0n1", .is_nvme = 1, .idx = 1, .size_tb = 0.5f, .temp_c = 38, .online = 1 },
        },
    };
    gui_update_disks(&dk);

    pve_stats_t pv = {
        .available = 1,
        .vm_total = 3, .vm_running = 2,
        .lxc_total = 2, .lxc_running = 2,
        .count = 5,
        .guests = {
            { .name = "truenas",       .vmid = 100, .running = 1, .is_lxc = 0 },
            { .name = "homeassistant", .vmid = 101, .running = 1, .is_lxc = 0 },
            { .name = "win11",         .vmid = 102, .running = 0, .is_lxc = 0 },
            { .name = "docker",        .vmid = 200, .running = 1, .is_lxc = 1 },
            { .name = "pihole",        .vmid = 201, .running = 1, .is_lxc = 1 },
        },
    };
    gui_update_pve(&pv);

    opnsense_stats_t op = {
        .gw_rtt_ms = 12, .gw_status = "online", .update_status = "aktuell",
        .dhcp_leases = 23, .dns_blocked_pct = 18,
    };
    gui_update_opnsense(&op);
    gui_update_wan_throughput(420e6f, 38e6f);
    gui_update_clock();

    pump(10);

    for (int p = 0; p < gui_page_count(); p++) {
        gui_show_page(p);
        pump(10);
        char path[64];
        snprintf(path, sizeof(path), "mockups/page_%d.raw", p);
        FILE *f = fopen(path, "wb");
        if (!f) { perror(path); return 1; }
        fwrite(fb, 1, sizeof(fb), f);
        fclose(f);
        fprintf(stderr, "wrote %s\n", path);
    }

    /* settings panel, opened over the home page */
    gui_show_page(0);
    pump(4);
    gui_settings_open();
    pump(14); /* let the slide-in animation finish */
    FILE *f = fopen("mockups/page_panel.raw", "wb");
    if (f) {
        fwrite(fb, 1, sizeof(fb), f);
        fclose(f);
        fprintf(stderr, "wrote mockups/page_panel.raw\n");
    }

    fprintf(stderr, "done: %d pages + settings panel\n", gui_page_count());
    return 0;
}
