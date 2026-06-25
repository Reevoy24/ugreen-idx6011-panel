#!/usr/bin/env bash
# ug-nonpro-hidden-fan-test.sh — does the non-Pro have a hidden 3rd/4th fan?
#
# The Pro controls 4 fans via EC duty regs 0xB0-0xB7 and reads tach at 0x34-0x3B.
# On the non-Pro those looked dead. This test WRITES 0xB0-0xB7 to full speed (the
# Pro layout) and diffs the whole EC RAM idle-vs-after: if any fan besides the two
# known system fans (0x96/0x98) spins up, a new tachometer register will change.
#
# Safety: it does NOT touch the 2 known system fans (0x9C-0x9F) — they stay in EC
# auto-mode the whole time. Full speed is thermally safe. It restores 0xB0-0xB7 to
# their original values on exit (even on Ctrl-C). Needs root + /dev/port.
# Usage: sudo bash ug-nonpro-hidden-fan-test.sh
set -u
[ "$(id -u)" -eq 0 ] || { echo "run as root"; exit 1; }
[ -e /dev/port ] || { echo "/dev/port missing"; exit 1; }

pw() { printf "$(printf '\\x%02x' "$2")" | dd of=/dev/port bs=1 count=1 seek="$1" conv=notrunc 2>/dev/null; }
pr() { dd if=/dev/port bs=1 count=1 skip="$1" 2>/dev/null | od -An -tu1 | tr -d ' '; }
EC_SC=$((0x66)); EC_DATA=$((0x62))
ecw() { local m=$1 w=$2 i s; for i in $(seq 1 300); do s=$(pr $EC_SC); [ $(((s&m)!=0)) -eq "$w" ] && return 0; done; return 1; }
ec_rd() { ecw 2 0||{ echo -1;return;}; pw $EC_SC 0x80; ecw 2 0||{ echo -1;return;}; pw $EC_DATA "$1"; ecw 1 1||{ echo -1;return;}; pr $EC_DATA; }
ec_wr() { ecw 2 0||return 1; pw $EC_SC 0x81; ecw 2 0||return 1; pw $EC_DATA "$1"; ecw 2 0||return 1; pw $EC_DATA "$2"; }

rpm()    { echo "sysfan1(0x96)=$(( $(ec_rd 0x96)*256+$(ec_rd 0x97) ))  sysfan2(0x98)=$(( $(ec_rd 0x98)*256+$(ec_rd 0x99) ))"; }
protach(){ echo "Pro-tach 0x34=$(( $(ec_rd 0x34)*256+$(ec_rd 0x35) )) 0x36=$(( $(ec_rd 0x36)*256+$(ec_rd 0x37) )) 0x38=$(( $(ec_rd 0x38)*256+$(ec_rd 0x39) )) 0x3A=$(( $(ec_rd 0x3A)*256+$(ec_rd 0x3B) ))"; }
snap()   { local i; for i in $(seq 0 255); do ec_rd $i; done; }

echo "=== non-Pro iDX6011: hidden-fan test (writes 0xB0-0xB7 only) ==="
echo "DMI: $(cat /sys/class/dmi/id/product_name 2>/dev/null) | $(cat /sys/class/dmi/id/product_version 2>/dev/null)"
[ "$(ec_rd 0x00)" = "-1" ] && { echo "no ACPI EC at 0x62/0x66 — aborting"; exit 1; }

# remember original 0xB0-0xB7 so we can put them back
declare -a ORIG; for i in $(seq 0 7); do ORIG[$i]=$(ec_rd $((0xB0+i))); done
restore_b0() { local i; for i in $(seq 0 7); do ec_wr $((0xB0+i)) "${ORIG[$i]}"; done; }
trap 'echo; echo "-> restoring 0xB0-0xB7"; restore_b0' EXIT INT TERM

echo; echo "[idle] $(rpm)"; echo "[idle] $(protach)"; echo "[idle] 0xB0-0xB7 = ${ORIG[*]}"
mapfile -t BASE < <(snap)

echo; echo "[writing 0xB0-0xB7 = enable1 + duty198 on all 4 Pro slots] ..."
for i in 0 2 4 6; do ec_wr $((0xB0+i)) 1; ec_wr $((0xB0+i+1)) 198; done
sleep 6
echo "[after] $(rpm)"; echo "[after] $(protach)"
mapfile -t AFTER < <(snap)

echo; echo "=== EC bytes changed idle -> after ==="
echo "    (0xB0-0xB7 change because WE wrote them; ANY OTHER change = a fan responded)"
other=0
for i in $(seq 0 255); do
  b=${BASE[$i]:--}; a=${AFTER[$i]:--}
  [ "$b" = "$a" ] && continue
  if [ "$i" -ge 176 ] && [ "$i" -le 183 ]; then tag="  (written by test)"; else tag="  <-- OTHER"; other=1; fi
  printf '  0x%02X: %3s -> %3s%s\n' "$i" "$b" "$a" "$tag"
done

echo; echo "(restoring 0xB0-0xB7 and settling)"; restore_b0; trap - EXIT; sleep 3
echo "[restored] $(rpm)"
echo
if [ "$other" = 1 ]; then
  echo "VERDICT: something OTHER than 0xB0-0xB7 changed -> there may be a 3rd/4th fan; see the '<-- OTHER' lines."
else
  echo "VERDICT: only 0xB0-0xB7 changed (our writes) and no RPM moved -> 0xB0 is dead here; confirms 2 fans only."
fi
echo "Please paste this entire output back into the issue."
