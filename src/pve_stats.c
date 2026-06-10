#include "pve_stats.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>

#define QEMU_CONF_DIR "/etc/pve/qemu-server"
#define LXC_CONF_DIR  "/etc/pve/lxc"

static int dir_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

/* first "key: value" occurrence from a small config file */
static void conf_get(const char *path, const char *key, char *buf, size_t n)
{
    buf[0] = '\0';
    FILE *fp = fopen(path, "r");
    if (!fp) return;
    char line[256];
    size_t klen = strlen(key);
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, key, klen) == 0 && line[klen] == ':') {
            const char *v = line + klen + 1;
            while (*v == ' ' || *v == '\t') v++;
            snprintf(buf, n, "%s", v);
            buf[strcspn(buf, "\r\n")] = '\0';
            break;
        }
    }
    fclose(fp);
}

static int vm_running(int vmid)
{
    char path[64];
    snprintf(path, sizeof(path), "/run/qemu-server/%d.pid", vmid);
    FILE *fp = fopen(path, "r");
    if (!fp) return 0;
    int pid = 0;
    int n = fscanf(fp, "%d", &pid);
    fclose(fp);
    if (n != 1 || pid <= 0) return 0;
    snprintf(path, sizeof(path), "/proc/%d", pid);
    return dir_exists(path);
}

static int lxc_running(int vmid)
{
    char path[64];
    snprintf(path, sizeof(path), "/sys/fs/cgroup/lxc/%d", vmid);
    return dir_exists(path);
}

static void scan_guests(pve_stats_t *out, const char *conf_dir, int is_lxc)
{
    DIR *dir = opendir(conf_dir);
    if (!dir) return;

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        int vmid;
        char rest[16];
        if (sscanf(ent->d_name, "%d.con%15s", &vmid, rest) != 2 || strcmp(rest, "f") != 0)
            continue;

        char conf[320], name[64];
        snprintf(conf, sizeof(conf), "%s/%s", conf_dir, ent->d_name);
        conf_get(conf, is_lxc ? "hostname" : "name", name, sizeof(name));
        if (!name[0])
            snprintf(name, sizeof(name), "%s %d", is_lxc ? "CT" : "VM", vmid);

        int running = is_lxc ? lxc_running(vmid) : vm_running(vmid);
        if (is_lxc) { out->lxc_total++; out->lxc_running += running; }
        else        { out->vm_total++;  out->vm_running += running; }

        if (out->count < PVE_MAX_GUESTS) {
            pve_guest_t *g = &out->guests[out->count++];
            snprintf(g->name, sizeof(g->name), "%.23s", name);
            g->vmid = vmid;
            g->running = running;
            g->is_lxc = is_lxc;
        }
    }
    closedir(dir);
}

static int cmp_vmid(const void *a, const void *b)
{
    return ((const pve_guest_t *)a)->vmid - ((const pve_guest_t *)b)->vmid;
}

int pve_stats_collect(pve_stats_t *out)
{
    memset(out, 0, sizeof(*out));

    out->available = dir_exists(QEMU_CONF_DIR) || dir_exists(LXC_CONF_DIR);
    if (!out->available)
        return 0;

    scan_guests(out, QEMU_CONF_DIR, 0);
    scan_guests(out, LXC_CONF_DIR, 1);
    qsort(out->guests, out->count, sizeof(out->guests[0]), cmp_vmid);
    return 0;
}
