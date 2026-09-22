/* Quick host-side sanity check for the stat collectors (no LVGL needed):
 *   gcc -Iinclude test/stats_check.c src/net_stats.c src/disk_stats.c \
 *       src/pve_stats.c src/gpu_stats.c $(pkg-config --cflags libdrm) -o stats-check
 */
#include "net_stats.h"
#include "disk_stats.h"
#include "pve_stats.h"
#include "gpu_stats.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <utime.h>
#include <time.h>

/* ---- external drive-temperature source ----
 * The one part of disk_stats that is pure parsing, so it can be checked without
 * the machine actually having the drives. Covers what the fan curve depends on:
 * the hottest value, "spun down" vs. "no reading at all", and above all that a
 * file which stopped being updated reports STALE instead of a frozen number. */
static int fails;

static void expect(const char *what, int got, int want)
{
    if (got == want) return;
    printf("  FAIL %-34s got %d, want %d\n", what, got, want);
    fails++;
}

static const char *ext_write(const char *body)
{
    static char path[] = "/tmp/ug-ext-temps.XXXXXX";
    static int made;
    if (!made) { close(mkstemp(path)); made = 1; }
    FILE *f = fopen(path, "w");
    if (f) { fputs(body, f); fclose(f); }
    return path;
}

static void check_external(void)
{
    int max_c = 0;
    const char *path;

    printf("external source:\n");

    disk_stats_set_external("", 0);
    expect("not configured", disk_stats_external_max(&max_c), DISK_EXT_OFF);

    disk_stats_set_external("/tmp/ug-ext-temps.absent", 120);
    expect("configured but missing", disk_stats_external_max(&max_c), DISK_EXT_STALE);

    path = ext_write("# pool\nsda=41\nsdb=39,16.0\nsdc=*\n");
    disk_stats_set_external(path, 120);
    expect("three drives listed", disk_stats_external_max(&max_c), 3);
    expect("hottest of the three", max_c, 41);

    ext_write("sda=*\nsdb=-\n");
    expect("all spun down: still read", disk_stats_external_max(&max_c), 2);
    expect("all spun down: no temp", max_c, -1);

    ext_write("max=44\n");
    expect("aggregate only", disk_stats_external_max(&max_c), 1);
    expect("aggregate value", max_c, 44);

    ext_write("sda=41\nmax=52\n");
    expect("aggregate wins when hotter", disk_stats_external_max(&max_c), 2);
    expect("aggregate value used", max_c, 52);

    ext_write("this file is not the format\n");
    expect("garbage is not a reading", disk_stats_external_max(&max_c), DISK_EXT_STALE);

    /* the whole point of max_age: a helper that died must not leave the fans
     * regulating on the last temperature it happened to report */
    path = ext_write("sda=41\n");
    struct utimbuf old = { .actime = time(NULL) - 600, .modtime = time(NULL) - 600 };
    utime(path, &old);
    expect("older than max_age", disk_stats_external_max(&max_c), DISK_EXT_STALE);
    disk_stats_set_external(path, 0);
    expect("max_age 0 never expires", disk_stats_external_max(&max_c), 1);

    /* push transport: same format, arriving as an HTTP body */
    char err[128] = "";
    int drives = 0;
    disk_stats_set_external(path, 120);
    expect("push accepted", disk_stats_write_external("sdx=37\nsdy=*\n", &drives, err, sizeof(err)), 0);
    expect("push drive count", drives, 2);
    expect("pushed value readable", disk_stats_external_max(&max_c), 2);
    expect("pushed temperature", max_c, 37);
    expect("push rejects junk", disk_stats_write_external("hello", &drives, err, sizeof(err)), -1);
    disk_stats_set_external("", 0);
    expect("push needs a configured file",
           disk_stats_write_external("sda=40\n", &drives, err, sizeof(err)), -2);

    /* merge into the drive list the panel and the web dashboard render: a name
     * the host also has locally only gets its temperature replaced, one it has
     * no block device for is appended as a drive of its own */
    disk_stats_set_external(ext_write("sda=44\nremote0=39,18.0\n"), 120);
    disk_stats_t merged;
    if (disk_stats_collect(&merged) == 0) {
        int remote_seen = 0, local_seen = 0;
        float remote_temp = -1, remote_size = -1, local_temp = -1;
        for (int i = 0; i < merged.count; i++) {
            if (strcmp(merged.disks[i].dev, "remote0") == 0) {
                remote_seen = 1;
                remote_temp = merged.disks[i].temp_c;
                remote_size = merged.disks[i].size_tb;
            } else if (strcmp(merged.disks[i].dev, "sda") == 0) {
                local_seen = 1;
                local_temp = merged.disks[i].temp_c;
            }
        }
        expect("drive with no block device listed", remote_seen, 1);
        expect("its reported temperature", (int)remote_temp, 39);
        expect("its reported capacity", (int)(remote_size * 10), 180);
        if (local_seen)   /* only where the host really has an sda */
            expect("local drive temperature overridden", (int)local_temp, 44);
    } else {
        printf("  SKIP merge check (no /sys/block)\n");
    }
    disk_stats_set_external("", 0);

    remove(path);
    printf("  %s\n", fails ? "FAILURES ABOVE" : "ok");
}

int main(void)
{
    check_external();

    net_stats_t net;
    net_stats_collect(&net);
    sleep(1);
    if (net_stats_collect(&net) == 0) {
        printf("net: %d iface(s), total rx %.0f B/s tx %.0f B/s\n",
               net.iface_count, net.total_rx_bps, net.total_tx_bps);
        for (int i = 0; i < net.iface_count; i++)
            printf("  %-8s up=%d ipv4=%-15s ipv6=%s rx=%.0f tx=%.0f\n",
                   net.ifaces[i].name, net.ifaces[i].link_up,
                   net.ifaces[i].ipv4[0] ? net.ifaces[i].ipv4 : "-",
                   net.ifaces[i].ipv6[0] ? net.ifaces[i].ipv6 : "-",
                   net.ifaces[i].rx_bps, net.ifaces[i].tx_bps);
    }

    disk_stats_t disks;
    if (disk_stats_collect(&disks) == 0) {
        printf("disks: %d\n", disks.count);
        for (int i = 0; i < disks.count; i++)
            printf("  %-10s nvme=%d idx=%d %.2f TB temp=%.0f\n",
                   disks.disks[i].dev, disks.disks[i].is_nvme, disks.disks[i].idx,
                   disks.disks[i].size_tb, disks.disks[i].temp_c);
    }

    pve_stats_t pve;
    pve_stats_collect(&pve);
    printf("pve: available=%d vm=%d/%d lxc=%d/%d guests=%d\n",
           pve.available, pve.vm_running, pve.vm_total,
           pve.lxc_running, pve.lxc_total, pve.count);
    for (int i = 0; i < pve.count; i++)
        printf("  %4d %-20s running=%d lxc=%d\n", pve.guests[i].vmid,
               pve.guests[i].name, pve.guests[i].running, pve.guests[i].is_lxc);

    int g = gpu_stats_init();
    printf("gpu: init=%d", g);
    if (g == 0) {
        gpu_stats_usage();
        sleep(1);
        printf(" usage=%.1f%%", gpu_stats_usage());
    }
    printf("\n");
    gpu_stats_cleanup();
    return fails ? 1 : 0;
}
