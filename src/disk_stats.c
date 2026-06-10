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
    snprintf(pattern, sizeof(pattern), "/sys/block/%s/device/hwmon*/temp1_input", dev);
    patterns[0] = pattern;
    char pattern2[160];
    snprintf(pattern2, sizeof(pattern2), "/sys/block/%s/device/hwmon/hwmon*/temp1_input", dev);
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

static int cmp_name(const void *a, const void *b)
{
    return strcmp(((const disk_info_t *)a)->dev, ((const disk_info_t *)b)->dev);
}

int disk_stats_collect(disk_stats_t *out)
{
    memset(out, 0, sizeof(*out));

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
