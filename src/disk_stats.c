#include "disk_stats.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <dirent.h>
#include <glob.h>

/* whole-disk block devices only: sda..sdz, nvme0n1.. */
static int is_sata_disk(const char *n)
{
    if (strncmp(n, "sd", 2) != 0) return 0;
    for (const char *p = n + 2; *p; p++)
        if (!islower((unsigned char)*p)) return 0;
    return n[2] != '\0';
}

static int is_nvme_disk(const char *n)
{
    int ctrl, ns;
    char rest[8];
    if (sscanf(n, "nvme%dn%d%7s", &ctrl, &ns, rest) == 2)
        return 1;
    return 0;
}

static float read_size_tb(const char *dev)
{
    char path[96];
    snprintf(path, sizeof(path), "/sys/block/%.15s/size", dev);
    FILE *fp = fopen(path, "r");
    if (!fp) return 0;
    unsigned long long sectors = 0;
    int n = fscanf(fp, "%llu", &sectors);
    fclose(fp);
    if (n != 1) return 0;
    return (float)(sectors * 512.0 / 1e12);
}

/* drivetemp (SATA) and nvme expose hwmon temp under the device dir */
static float read_temp_c(const char *dev)
{
    char pattern[160];
    const char *patterns[2];
    snprintf(pattern, sizeof(pattern), "/sys/block/%.15s/device/hwmon*/temp1_input", dev);
    patterns[0] = pattern;
    char pattern2[160];
    snprintf(pattern2, sizeof(pattern2), "/sys/block/%.15s/device/hwmon/hwmon*/temp1_input", dev);
    patterns[1] = pattern2;

    for (int i = 0; i < 2; i++) {
        glob_t g;
        if (glob(patterns[i], 0, NULL, &g) == 0 && g.gl_pathc > 0) {
            FILE *fp = fopen(g.gl_pathv[0], "r");
            globfree(&g);
            if (fp) {
                long milli = 0;
                int n = fscanf(fp, "%ld", &milli);
                fclose(fp);
                if (n == 1) return milli / 1000.0f;
            }
        } else {
            globfree(&g);
        }
    }
    return -1.0f;
}

/* ---- Unraid: temps from emhttpd instead of drivetemp ----
 * emhttpd already polls every managed drive (respecting spindown: spun-down
 * drives report temp="*") into /var/local/emhttp/disks.ini. Reading that file
 * costs no disk I/O, while every drivetemp hwmon read is a live SMART query
 * that audibly unparks the heads on many HDDs. */
#define UNRAID_DISKS_INI "/var/local/emhttp/disks.ini"
#define INI_DISK_MAX 32

typedef struct { char dev[16]; float temp_c; } ini_disk_t;

/* Parse disks.ini sections into dev -> temp entries (temp -1 = spun down or
 * unknown). Returns the entry count, or -1 if the file is absent (not Unraid). */
static int unraid_ini_load(ini_disk_t *out, int max)
{
    const char *path = getenv("UG_DISKS_INI");   /* test override */
    if (!path || !*path) path = UNRAID_DISKS_INI;
    FILE *fp = fopen(path, "r");
    if (!fp) return -1;

    int n = 0;
    char line[256], dev[16] = "", val[16];
    float temp = -1.0f;
    for (;;) {
        char *got = fgets(line, sizeof(line), fp);
        if (!got || line[0] == '[') {            /* next section or EOF: commit */
            if (dev[0] && n < max) {
                snprintf(out[n].dev, sizeof(out[n].dev), "%s", dev);
                out[n].temp_c = temp;
                n++;
            }
            dev[0] = 0; temp = -1.0f;
            if (!got) break;
            continue;
        }
        if (sscanf(line, "device=\"%15[^\"]\"", val) == 1)
            snprintf(dev, sizeof(dev), "%s", val);
        else if (sscanf(line, "temp=\"%15[^\"]\"", val) == 1)
            temp = isdigit((unsigned char)val[0]) ? (float)atoi(val) : -1.0f;
    }
    fclose(fp);
    return n;
}

static int ini_find(const ini_disk_t *ini, int n, const char *dev, float *temp)
{
    for (int i = 0; i < n; i++)
        if (strcmp(ini[i].dev, dev) == 0) { *temp = ini[i].temp_c; return 1; }
    return 0;
}

int disk_stats_unraid_max(int *max_c)
{
    ini_disk_t ini[INI_DISK_MAX];
    int n = unraid_ini_load(ini, INI_DISK_MAX);
    int best = -1;
    for (int i = 0; i < n; i++)
        if (ini[i].temp_c >= 0 && (int)ini[i].temp_c > best) best = (int)ini[i].temp_c;
    *max_c = best;
    return n;
}

static int cmp_name(const void *a, const void *b)
{
    return strcmp(((const disk_info_t *)a)->dev, ((const disk_info_t *)b)->dev);
}

int disk_stats_collect(disk_stats_t *out)
{
    memset(out, 0, sizeof(*out));

    ini_disk_t ini[INI_DISK_MAX];
    int ini_n = unraid_ini_load(ini, INI_DISK_MAX);

    DIR *dir = opendir("/sys/block");
    if (!dir) return -1;

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL && out->count < DISK_MAX) {
        int nvme = is_nvme_disk(ent->d_name);
        if (!nvme && !is_sata_disk(ent->d_name)) continue;

        disk_info_t *d = &out->disks[out->count++];
        snprintf(d->dev, sizeof(d->dev), "%.15s", ent->d_name);
        d->is_nvme = nvme;
        d->size_tb = read_size_tb(ent->d_name);
        float t;
        if (ini_n > 0 && ini_find(ini, ini_n, ent->d_name, &t))
            d->temp_c = t;   /* emhttpd's value; -1 = spun down, leave it asleep */
        else
            d->temp_c = read_temp_c(ent->d_name);
        d->online = 1;
    }
    closedir(dir);

    qsort(out->disks, out->count, sizeof(out->disks[0]), cmp_name);

    int sata_idx = 0, nvme_idx = 0;
    for (int i = 0; i < out->count; i++)
        out->disks[i].idx = out->disks[i].is_nvme ? ++nvme_idx : ++sata_idx;
    return 0;
}
