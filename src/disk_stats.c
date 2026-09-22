#include "disk_stats.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <dirent.h>
#include <glob.h>
#include <time.h>
#include <unistd.h>
#include <libgen.h>
#include <sys/stat.h>
#include <sys/types.h>

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
 * that audibly unparks the heads on many drives. */
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

/* ---- generic external source (any platform) ----
 * Same idea as the Unraid path, but the file is written by whoever can actually
 * see the drives - typically a helper inside the VM that owns the HBA. Format
 * and rationale: see disk_stats.h. */
#define EXT_MAX_BYTES (16 * 1024)   /* a push body far beyond any real drive count */

static char ext_path[256] = "";
static int  ext_max_age = 0;

void disk_stats_set_external(const char *path, int max_age)
{
    snprintf(ext_path, sizeof(ext_path), "%s", path ? path : "");
    ext_max_age = max_age > 0 ? max_age : 0;
}

/* Parse the file/push format. Fills up to max entries and sets *agg from an
 * aggregate "max=" line (-1 when absent). Returns the entry count, or -1 when
 * not one usable line was found - so a truncated or wrong-format file is never
 * mistaken for "every drive spun down". */
static int ext_parse(FILE *fp, disk_ext_t *out, int max, float *agg)
{
    char line[256];
    int n = 0, valid = 0;
    *agg = -1.0f;
    while (fgets(line, sizeof(line), fp)) {
        char key[32], val[64];
        if (sscanf(line, " %31[^=#\r\n \t] = %63[^\r\n]", key, val) != 2) continue;
        char *hash = strchr(val, '#');                 /* trailing comment */
        if (hash) *hash = '\0';
        float size = 0;
        char *comma = strchr(val, ',');
        if (comma) { *comma = '\0'; size = (float)atof(comma + 1); }
        for (char *e = val + strlen(val); e > val && isspace((unsigned char)e[-1]); )
            *--e = '\0';

        float temp;
        if (isdigit((unsigned char)val[0])) temp = (float)atof(val);
        else if (val[0] == '*' || val[0] == '-' || val[0] == '\0') temp = -1.0f;
        else continue;                                 /* not a temperature */

        if (strcmp(key, "max") == 0) {                 /* aggregate, not a drive */
            if (temp >= 0) { *agg = temp; valid++; }
            continue;
        }
        valid++;
        if (n < max) {
            snprintf(out[n].name, sizeof(out[n].name), "%.15s", key);  /* names are display-only; the fan max scans every entry */
            out[n].temp_c = temp;
            out[n].size_tb = size > 0 ? size : 0;
            n++;
        }
    }
    return valid ? n : -1;
}

/* DISK_EXT_OFF when nothing is configured, DISK_EXT_STALE when the file is
 * missing, too old or unusable, else the entry count. */
static int ext_load(disk_ext_t *out, int max, float *agg)
{
    *agg = -1.0f;
    if (!ext_path[0]) return DISK_EXT_OFF;

    struct stat st;
    if (stat(ext_path, &st) != 0) return DISK_EXT_STALE;
    if (ext_max_age > 0) {
        time_t now = time(NULL);
        if (now > st.st_mtime && now - st.st_mtime > ext_max_age) return DISK_EXT_STALE;
    }
    FILE *fp = fopen(ext_path, "r");
    if (!fp) return DISK_EXT_STALE;
    int n = ext_parse(fp, out, max, agg);
    fclose(fp);
    return n < 0 ? DISK_EXT_STALE : n;
}

int disk_stats_external_max(int *max_c)
{
    disk_ext_t ext[INI_DISK_MAX];
    float agg;
    int n = ext_load(ext, INI_DISK_MAX, &agg);
    *max_c = -1;
    if (n < 0) return n;

    int best = agg >= 0 ? (int)agg : -1;
    for (int i = 0; i < n; i++)
        if (ext[i].temp_c >= 0 && (int)ext[i].temp_c > best) best = (int)ext[i].temp_c;
    *max_c = best;
    return n + (agg >= 0 ? 1 : 0);
}

static int ext_find(const disk_ext_t *ext, int n, const char *dev)
{
    for (int i = 0; i < n; i++)
        if (strcmp(ext[i].name, dev) == 0) return i;
    return -1;
}

