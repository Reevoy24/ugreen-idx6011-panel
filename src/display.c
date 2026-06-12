#include "display.h"
#include "include/custom_tick.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <xf86drm.h>
#include <xf86drmMode.h>

static lv_display_t *disp = NULL;
static int dbg = 0;

#define LOGI(...) do { fprintf(stderr, "display: " __VA_ARGS__); fputc('\n', stderr); } while (0)
#define LOGD(...) do { if (dbg) { fprintf(stderr, "display(debug): " __VA_ARGS__); fputc('\n', stderr); } } while (0)

typedef struct {
    char device[272];       /* /dev/dri/cardN that has the usable connector */
    uint32_t connector_id;
    char connector_name[40];
    uint32_t width, height, refresh;
    int prio;               /* 0 = internal panel type, 1 = external */
} probe_result_t;

/* In auto mode prefer internal panel connectors: an external monitor on a
 * DP/HDMI port can also be "connected", and enumeration order must not
 * decide whether the dashboard lands on the NAS panel or the big screen. */
static int connector_prio(uint32_t type)
{
    switch (type) {
        case DRM_MODE_CONNECTOR_eDP:
        case DRM_MODE_CONNECTOR_DSI:
        case DRM_MODE_CONNECTOR_LVDS:
            return 0;
        default:
            return 1;
    }
}

/* Builds the kernel-style connector name, e.g. "eDP-1", "DP-3", "DSI-1" —
 * matches what /sys/class/drm/card0-eDP-1 etc. are named after. */
static void connector_name(const drmModeConnector *conn, char *buf, size_t n)
{
    const char *type = drmModeGetConnectorTypeName(conn->connector_type);
    if (!type) type = "Unknown";
    snprintf(buf, n, "%s-%d", type, conn->connector_type_id);
}

static const char *connection_str(drmModeConnection c)
{
    switch (c) {
        case DRM_MODE_CONNECTED: return "connected";
        case DRM_MODE_DISCONNECTED: return "disconnected";
        default: return "unknown";
    }
}

/* Inspect one DRM device: log the driver and every connector with its status,
 * and fill `out` with the first usable (connected, has modes) connector that
 * matches `want` ("auto"/""/NULL = any). Returns 0 if one was found. */
static int probe_card(const char *path, const char *want, probe_result_t *out, int log_inventory)
{
    int want_any = (!want || !want[0] || strcasecmp(want, "auto") == 0);
    int found = -1;

    int fd = open(path, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        if (log_inventory)
            LOGI("%s: cannot open: %s", path, strerror(errno));
        return -1;
    }

    drmVersionPtr ver = drmGetVersion(fd);
    if (log_inventory && ver)
        LOGI("%s: driver \"%s\"", path, ver->name ? ver->name : "?");
    if (ver) drmFreeVersion(ver);

    drmModeRes *res = drmModeGetResources(fd);
    if (!res) {
        if (log_inventory)
            LOGI("%s: drmModeGetResources failed (%s) — not a modesetting device",
                 path, strerror(errno));
        close(fd);
        return -1;
    }

    if (log_inventory)
        LOGI("%s: %d connector(s), %d CRTC(s)", path, res->count_connectors, res->count_crtcs);
    if (res->count_crtcs <= 0 && log_inventory)
        LOGI("%s: no CRTCs — kernel driver did not bring up any display pipe", path);

    for (int i = 0; i < res->count_connectors; i++) {
        drmModeConnector *conn = drmModeGetConnector(fd, res->connectors[i]);
        if (!conn) continue;

        char name[40];
        connector_name(conn, name, sizeof(name));

        char idbuf[16];
        snprintf(idbuf, sizeof(idbuf), "%u", conn->connector_id);
        int matches = want_any || strcasecmp(want, name) == 0 || strcmp(want, idbuf) == 0;
        int usable = (conn->connection == DRM_MODE_CONNECTED && conn->count_modes > 0);

        if (log_inventory) {
            if (conn->count_modes > 0)
                LOGI("%s: connector %s (id %u): %s, %d mode(s), first %dx%d@%d",
                     path, name, conn->connector_id, connection_str(conn->connection),
                     conn->count_modes, conn->modes[0].hdisplay, conn->modes[0].vdisplay,
                     conn->modes[0].vrefresh);
            else
                LOGI("%s: connector %s (id %u): %s, no modes",
                     path, name, conn->connector_id, connection_str(conn->connection));

            if (matches && !want_any && !usable)
                LOGI("%s: configured connector \"%s\" matches %s but it is not usable (%s, %d modes)",
                     path, want, name, connection_str(conn->connection), conn->count_modes);
        }

        int prio = connector_prio(conn->connector_type);
        if (matches && usable && (found != 0 || prio < out->prio)) {
            snprintf(out->device, sizeof(out->device), "%s", path);
            out->connector_id = conn->connector_id;
            snprintf(out->connector_name, sizeof(out->connector_name), "%s", name);
            out->width = conn->modes[0].hdisplay;
            out->height = conn->modes[0].vdisplay;
            out->refresh = conn->modes[0].vrefresh;
            out->prio = prio;
            found = 0;
            /* keep iterating so the full inventory still gets logged and an
             * internal panel later in the list can still win */
        }

        drmModeFreeConnector(conn);
    }

    drmModeFreeResources(res);
    close(fd);
    return found;
}

