/* ug-fand temperature-path checks — no EC, no root:
 *
 *   gcc -Iinclude -pthread test/fand_check.c src/fand_api.c src/system_stats.c \
 *       src/net_stats.c src/disk_stats.c src/snmp.c -o fand-check
 *
 * Pulls ug_fand.c in directly so its static helpers can be exercised; its
 * main() is renamed so this file provides the entry point.
 *
 * The case that matters: a temperature source that goes away WHILE the daemon
 * runs must end in the missing-sensor failsafe. It used to end in a frozen
 * average instead, and nobody noticed until a rebooting storage VM left the
 * fans at 37 % (#10). */
#define main ug_fand_main
#include "../src/ug_fand.c"
#undef main
#include <utime.h>

static int fails;

static void expect(const char *what, long long got, long long want)
{
    if (got == want) return;
    printf("  FAIL %-44s got %lld, want %lld\n", what, got, want);
    fails++;
}

int main(void)
{
    printf("fan temperature path:\n");

    /* smoothing: a lost reading must drop the average, not freeze it */
    double ema = -1;
    expect("first reading is taken as is", smooth_temp(&ema, 43), 43);
    for (int i = 0; i < 10; i++) smooth_temp(&ema, 43);
    expect("reading lost -> no value", smooth_temp(&ema, -1), -1);
    expect("still lost -> still no value", smooth_temp(&ema, -1), -1);
    expect("back -> restarts from the new reading", smooth_temp(&ema, 38), 38);

    /* the #10 scenario through the real sys_temp(): external source goes stale */
    char path[] = "/tmp/ug-fand-check.XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) { printf("  SKIP (mkstemp)\n"); return 1; }
    const char *line = "vm:sda=43\n";
    if (write(fd, line, strlen(line)) != (ssize_t)strlen(line)) fails++;
    close(fd);
    disk_stats_set_external(path, 120);

    int st = sys_temp(0);                       /* 0 = no cache, read now */
    expect("fresh external source is used", st >= 43, 1);   /* max with any local NVMe */

    struct utimbuf old = { .actime = time(NULL) - 600, .modtime = time(NULL) - 600 };
    utime(path, &old);
    st = sys_temp(0);
    expect("stale source -> no reading, even with NVMe", st, -1);

    ema = 43;                                   /* the fans were following 43 C */
    expect("stale source reaches the failsafe", smooth_temp(&ema, st), -1);

    remove(path);
    disk_stats_set_external("", 0);
    printf("  %s\n", fails ? "FAILURES ABOVE" : "ok");
    return fails ? 1 : 0;
}
