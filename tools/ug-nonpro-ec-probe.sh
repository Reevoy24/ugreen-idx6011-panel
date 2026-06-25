#!/usr/bin/env bash
# ug-nonpro-ec-probe.sh — READ-ONLY fan-EC discovery for a non-Pro UGREEN NAS.
#
# Answers one question: where does THIS board keep its fan control?
#   (A) the ACPI EC at 0x62/0x66 (like the iDX6011 Pro, maybe different offsets)
#   (B) a UGREEN "201x" Super-I/O (chip 0x2011) at 0x2E/0x2F
#   (C) a standard ITE IT86xx/IT8613 Super-I/O at 0x2E/0x2F
#
# It ONLY READS registers (EC read-command 0x80, and Super-I/O index reads).
# It never issues an EC write (0x81) and never writes a fan-duty register, so
# the fans do not change. Safe to run.
#
# Needs: root + /dev/port (CONFIG_DEVPORT — present on stock TrueNAS SCALE).
# Usage: sudo bash ug-nonpro-ec-probe.sh
set -u
[ "$(id -u)" -eq 0 ] || { echo "run as root"; exit 1; }
[ -e /dev/port ] || { echo "/dev/port missing — cannot probe ports from userspace"; exit 1; }

pw()  { printf "$(printf '\\x%02x' "$2")" | dd of=/dev/port bs=1 count=1 seek="$1" conv=notrunc 2>/dev/null; }
pr()  { dd if=/dev/port bs=1 count=1 skip="$1" 2>/dev/null | od -An -tu1 | tr -d ' '; }
hx()  { printf '0x%02X' "$1"; }

echo "=================================================================="
echo " UGREEN fan-EC probe (read-only)"
echo " DMI: $(cat /sys/class/dmi/id/product_name 2>/dev/null) | $(cat /sys/class/dmi/id/product_version 2>/dev/null)"
echo "=================================================================="

# ---------- (A) ACPI EC RAM dump @ 0x62/0x66 (the Pro's interface) ----------
EC_SC=$((0x66)); EC_DATA=$((0x62))
ec_wait() { local m=$1 want=$2 i s; for i in $(seq 1 200); do s=$(pr $EC_SC); [ $(((s & m)!=0)) -eq "$want" ] && return 0; done; return 1; }
ec_rd()   { ec_wait 2 0 || { echo "-1"; return; }; pw $EC_SC 0x80; ec_wait 2 0 || { echo "-1"; return; }; pw $EC_DATA "$1"; ec_wait 1 1 || { echo "-1"; return; }; pr $EC_DATA; }

echo "[A] ACPI EC (0x62/0x66) RAM dump 0x00-0xFF:"
probe=$(ec_rd 0x00)
if [ "$probe" = "-1" ]; then
  echo "    no responsive ACPI EC here (handshake timed out) -> fans are NOT on a 0x62/0x66 EC"
else
  for base in $(seq 0 16 240); do
    row=""
    for off in $(seq 0 15); do v=$(ec_rd $((base+off))); row="$row $(printf '%02X' "$v" 2>/dev/null || echo '--')"; done
    printf "    %02X:%s\n" "$base" "$row"
  done
  echo "    (Pro layout for reference: tach RPM at 0x34-0x3B, fan duty at 0xB0-0xB7.)"
fi

# ---------- (B) UGREEN 201x knock (chip 0x2011) @ 0x2E/0x2F ----------
SIO_IDX=$((0x2E)); SIO_DATA=$((0x2F)); cfg() { pw $SIO_IDX "$1"; pr $SIO_DATA; }
pw $SIO_IDX 0xA5; pw $SIO_IDX 0x69; pw $SIO_IDX 0x87
id_201=$(( ($(cfg 0x20)<<8) | $(cfg 0x21) )); rev_201=$(( ($(cfg 0x22)<<8) | $(cfg 0x23) ))
printf "[B] 201x knock (A5 69 87):  chip=0x%04X rev=0x%04X\n" "$id_201" "$rev_201"
if [ "$id_201" -eq $((0x2011)) ]; then
  pw $SIO_IDX 0x07; pw $SIO_DATA 0x0A
  B=$(( ($(cfg 0x60)<<8) | $(cfg 0x61) ))
  printf "    UGREEN 201x! ec_addr_port=%s data=%s\n" "$(hx "$B")" "$(hx $((B+8)))"
  if [ "$B" -gt 0 ] && [ "$B" -ne $((0xFFFF)) ]; then
    ecr(){ pw $((B+1)) 0x00; pw $((B+7)) 0x20; pw $((B+6)) 0x00; pw $((B+5)) "$1"; pw $((B+4)) "$2"; pr $((B+8)); }
    cf=$(( ($(ecr 0x53 0xF8)<<8)|$(ecr 0x53 0xF9) )); sf=$(( ($(ecr 0x53 0xFA)<<8)|$(ecr 0x53 0xFB) ))
    printf "    cpu_temp=%sC board_temp=%sC  cpufan=%d sysfan1=%d RPM\n" "$(ecr 0x53 0xE2)" "$(ecr 0x53 0xE4)" "$cf" "$sf"
    printf "    enable 0x53F0=%s 0x53F1=%s  duty CPU(0x53F4)=%d%% SYS(0x53F5)=%d%%\n" \
      "$(hx "$(ecr 0x53 0xF0)")" "$(hx "$(ecr 0x53 0xF1)")" "$(ecr 0x53 0xF4)" "$(ecr 0x53 0xF5)"
  fi
fi
pw $SIO_IDX 0x87; pw $SIO_IDX 0x69; pw $SIO_IDX 0xA5

# ---------- (C) standard ITE knock (IT86xx / IT8613) @ 0x2E/0x2F ----------
pw $SIO_IDX 0x87; pw $SIO_IDX 0x01; pw $SIO_IDX 0x55; pw $SIO_IDX 0x55
id_std=$(( ($(cfg 0x20)<<8) | $(cfg 0x21) )); rev_std=$(cfg 0x22)
printf "[C] std ITE knock (87 01 55 55):  chip=0x%04X rev=0x%02X\n" "$id_std" "$rev_std"
if [ $(( id_std & 0xFF00 )) -eq $((0x8600)) ]; then
  pw $SIO_IDX 0x07; pw $SIO_DATA 0x04; act=$(cfg 0x30)
  EI=$((0xA35)); ED=$((0xA36)); ecr2(){ pw $EI "$1"; pr $ED; }
  tl=$(ecr2 0x0F); th=$(ecr2 0x1A); tach=$(( (th<<8)|tl )); rpm=0
  [ "$tach" -gt 0 ] && [ "$tach" -ne 65535 ] && rpm=$(( 1350000/(2*tach) ))
  printf "    ITE IT86xx! LDN4 active(0x30)=%s  tach=%d RPM~%d  ctl(0x17)=%s duty(0x73)=%d\n" \
    "$(hx "$act")" "$tach" "$rpm" "$(hx "$(ecr2 0x17)")" "$(ecr2 0x73)"
fi
pw $SIO_IDX 0x02; pw $SIO_DATA 0x02

echo "=================================================================="
echo "Done. No EC write and no fan-duty register were issued; fans unchanged."
echo "Please paste this ENTIRE output back into the issue."
