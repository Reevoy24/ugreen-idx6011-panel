/* Prints the bay of every disk named on the command line, or of every sd*
 * disk when none is given — the C side of the bay map (disk_stats.c), for
 * tools/test-bays.sh and for checking a real unit by hand:
 *     ./bays-probe              # layout + bay per disk
 *     ./bays-probe --collect    # what the web dashboard gets: bay, model, size
 * UG_SYSFS_ROOT / UG_BAY_LAYOUT point it at a fake tree (see disk_stats.c). */
#include "disk_stats.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

int main(int argc, char **argv)
{
    const char *layout = disk_stats_bay_layout_name();
    printf("layout %s\n", layout ? layout : "none");

    if (argc > 1 && strcmp(argv[1], "--collect") == 0) {
        disk_stats_t s;
        if (disk_stats_collect(&s) != 0) return 1;
        for (int i = 0; i < s.count; i++)
            printf("%-8s bay %d  %5.1f TB  %s\n", s.disks[i].dev, s.disks[i].bay,
                   s.disks[i].size_tb, s.disks[i].model);
        printf("passthrough");
        for (int n = 1; n <= BAY_COUNT; n++)
            if (s.bays_passthrough & (1u << (n - 1))) printf(" %d", n);
        printf("\n");
        return 0;
    }
    if (argc > 1) {
        for (int i = 1; i < argc; i++)
            printf("%s %d\n", argv[i], disk_stats_bay_of(argv[i]));
        return 0;
    }
    const char *root = getenv("UG_SYSFS_ROOT");
    char path[256];
    snprintf(path, sizeof(path), "%s/sys/block", root ? root : "");
    DIR *d = opendir(path);
    if (!d) return 1;
    struct dirent *e;
    while ((e = readdir(d)) != NULL)
        if (strncmp(e->d_name, "sd", 2) == 0)
            printf("%s %d\n", e->d_name, disk_stats_bay_of(e->d_name));
    closedir(d);
    return 0;
}
