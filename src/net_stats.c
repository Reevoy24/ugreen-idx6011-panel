#include "net_stats.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

/* Interfaces worth showing: physical NICs, bonds, bridges with an address.
 * Proxmox creates lots of plumbing (fwbr/fwpr/fwln/veth/tap) we hide. */
static int iface_interesting(const char *name)
{
    if (strncmp(name, "lo", 2) == 0) return 0;
    if (strncmp(name, "veth", 4) == 0) return 0;
    if (strncmp(name, "tap", 3) == 0) return 0;
    if (strncmp(name, "fwbr", 4) == 0) return 0;
    if (strncmp(name, "fwpr", 4) == 0) return 0;
    if (strncmp(name, "fwln", 4) == 0) return 0;
    if (strncmp(name, "docker", 6) == 0) return 0;
    return strncmp(name, "en", 2) == 0 || strncmp(name, "eth", 3) == 0 ||
           strncmp(name, "vmbr", 4) == 0 || strncmp(name, "bond", 4) == 0 ||
           strncmp(name, "br", 2) == 0 || strncmp(name, "wl", 2) == 0;
}

static int iface_is_physical(const char *name)
{
    return strncmp(name, "en", 2) == 0 || strncmp(name, "eth", 3) == 0 ||
           strncmp(name, "wl", 2) == 0;
}

static int read_carrier(const char *name)
{
    char path[80];
    snprintf(path, sizeof(path), "/sys/class/net/%s/carrier", name);
    FILE *fp = fopen(path, "r");
    if (!fp) return 0;
    int c = fgetc(fp);
    fclose(fp);
    return c == '1';
}

static void fill_addrs(net_stats_t *out)
{
    struct ifaddrs *ifa0 = NULL;
    if (getifaddrs(&ifa0) != 0) return;

    for (struct ifaddrs *ifa = ifa0; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || !ifa->ifa_name) continue;
        net_iface_t *nif = NULL;
        for (int i = 0; i < out->iface_count; i++) {
            if (strcmp(out->ifaces[i].name, ifa->ifa_name) == 0) {
                nif = &out->ifaces[i];
                break;
            }
        }
        if (!nif) continue;

        if (ifa->ifa_addr->sa_family == AF_INET && !nif->ipv4[0]) {
            struct sockaddr_in *sa = (struct sockaddr_in *)ifa->ifa_addr;
            inet_ntop(AF_INET, &sa->sin_addr, nif->ipv4, sizeof(nif->ipv4));
        } else if (ifa->ifa_addr->sa_family == AF_INET6) {
            struct sockaddr_in6 *sa6 = (struct sockaddr_in6 *)ifa->ifa_addr;
            int is_ll = IN6_IS_ADDR_LINKLOCAL(&sa6->sin6_addr);
            /* prefer a global address; fall back to link-local */
            if (!nif->ipv6[0] || (!is_ll && strncmp(nif->ipv6, "fe80", 4) == 0))
                inet_ntop(AF_INET6, &sa6->sin6_addr, nif->ipv6, sizeof(nif->ipv6));
        }
    }
    freeifaddrs(ifa0);
}

int net_stats_collect(net_stats_t *out)
{
    static char prev_name[NET_MAX_IFACES][16];
    static unsigned long long prev_rx[NET_MAX_IFACES], prev_tx[NET_MAX_IFACES];
    static struct timespec prev_ts;
    static int have_prev = 0;

    memset(out, 0, sizeof(*out));

    FILE *fp = fopen("/proc/net/dev", "r");
    if (!fp) return -1;

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    float dt = 0.0f;
    if (have_prev)
        dt = (now.tv_sec - prev_ts.tv_sec) + (now.tv_nsec - prev_ts.tv_nsec) / 1e9f;

    char line[512];
    /* skip the two header lines */
    if (!fgets(line, sizeof(line), fp) || !fgets(line, sizeof(line), fp)) {
        fclose(fp);
        return -1;
    }

    unsigned long long cur_rx[NET_MAX_IFACES] = {0}, cur_tx[NET_MAX_IFACES] = {0};

    while (fgets(line, sizeof(line), fp) && out->iface_count < NET_MAX_IFACES) {
        char name[32];
        unsigned long long rx, tx, dummy;
        char *colon = strchr(line, ':');
        if (!colon) continue;
        *colon = ' ';
        if (sscanf(line, " %31s %llu %llu %llu %llu %llu %llu %llu %llu %llu",
                   name, &rx, &dummy, &dummy, &dummy, &dummy, &dummy, &dummy, &dummy,
                   &tx) != 10)
            continue;
        if (!iface_interesting(name)) continue;

        int idx = out->iface_count++;
        net_iface_t *nif = &out->ifaces[idx];
        snprintf(nif->name, sizeof(nif->name), "%.15s", name);
        nif->link_up = read_carrier(name);

        cur_rx[idx] = rx;
        cur_tx[idx] = tx;
        if (have_prev && dt > 0.05f) {
            for (int p = 0; p < NET_MAX_IFACES; p++) {
                if (strcmp(prev_name[p], name) == 0) {
                    if (rx >= prev_rx[p]) nif->rx_bps = (rx - prev_rx[p]) / dt;
                    if (tx >= prev_tx[p]) nif->tx_bps = (tx - prev_tx[p]) / dt;
                    break;
                }
            }
        }
    }
    fclose(fp);

    /* totals over physical NICs only (bridges re-count member traffic);
     * if no physical NIC made the list, sum what we have */
    int phys = 0;
    for (int i = 0; i < out->iface_count; i++) {
        if (iface_is_physical(out->ifaces[i].name)) {
            out->total_rx_bps += out->ifaces[i].rx_bps;
            out->total_tx_bps += out->ifaces[i].tx_bps;
            phys++;
        }
    }
    if (!phys) {
        for (int i = 0; i < out->iface_count; i++) {
            out->total_rx_bps += out->ifaces[i].rx_bps;
            out->total_tx_bps += out->ifaces[i].tx_bps;
        }
    }

    fill_addrs(out);

    for (int i = 0; i < out->iface_count; i++) {
        snprintf(prev_name[i], sizeof(prev_name[i]), "%s", out->ifaces[i].name);
        prev_rx[i] = cur_rx[i];
        prev_tx[i] = cur_tx[i];
    }
    for (int i = out->iface_count; i < NET_MAX_IFACES; i++)
        prev_name[i][0] = '\0';
    prev_ts = now;
    have_prev = 1;
    return 0;
}