static int card_filter(const struct dirent *e)
{
    return strncmp(e->d_name, "card", 4) == 0 && e->d_name[4] >= '0' && e->d_name[4] <= '9';
}

/* Scan `forced` (if set) or all /dev/dri/card* for a usable connector. */
static int probe_devices(const char *forced, const char *want, probe_result_t *out, int log_inventory)
{
    if (forced && forced[0])
        return probe_card(forced, want, out, log_inventory);

    struct dirent **list = NULL;
    int n = scandir("/dev/dri", &list, card_filter, alphasort);
    if (n < 0) {
        if (log_inventory)
            LOGI("cannot scan /dev/dri (%s) — is a DRM/KMS driver loaded?", strerror(errno));
        return -1;
    }
    if (n == 0) {
        if (log_inventory)
            LOGI("no /dev/dri/card* devices — kernel exposes no DRM device");
        free(list);
        return -1;
    }

    int found = -1;
    for (int i = 0; i < n; i++) {
        char path[272];
        snprintf(path, sizeof(path), "/dev/dri/%s", list[i]->d_name);
        probe_result_t tmp;
        if (probe_card(path, want, &tmp, log_inventory) == 0 &&
            (found != 0 || tmp.prio < out->prio)) {
            *out = tmp;
            found = 0;
        }
        free(list[i]);
    }
    free(list);
    return found;
}

int display_init(const config_t *config)
{
    dbg = config->debug;

    const char *forced = config->drm_device[0] ? config->drm_device : NULL;
    const char *want = config->connector;
    int timeout = config->drm_probe_timeout;
    if (timeout < 0) timeout = 0;

    if (forced)
        LOGI("config: drm_device=%s", forced);
    if (want[0] && strcasecmp(want, "auto") != 0)
        LOGI("config: connector=%s", want);

    /* Pre-flight probe with libdrm before handing anything to LVGL, so the
     * journal shows exactly which devices/connectors exist and why init
     * fails. The panel connector can appear a little after boot depending on
     * driver load order, so poll for it instead of failing on first look. */
    probe_result_t pr;
    int ret = probe_devices(forced, want, &pr, 1);
    if (ret != 0 && timeout > 0) {
        LOGI("no usable DRM connector yet, waiting up to %d s for one to appear...", timeout);
        for (int waited = 0; ret != 0 && waited < timeout; waited++) {
            sleep(1);
            ret = probe_devices(forced, want, &pr, dbg);
        }
        if (ret != 0) {
            LOGI("final DRM state after waiting %d s:", timeout);
            probe_devices(forced, want, &pr, 1);
        }
    }

    if (ret != 0) {
        LOGI("No connected DRM connector found; kernel did not expose internal panel.");
        LOGI("hint: check 'dmesg | grep -iE \"drm|edp\"' for errors like 'failed to retrieve link info, disabling eDP'");
        LOGI("hint: 'for x in /sys/class/drm/card*-*/status; do echo \"$x: $(cat $x)\"; done' should list one 'connected' entry");
        return DISPLAY_NO_CONNECTOR;
    }

    LOGI("using DRM device %s, connector %s (id %u), mode %ux%u@%u",
         pr.device, pr.connector_name, pr.connector_id, pr.width, pr.height, pr.refresh);

    lv_init();
    lv_tick_set_cb(custom_tick_get);

    disp = lv_linux_drm_create();
    if (!disp) {
        LOGI("lv_linux_drm_create() failed");
        return DISPLAY_ERR;
    }

    LOGD("handing %s connector id %u to LVGL", pr.device, pr.connector_id);
    if (lv_linux_drm_set_file(disp, pr.device, (int64_t)pr.connector_id) != LV_RESULT_OK) {
        LOGI("LVGL DRM setup failed on %s connector %s — no draw buffers were created (see LVGL errors above)",
             pr.device, pr.connector_name);
        return DISPLAY_ERR;
    }

    int32_t w = lv_display_get_horizontal_resolution(disp);
    int32_t h = lv_display_get_vertical_resolution(disp);
    if (w <= 0 || h <= 0) {
        LOGI("DRM display has no resolution — init failed");
        return DISPLAY_ERR;
    }

    LOGI("DRM display initialized: %dx%d", w, h);
    return DISPLAY_OK;
}

void display_render(void)
{
    lv_timer_handler();
}

void display_close(void)
{
    if (disp) {
        lv_display_delete(disp);
        disp = NULL;
    }
}
