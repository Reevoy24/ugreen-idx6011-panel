#ifndef NET_STATS_H
#define NET_STATS_H

#define NET_MAX_IFACES 4

typedef struct {
    char name[16];
    int link_up;
    char ipv4[20];
    char ipv6[44];
    float rx_bps;
    float tx_bps;
} net_iface_t;

typedef struct {
    float total_rx_bps;
    float total_tx_bps;
    int iface_count;
    net_iface_t ifaces[NET_MAX_IFACES];
} net_stats_t;

/* Samples /proc/net/dev (rates need two calls; first returns rates of 0). */
int net_stats_collect(net_stats_t *out);

#endif
