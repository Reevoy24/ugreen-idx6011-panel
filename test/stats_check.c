/* Quick host-side sanity check for the stat collectors (no LVGL needed):
 *   gcc -Iinclude test/stats_check.c src/net_stats.c src/disk_stats.c \
 *       src/pve_stats.c src/gpu_stats.c $(pkg-config --cflags libdrm) -o stats-check
 */
#include "net_stats.h"
#include "disk_stats.h"
#include "pve_stats.h"
#include "gpu_stats.h"
#include <stdio.h>
#include <unistd.h>

int main(void)
{
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
    return 0;
}
