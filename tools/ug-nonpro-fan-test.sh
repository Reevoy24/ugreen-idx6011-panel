#!/usr/bin/env bash
# ug-nonpro-fan-test.sh — controllability test for the non-Pro iDX6011.
#
# Unlike the read-only probe, this DOES write fan-duty registers (EC reg 0x9C-0x9F)
# to prove the fans are actually controllable, then HANDS CONTROL BACK to the EC's
# automatic mode (enable=0) so the machine is left safe. It also diffs the full EC
# RAM at full speed vs idle to reveal how many fans really respond.
#
# Safety:
#   * only briefly forces FULL speed (safe) then a moderate speed, ~6s each;
#   * always restores EC auto-mode on exit (even on Ctrl-C);
#   * the EC's own thermal protection still applies.
# Needs: root + /dev/port. Usage: sudo bash ug-nonpro-fan-test.sh
set -u
[ "$(id -u)" -eq 0 ] || { echo "run as root"; exit 1; }
[ -e /dev/port ] || { echo "/dev/port missing"; exit 1; }

pw() { printf "$(printf '\\x%02x' "$2")" | dd of=/dev/port bs=1 count=1 seek="$1" conv=notrunc 2>/dev/null; }
pr() { dd if=/dev/port bs=1 count=1 skip="$1" 2>/dev/null | od -An -tu1 | tr -d ' '; }

EC_SC=$((0x66)); EC_DATA=$((0x62))
ecw() { local m=$1 w=$2 i s; for i in $(seq 1 300); do s=$(pr $EC_SC); [ $(((s&m)!=0)) -eq "$w" ] && return 0; done; return 1; }
ec_rd() { ecw 2 0||{ echo -1;return;}; pw $EC_SC 0x80; ecw 2 0||{ echo -1;return;}; pw $EC_DATA "$1"; ecw 1 1||{ echo -1;return;}; pr $EC_DATA; }
ec_wr() { ecw 2 0||return 1; pw $EC_SC 0x81; ecw 2 0||return 1; pw $EC_DATA "$1"; ecw 2 0||return 1; pw $EC_DATA "$2"; }

# duty registers (enable=1 + value) for the 2 system fans; 0..198 (0xC6) scale
set_duty() { ec_wr 0x9C 1; ec_wr 0x9D "$1"; ec_wr 0x9E 1; ec_wr 0x9F "$1"; }
release()  { ec_wr 0x9C 0; ec_wr 0x9D 0; ec_wr 0x9E 0; ec_wr 0x9F 0; }  # restore idle/auto (all 0)
trap 'echo; echo "-> restoring EC auto-mode"; release' EXIT INT TERM

rpm() { echo "sysfan1=$(( $(ec_rd 0x96)*256 + $(ec_rd 0x97) ))  sysfan2=$(( $(ec_rd 0x98)*256 + $(ec_rd 0x99) ))"; }
snap() { local i; for i in $(seq 0 255); do printf '%s\n' "$(ec_rd $i)"; done; }

echo "=== non-Pro iDX6011 fan controllability test ==="
echo "DMI: $(cat /sys/class/dmi/id/product_name 2>/dev/null) | $(cat /sys/class/dmi/id/product_version 2>/dev/null)"
[ "$(ec_rd 0x00)" = "-1" ] && { echo "no ACPI EC at 0x62/0x66 — aborting"; exit 1; }

echo; echo "[idle/auto]  $(rpm)"
mapfile -t BASE < <(snap)

echo; echo "[set FULL 198] writing 0x9C-0x9F ..."; set_duty 198; sleep 6
echo "[full]       $(rpm)"
mapfile -t FULL < <(snap)

echo; echo "[set LOW 60] ..."; set_duty 60; sleep 6
echo "[low]        $(rpm)"

echo; echo "=== EC bytes that changed idle -> full-speed (every responding fan/state) ==="
for i in $(seq 0 255); do
  b=${BASE[$i]:--}; f=${FULL[$i]:--}
  if [ "$b" != "$f" ]; then printf '  0x%02X: %3s -> %3s\n' "$i" "$b" "$f"; fi
done

echo; echo "(restoring auto-mode now)"; release; sleep 4
echo "[back to auto] $(rpm)"
trap - EXIT
echo
echo "VERDICT: if sysfan RPM rose at FULL and dropped at LOW, the fans are controllable."
echo "The 'changed bytes' list shows how many fan/RPM registers actually responded."
echo "Please paste this entire output back into the issue."
