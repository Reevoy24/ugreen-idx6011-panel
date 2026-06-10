#ifndef PVE_STATS_H
#define PVE_STATS_H

#define PVE_MAX_GUESTS 12

typedef struct {
    char name[24];
    int vmid;
    int running;
    int is_lxc;
} pve_guest_t;

typedef struct {
    int available;    /* 0 if this host is not a Proxmox node */
    int vm_total, vm_running;
    int lxc_total, lxc_running;
    int count;
    pve_guest_t guests[PVE_MAX_GUESTS];
} pve_stats_t;

int pve_stats_collect(pve_stats_t *out);

#endif