int disk_stats_write_external(const char *text, int *drives, char *err, size_t errsz)
{
    if (!ext_path[0]) {
        snprintf(err, errsz, "no external temperature file configured");
        return -2;
    }
    size_t len = text ? strlen(text) : 0;
    if (len == 0 || len > EXT_MAX_BYTES) {
        snprintf(err, errsz, "body is empty or larger than %d bytes", EXT_MAX_BYTES);
        return -1;
    }

    disk_ext_t ext[INI_DISK_MAX];
    float agg;
    FILE *mem = fmemopen((void *)text, len, "r");
    if (!mem) { snprintf(err, errsz, "out of memory"); return -2; }
    int n = ext_parse(mem, ext, INI_DISK_MAX, &agg);
    fclose(mem);
    if (n < 0) {
        snprintf(err, errsz, "no usable name=temp line in the body");  /* send_error does not escape JSON */
        return -1;
    }

    /* Write back what was parsed, not the raw body: the file then holds exactly
     * the readings that passed validation, with no unbounded junk. */
    char tmp[288];
    snprintf(tmp, sizeof(tmp), "%s.tmp", ext_path);
    FILE *w = fopen(tmp, "w");
    if (!w) {                                   /* first push: the dir may not exist yet */
        char dup[256];
        snprintf(dup, sizeof(dup), "%s", ext_path);
        char *dir = dirname(dup);
        if (dir && dir[0] == '/') mkdir(dir, 0755);
        w = fopen(tmp, "w");
    }
    if (!w) { snprintf(err, errsz, "cannot write %s", ext_path); return -2; }

    fprintf(w, "# pushed to the ug-paneld/ug-fand API\n");
    for (int i = 0; i < n; i++) {
        fprintf(w, "%s=", ext[i].name);
        if (ext[i].temp_c >= 0) fprintf(w, "%.0f", ext[i].temp_c);
        else                    fputc('*', w);
        if (ext[i].size_tb > 0) fprintf(w, ",%.2f", ext[i].size_tb);
        fputc('\n', w);
    }
    if (agg >= 0) fprintf(w, "max=%.0f\n", agg);

    if (fclose(w) != 0 || rename(tmp, ext_path) != 0) {
        unlink(tmp);
        snprintf(err, errsz, "cannot write %s", ext_path);
        return -2;
    }
    *drives = n + (agg >= 0 ? 1 : 0);
    return 0;
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

    disk_ext_t ext[INI_DISK_MAX];
    char ext_used[INI_DISK_MAX] = { 0 };
    float ext_agg;
    int ext_n = ext_load(ext, INI_DISK_MAX, &ext_agg);
    if (ext_n < 0) ext_n = 0;                   /* off or stale: local sources only */

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
        int e = ext_find(ext, ext_n, ent->d_name);
        if (e >= 0) {
            d->temp_c = ext[e].temp_c;   /* the external source wins for a drive it names */
            ext_used[e] = 1;
        } else if (ini_n > 0 && ini_find(ini, ini_n, ent->d_name, &t))
            d->temp_c = t;   /* emhttpd's value; -1 = spun down, leave it asleep */
        else
            d->temp_c = read_temp_c(ent->d_name);
        d->online = 1;
    }
    closedir(dir);

    /* Drives the host has no block device for at all - the passthrough case the
     * external source exists for. Listed from the reported name alone. */
    for (int i = 0; i < ext_n && out->count < DISK_MAX; i++) {
        if (ext_used[i]) continue;
        disk_info_t *d = &out->disks[out->count++];
        snprintf(d->dev, sizeof(d->dev), "%.15s", ext[i].name);
        d->is_nvme = strncmp(ext[i].name, "nvme", 4) == 0;
        d->size_tb = ext[i].size_tb;
        d->temp_c = ext[i].temp_c;
        d->online = 1;
    }

    qsort(out->disks, out->count, sizeof(out->disks[0]), cmp_name);

    int sata_idx = 0, nvme_idx = 0;
    for (int i = 0; i < out->count; i++)
        out->disks[i].idx = out->disks[i].is_nvme ? ++nvme_idx : ++sata_idx;
    return 0;
}
