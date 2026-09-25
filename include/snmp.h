#ifndef SNMP_H
#define SNMP_H

#include <stddef.h>

/* Minimal SNMPv2c client: exactly enough to walk one table on a NAS's SNMP
 * agent — here TrueNAS's hddTempTable, so ug-fand can follow the temperatures
 * of disks that sit behind a passed-through controller in a VM.
 *
 * Why SNMP and not something that asks the disks: every temperature read is a
 * command to the drive, which can reset its spin-down timer or unpark its
 * heads (see the drivetemp kernel docs, and issue #6). TrueNAS already reads
 * each disk every 5 minutes for its own graphs, and its SNMP agent serves
 * exactly those cached values — so polling it adds no disk access at all.
 *
 * Deliberately small: v2c GETBULK only, IPv4/IPv6 literals only (no DNS — the
 * fand build is static, and NSS in a static glibc binary is a trap). Replies
 * come from the network and this runs as root, so the parser trusts nothing:
 * every length is checked against the buffer before it is used. */

#define SNMP_OID_MAX 32

typedef struct {
    unsigned oid[SNMP_OID_MAX];
    int oid_len;
    unsigned char type;     /* BER tag of the value */
    long long num;          /* INTEGER / Gauge32 / Counter32 / TimeTicks */
    char str[64];           /* OCTET STRING, NUL-terminated (truncated) */
} snmp_varbind_t;

/* Walk every object under root. Returns the number of varbinds stored in out,
 * or -1 with a reason in err (unreachable, timeout, error-status, ...). */
int snmp_walk(const char *ip, const char *community,
              const unsigned *root, int root_len,
              snmp_varbind_t *out, int max, char *err, size_t errsz);

/* TrueNAS: read TRUENAS-MIB::hddTempTable and render it in the external
 * drive-temperature file format ("vm:sda=41", "*" = no reading). The "vm:"
 * prefix keeps the guest's disk names from colliding with the host's own
 * sda/nvme0n1 in the drive list. Returns the number of drives (0 = the agent
 * served an empty table), or -1 with a reason in err. */
int snmp_truenas_disk_temps(const char *ip, const char *community,
                            char *text, size_t textsz, char *err, size_t errsz);

/* Exposed for the tests: parse one response datagram (GetResponse PDU with the
 * expected request id). Returns the varbind count, -1 when malformed or the
 * agent reported an error, -2 when it answers a different request id. */
int snmp_parse_response(const unsigned char *buf, size_t len, long reqid,
                        snmp_varbind_t *out, int max);

#endif
