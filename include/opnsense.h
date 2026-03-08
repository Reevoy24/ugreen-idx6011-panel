#ifndef OPNSENSE_H
#define OPNSENSE_H

#include "config.h"

typedef struct {
    float wan_in_bps;
    float wan_out_bps;
    int gw_rtt_ms;
    char gw_status[16];
    char update_status[32];
    int dhcp_leases;
    int dns_queries;
    int dns_blocked;
    int dns_blocked_pct;
} opnsense_stats_t;

int opnsense_init(const config_t *config);
int opnsense_collect(opnsense_stats_t *stats);
void opnsense_cleanup(void);

#endif
