#!/usr/bin/env bash
# ug-thermal-bench.sh — reproducible thermal benchmark for the UGREEN iDX6011.
#
# Purpose: measure the cooling before/after a hardware mod (e.g. Noctua fans +
# fresh thermal paste) so you can put hard numbers on the difference. It drives
# an IDENTICAL all-core CPU load both times and logs, per second-ish:
#   * CPU package temperature   (coretemp, same sensor ug-fand/the panel use)
#   * disk/NVMe temperature      (system side)
#   * all fan RPMs               (from /run/ug-fand/status, else read the EC)
#   * CPU clock + throttle count (did it thermal-throttle?)
#   * CPU package power (RAPL)   (sanity: same watts in => a fair comparison)
#
# The headline number is DELTA-T = load_temp - idle_temp. Reporting the *rise
# over idle* cancels out the room temperature, which will differ between the two
# test days — an absolute "70C" means nothing without knowing the ambient.
#
# Three phases: idle baseline -> full load -> cooldown. At the end it prints a
# summary and writes <label>-<timestamp>.csv + .summary next to each other.
# Run it once BEFORE the mod (--label before) and once AFTER (--label after),
# then:  sudo bash ug-thermal-bench.sh compare before-*.summary after-*.summary
#
# By DEFAULT it does NOT touch fan control at all (read-only, zero risk): the
# fans run their normal ug-fand curve, so you see the real-world result — cooler
# AND quieter at once. Run both tests in the SAME ug-fand mode for a fair compare.
# Optional --fan-max pins every fan to 100% for the run (isolates pure cooling
# capacity / the paste); it does so by hot-swapping the ug-fand config and
# restores it on exit — ug-fand keeps owning the EC and its own failsafes.
#
# Needs: root. Runs on Proxmox / TrueNAS / Debian. NOT for UGOS.
# Usage:  sudo bash ug-thermal-bench.sh [--label before] [--load 600] [options]
#         sudo bash ug-thermal-bench.sh compare A.summary B.summary
set -u

# ------------------------------- defaults ----------------------------------
LABEL="run"
IDLE_SECS=60          # baseline before load
LOAD_SECS=600         # full-load soak (10 min -> reaches steady state)
COOL_SECS=120         # cooldown after load (0 to skip)
INTERVAL=2            # sample period, seconds
FAN_MODE="observe"    # observe = don't touch fans; max = pin all fans to 100%
LOAD_METHOD="auto"    # auto -> stress-ng if present, else sha256sum /dev/zero
OUTDIR="."
AMBIENT=""            # optional room-temp note, copied into the summary
CONFIG_PATH="/etc/ug-fand/config"
STATUS_PATH="/run/ug-fand/status"
EC_LOCK="/run/ug-ec.lock"

# ------------------------------ compare mode -------------------------------
# `compare A.summary B.summary` — print a before/after delta table and exit.
if [ "${1:-}" = "compare" ]; then
  A="${2:-}"; B="${3:-}"
  [ -r "$A" ] && [ -r "$B" ] || { echo "usage: $0 compare A.summary B.summary"; exit 1; }
  awk -F= '
    FNR==1 { file++ }
    { if (file==1) a[$1]=$2; else b[$1]=$2 }
    function num(x){ return (x=="" ? "n/a" : x) }
    function delta(k,   d){ if (a[k]=="" || b[k]=="") return ""; d=b[k]-a[k];
                            return sprintf("%+.1f", d) }
    END {
      la=(a["label"]==""?"A":a["label"]); lb=(b["label"]==""?"B":b["label"])
      printf "\n  %-22s %12s %12s %10s\n", "metric", la, lb, "delta"
      printf "  %s\n", "------------------------------------------------------------"
      printf "  %-22s %12s %12s %10s\n", "idle temp (C)",      num(a["idle_temp_c"]),        num(b["idle_temp_c"]),        delta("idle_temp_c")
      printf "  %-22s %12s %12s %10s\n", "load temp (C)",      num(a["load_steady_temp_c"]), num(b["load_steady_temp_c"]), delta("load_steady_temp_c")
      printf "  %-22s %12s %12s %10s\n", "peak temp (C)",      num(a["peak_temp_c"]),        num(b["peak_temp_c"]),        delta("peak_temp_c")
      printf "  %-22s %12s %12s %10s   <- key number\n", "delta-T over idle (K)", num(a["delta_t_k"]), num(b["delta_t_k"]), delta("delta_t_k")
      printf "  %-22s %12s %12s %10s\n", "disk temp @load (C)",num(a["load_disk_temp_c"]),   num(b["load_disk_temp_c"]),   delta("load_disk_temp_c")
      printf "  %-22s %12s %12s %10s\n", "throttle events",    num(a["throttle_events"]),    num(b["throttle_events"]),    delta("throttle_events")
      printf "  %-22s %12s %12s %10s\n", "avg clock @load MHz",num(a["steady_clk_mhz"]),     num(b["steady_clk_mhz"]),     delta("steady_clk_mhz")
      printf "  %-22s %12s %12s %10s\n", "pkg power @load W",  num(a["pkg_watts"]),          num(b["pkg_watts"]),          delta("pkg_watts")
      printf "  %-22s %12s %12s %10s\n", "cpufan1 @load RPM",  num(a["cpufan1_rpm"]),        num(b["cpufan1_rpm"]),        delta("cpufan1_rpm")
      printf "  %-22s %12s %12s %10s\n", "cpufan2 @load RPM",  num(a["cpufan2_rpm"]),        num(b["cpufan2_rpm"]),        delta("cpufan2_rpm")
      printf "  %-22s %12s %12s %10s\n", "sysfan1 @load RPM",  num(a["sysfan1_rpm"]),        num(b["sysfan1_rpm"]),        delta("sysfan1_rpm")
      printf "  %-22s %12s %12s %10s\n", "sysfan2 @load RPM",  num(a["sysfan2_rpm"]),        num(b["sysfan2_rpm"]),        delta("sysfan2_rpm")
      printf "\n  load: %s=\"%s\" %s=\"%s\"\n", la, a["load_method"], lb, b["load_method"]
      if (a["load_method"] != b["load_method"])
        printf "  !! different load methods — the two runs are NOT directly comparable.\n"
      if (a["fan_mode"] != b["fan_mode"])
        printf "  !! different fan modes (%s vs %s) — compare with care.\n", a["fan_mode"], b["fan_mode"]
      printf "  ambient: %s=\"%s\" %s=\"%s\"\n\n", la, num(a["ambient"]), lb, num(b["ambient"])
    }' "$A" "$B"
  exit 0
fi

# ------------------------------ arg parsing --------------------------------
while [ $# -gt 0 ]; do
  case "$1" in
    --label)  LABEL="$2"; shift 2;;
    --idle)   IDLE_SECS="$2"; shift 2;;
    --load)   LOAD_SECS="$2"; shift 2;;
    --cool)   COOL_SECS="$2"; shift 2;;
    --interval) INTERVAL="$2"; shift 2;;
    --fan-max) FAN_MODE="max"; shift;;
    --method) LOAD_METHOD="$2"; shift 2;;
    --out)    OUTDIR="$2"; shift 2;;
    --ambient) AMBIENT="$2"; shift 2;;
    -h|--help)
      sed -n '2,40p' "$0"; exit 0;;
    *) echo "unknown option: $1 (try --help)"; exit 1;;
  esac
done

[ "$(id -u)" -eq 0 ] || { echo "run as root (needs hwmon/RAPL/EC access)"; exit 1; }
mkdir -p "$OUTDIR" || { echo "cannot create outdir $OUTDIR"; exit 1; }

TS=$(date +%Y%m%d-%H%M%S)
CSV="$OUTDIR/${LABEL}-${TS}.csv"
SUM="$OUTDIR/${LABEL}-${TS}.summary"
MODEL=$(cat /sys/class/dmi/id/product_name 2>/dev/null || echo unknown)
NONPRO=0; case "$MODEL" in *iDX6011*) case "$MODEL" in *Pro*) ;; *) NONPRO=1;; esac;; esac
NPROC=$(nproc 2>/dev/null || echo 1)

# ------------------------- sensor discovery --------------------------------
# CPU temp: hottest temp*_input from the hwmon whose name matches (coretemp on
# Intel). Same selection logic as ug-fand, so numbers line up with the panel.
cpu_temp_mC() {
  local want best=-1 h n p v
  for want in coretemp k10temp acpitz; do
    for h in /sys/class/hwmon/hwmon*; do
      [ -r "$h/name" ] || continue
      read -r n < "$h/name" 2>/dev/null || continue
      case "$n" in *"$want"*) ;; *) continue;; esac
      for p in "$h"/temp*_input; do
        [ -r "$p" ] || continue
        read -r v < "$p" 2>/dev/null || continue
        [ "$v" -gt "$best" ] 2>/dev/null && best="$v"
      done
    done
    [ "$best" -ge 0 ] && { echo "$best"; return; }
  done
  echo -1
}
disk_temp_mC() {
  local want best=-1 h n p v
  for want in nvme drivetemp; do
    for h in /sys/class/hwmon/hwmon*; do
      [ -r "$h/name" ] || continue
      read -r n < "$h/name" 2>/dev/null || continue
      case "$n" in *"$want"*) ;; *) continue;; esac
      for p in "$h"/temp*_input; do
        [ -r "$p" ] || continue
        read -r v < "$p" 2>/dev/null || continue
        [ "$v" -gt "$best" ] 2>/dev/null && best="$v"
      done
    done
  done
  echo "$best"
}

# CPU clock (MHz): avg + min over all cores. Prefer cpufreq; fall back to
# /proc/cpuinfo "cpu MHz" on kernels that don't export scaling_cur_freq.
clk_avg_min() {
  local sum=0 cnt=0 min=99999999 v
  if compgen -G "/sys/devices/system/cpu/cpu[0-9]*/cpufreq/scaling_cur_freq" >/dev/null; then
    for p in /sys/devices/system/cpu/cpu[0-9]*/cpufreq/scaling_cur_freq; do
      read -r v < "$p" 2>/dev/null || continue
      v=$((v/1000)); sum=$((sum+v)); cnt=$((cnt+1))
      [ "$v" -lt "$min" ] && min="$v"
    done
  else
    while read -r _ _ v; do
      v=${v%.*}; sum=$((sum+v)); cnt=$((cnt+1))
      [ "$v" -lt "$min" ] && min="$v"
    done < <(awk -F: '/cpu MHz/{print $1":"$2}' /proc/cpuinfo | sed 's/:/ /')
  fi
  [ "$cnt" -gt 0 ] && echo "$((sum/cnt)) $min" || echo ""
}

# Cumulative thermal-throttle count across all cores (a rising number during
# load == the CPU hit its thermal limit and clocked down).
throttle_total() {
  local sum=0 v
  for p in /sys/devices/system/cpu/cpu[0-9]*/thermal_throttle/core_throttle_count; do
    [ -r "$p" ] || continue
    read -r v < "$p" 2>/dev/null || continue
    sum=$((sum+v))
  done
  echo "$sum"
}

# RAPL package energy (uJ) + its wrap point, for a per-sample watt figure.
RAPL_DIR=""
for d in /sys/class/powercap/intel-rapl:0 /sys/class/powercap/intel-rapl/intel-rapl:0; do
  [ -r "$d/energy_uj" ] && { RAPL_DIR="$d"; break; }
done
RAPL_MAX=0
[ -n "$RAPL_DIR" ] && [ -r "$RAPL_DIR/max_energy_range_uj" ] && read -r RAPL_MAX < "$RAPL_DIR/max_energy_range_uj"

# --------------------------- fan RPM sources -------------------------------
# Preferred: the status file ug-fand already publishes (no EC contention).
FAN_SRC="none"
[ -r "$STATUS_PATH" ] && FAN_SRC="status"

# Fallback: read the tach registers straight off the EC (read-only), coordinated
# with ug-paneld's backlight via the shared flock. Only used if ug-fand is not
# running. Mirrors the register map in ug_fand.c.
EC_SC=$((0x66)); EC_DATA=$((0x62))
have_devport=0; [ -e /dev/port ] && have_devport=1
[ "$FAN_SRC" = "none" ] && [ "$have_devport" = 1 ] && FAN_SRC="ec"
pw() { printf "$(printf '\\x%02x' "$2")" | dd of=/dev/port bs=1 count=1 seek="$1" conv=notrunc 2>/dev/null; }
pr() { dd if=/dev/port bs=1 count=1 skip="$1" 2>/dev/null | od -An -tu1 | tr -d ' '; }
ecw() { local m=$1 w=$2 i s; for i in $(seq 1 300); do s=$(pr $EC_SC); [ $(((s&m)!=0)) -eq "$w" ] && return 0; done; return 1; }
ec_rd() { ecw 2 0||{ echo 0;return;}; pw $EC_SC 0x80; ecw 2 0||{ echo 0;return;}; pw $EC_DATA "$1"; ecw 1 1||{ echo 0;return;}; pr $EC_DATA; }
ec_tach() { echo $(( $(ec_rd "$1")*256 + $(ec_rd $(( $1 + 1 )) ) )); }

# Returns "cpufan1 cpufan2 sysfan1 sysfan2" (0 where a fan doesn't exist).
read_rpms() {
  if [ "$FAN_SRC" = "status" ]; then
    awk -F= '
      /^cpufan1=/{c1=$2} /^cpufan2=/{c2=$2}
      /^sysfan1=/{s1=$2} /^sysfan2=/{s2=$2}
      END{printf "%d %d %d %d", c1,c2,s1,s2}' "$STATUS_PATH" 2>/dev/null
  elif [ "$FAN_SRC" = "ec" ]; then
    ( flock 9
      if [ "$NONPRO" = 1 ]; then
        printf "0 0 %d %d" "$(ec_tach 0x96)" "$(ec_tach 0x98)"
      else
        printf "%d %d %d %d" "$(ec_tach 0x34)" "$(ec_tach 0x36)" "$(ec_tach 0x38)" "$(ec_tach 0x3A)"
      fi
    ) 9>"$EC_LOCK"
  else
    printf "0 0 0 0"
  fi
}
# Fan duty % the daemon currently applies (status file only).
read_pcts() {
  if [ "$FAN_SRC" = "status" ]; then
    awk -F= '/^cpu_pct=/{c=$2} /^sys_pct=/{s=$2} END{printf "%d %d", c, s}' "$STATUS_PATH" 2>/dev/null
  else
    printf "0 0"
  fi
}

# ----------------------------- load engine ---------------------------------
# Identical work both runs => identical heat => a fair comparison. We record the
# exact method into the summary and refuse to compare across different methods.
LOAD_PIDS=()
if [ "$LOAD_METHOD" = "auto" ]; then
  if command -v stress-ng >/dev/null 2>&1; then LOAD_METHOD="stress-ng"; else LOAD_METHOD="sha256sum"; fi
fi
start_load() {
  case "$LOAD_METHOD" in
    stress-ng)
      stress-ng --cpu "$NPROC" --cpu-method matrixprod -t "$((LOAD_SECS+5))s" >/dev/null 2>&1 &
      LOAD_PIDS+=($!) ;;
    sha256sum)
      local i
      for i in $(seq 1 "$NPROC"); do sha256sum /dev/zero >/dev/null 2>&1 & LOAD_PIDS+=($!); done ;;
    *) echo "unknown --method $LOAD_METHOD (use stress-ng or sha256sum)"; exit 1;;
  esac
}
stop_load() {
  local p
  for p in "${LOAD_PIDS[@]:-}"; do [ -n "$p" ] && kill "$p" 2>/dev/null; done
  command -v stress-ng >/dev/null 2>&1 && pkill -f "stress-ng --cpu" 2>/dev/null
  LOAD_PIDS=()
}

# --------------------------- optional fan pin ------------------------------
# --fan-max: hot-swap the ug-fand config so every fan sits at 100% for the run,
# then restore it verbatim on exit. ug-fand keeps controlling the EC (and keeps
# its critical-temp failsafe), we just hand it a flat 100% curve.
CFG_BACKUP=""
pin_fans_max() {
  [ "$FAN_MODE" = "max" ] || return 0
  if [ ! -w "$CONFIG_PATH" ] && [ ! -w "$(dirname "$CONFIG_PATH")" ]; then
    echo "!! --fan-max: $CONFIG_PATH not writable / ug-fand not installed — staying in observe mode"
    FAN_MODE="observe"; return 0
  fi
  CFG_BACKUP="$(mktemp)"
  [ -r "$CONFIG_PATH" ] && cp "$CONFIG_PATH" "$CFG_BACKUP"
  # strip the keys we override, then append flat 100% curves in turbo mode
  { [ -r "$CONFIG_PATH" ] && grep -vE '^(mode|cpu_turbo|sys_turbo)=' "$CONFIG_PATH"
    echo "mode=turbo"
    echo "cpu_turbo=0:100,120:100"
    echo "sys_turbo=0:100,120:100"
  } > "$CONFIG_PATH.bench.tmp" && mv "$CONFIG_PATH.bench.tmp" "$CONFIG_PATH"
  echo "  fans pinned to 100% via ug-fand config (restored on exit); waiting 8s to spin up..."
  sleep 8
}
restore_fans() {
  [ -n "$CFG_BACKUP" ] || return 0
  if [ -s "$CFG_BACKUP" ]; then cp "$CFG_BACKUP" "$CONFIG_PATH"; else rm -f "$CONFIG_PATH"; fi
  rm -f "$CFG_BACKUP"; CFG_BACKUP=""
  echo "  ug-fand config restored."
}

cleanup() { stop_load; restore_fans; }
trap 'echo; echo "-> aborting, cleaning up"; cleanup; exit 130' INT TERM
trap cleanup EXIT

# ------------------------------ sampling -----------------------------------
PREV_E=""; PREV_T=""     # for RAPL watts between samples
START_EPOCH=$(date +%s)
sample() {                 # $1 = phase label
  local phase="$1" now el ct dt rpms pcts clk clkavg clkmin thr watts e t
  now=$(date +%s.%N); el=$(awk -v a="$START_EPOCH" -v b="$now" 'BEGIN{printf "%.1f", b-a}')
  ct=$(cpu_temp_mC); dt=$(disk_temp_mC)
  rpms=$(read_rpms); pcts=$(read_pcts)
  clk=$(clk_avg_min); clkavg=${clk% *}; clkmin=${clk#* }
  [ -z "$clk" ] && { clkavg=""; clkmin=""; }
  thr=$(throttle_total)
  # RAPL -> average watts since the previous sample
  watts=""
  if [ -n "$RAPL_DIR" ]; then
    read -r e < "$RAPL_DIR/energy_uj" 2>/dev/null || e=""
    t="$now"
    if [ -n "$e" ] && [ -n "$PREV_E" ]; then
      watts=$(awk -v e="$e" -v pe="$PREV_E" -v t="$t" -v pt="$PREV_T" -v m="$RAPL_MAX" '
        BEGIN{ de=e-pe; if(de<0 && m>0) de+=m; dt=t-pt; if(dt<=0){print ""; exit}
               printf "%.1f", (de/1e6)/dt }')
    fi
    PREV_E="$e"; PREV_T="$t"
  fi
  # ct/dt are milli-C; convert to C with one decimal
  local ctc dtc
  ctc=$(awk -v v="$ct" 'BEGIN{ if(v<0) print ""; else printf "%.1f", v/1000 }')
  dtc=$(awk -v v="$dt" 'BEGIN{ if(v<0) print ""; else printf "%.1f", v/1000 }')
  echo "$now,$el,$phase,$ctc,$dtc,${rpms// /,},${pcts// /,},$clkavg,$clkmin,$thr,$watts" >> "$CSV"
  # live line
  printf "\r  [%-4s] t=%5ss  cpu=%-5sC disk=%-5sC  fans(%s) %%(%s)  clk=%sMHz thr=%s W=%s   " \
    "$phase" "$el" "${ctc:-?}" "${dtc:-?}" "${rpms// /,}" "${pcts// /,}" "${clkavg:-?}" "$thr" "${watts:-?}"
}

run_phase() {              # $1 = label, $2 = seconds
  local phase="$1" secs="$2" i
  local n=$(( secs / INTERVAL )); [ "$n" -lt 1 ] && n=1
  for i in $(seq 1 "$n"); do sample "$phase"; sleep "$INTERVAL"; done
  echo
}

# ------------------------------- run ---------------------------------------
echo "=================================================================="
echo " ug-thermal-bench — $LABEL"
echo " model=$MODEL  cores=$NPROC  load=$LOAD_METHOD  fan=$FAN_MODE  fanRPM=$FAN_SRC"
echo " phases: idle ${IDLE_SECS}s -> load ${LOAD_SECS}s -> cool ${COOL_SECS}s @ ${INTERVAL}s"
echo " output: $CSV"
[ "$FAN_SRC" = "none" ] && echo " NOTE: no ug-fand status and no /dev/port — fan RPM will read 0."
echo "=================================================================="
echo "epoch,elapsed,phase,cpu_temp,disk_temp,cpufan1,cpufan2,sysfan1,sysfan2,cpu_pct,sys_pct,clk_avg_mhz,clk_min_mhz,throttle_total,pkg_watts" > "$CSV"

pin_fans_max
echo "[1/3] idle baseline (${IDLE_SECS}s)"; run_phase idle "$IDLE_SECS"
echo "[2/3] full load (${LOAD_SECS}s) — starting $NPROC-way $LOAD_METHOD"; start_load; run_phase load "$LOAD_SECS"; stop_load
if [ "$COOL_SECS" -gt 0 ]; then echo "[3/3] cooldown (${COOL_SECS}s)"; run_phase cool "$COOL_SECS"; else echo "[3/3] cooldown skipped"; fi

# ----------------------------- summarise -----------------------------------
# Steady-state window = last 90s of the load phase (or the last third if the
# soak was shorter), so we average the plateau, not the ramp.
awk -F, -v label="$LABEL" -v model="$MODEL" -v method="$LOAD_METHOD" \
        -v fanmode="$FAN_MODE" -v loadsecs="$LOAD_SECS" -v ambient="$AMBIENT" -v ts="$TS" '
  NR==1 { next }
  {
    ph=$3; el=$2+0; n++
    P[n]=ph; EL[n]=el; T[n]=$4; DT[n]=$5
    C1[n]=$6; C2[n]=$7; S1[n]=$8; S2[n]=$9
    CK[n]=$12; MK[n]=$13; TH[n]=$14; W[n]=$15
    if (ph=="idle") { idle_last=el }
    if (ph=="load") { if (el>load_last) load_last=el; if (load_first==0) load_first=el }
  }
  function mean(arr, cond_ph, from,   i,s,c){ s=0;c=0;
    for(i=1;i<=n;i++) if(P[i]==cond_ph && EL[i]>=from && arr[i]!=""){ s+=arr[i]; c++ }
    return c? s/c : "" }
  function maxv(arr, cond_ph,   i,m,seen){ m=-1e9; seen=0;
    for(i=1;i<=n;i++) if(P[i]==cond_ph && arr[i]!=""){ if(arr[i]+0>m){m=arr[i]+0}; seen=1 }
    return seen? m : "" }
  function minv(arr, cond_ph,   i,m,seen){ m=1e18; seen=0;
    for(i=1;i<=n;i++) if(P[i]==cond_ph && arr[i]!=""){ if(arr[i]+0<m){m=arr[i]+0}; seen=1 }
    return seen? m : "" }
  END {
    win = load_last>0 ? load_last-90 : 0
    if (win < load_first) win = load_first + (load_last-load_first)*2/3
    idle_from = idle_last>30 ? idle_last-30 : 0

    idle_t   = mean(T,  "idle", idle_from)
    load_t   = mean(T,  "load", win)
    peak_t   = maxv(T,  "load")
    disk_t   = mean(DT, "load", win)
    c1 = mean(C1,"load",win); c2 = mean(C2,"load",win)
    s1 = mean(S1,"load",win); s2 = mean(S2,"load",win)
    clk = mean(CK,"load",win); mclk = minv(MK,"load")
    watts = mean(W,"load",win)
    # throttle events during load = last cumulative count - first
    thr_first=""; thr_last=""
    for(i=1;i<=n;i++) if(P[i]=="load" && TH[i]!=""){ if(thr_first=="")thr_first=TH[i]; thr_last=TH[i] }
    thr = (thr_first=="")? "" : (thr_last-thr_first)
    dtk = (idle_t=="" || load_t=="")? "" : load_t-idle_t

    printf "label=%s\n", label            > SUMOUT
    printf "timestamp=%s\n", ts            > SUMOUT
    printf "model=%s\n", model             > SUMOUT
    printf "load_method=%s\n", method      > SUMOUT
    printf "fan_mode=%s\n", fanmode        > SUMOUT
    printf "load_seconds=%s\n", loadsecs   > SUMOUT
    printf "ambient=%s\n", ambient         > SUMOUT
    if(idle_t!="") printf "idle_temp_c=%.1f\n", idle_t          > SUMOUT
    if(load_t!="") printf "load_steady_temp_c=%.1f\n", load_t   > SUMOUT
    if(peak_t!="") printf "peak_temp_c=%.1f\n", peak_t          > SUMOUT
    if(dtk!="")    printf "delta_t_k=%.1f\n", dtk               > SUMOUT
    if(disk_t!="") printf "load_disk_temp_c=%.1f\n", disk_t     > SUMOUT
    if(thr!="")    printf "throttle_events=%d\n", thr           > SUMOUT
    if(clk!="")    printf "steady_clk_mhz=%.0f\n", clk          > SUMOUT
    if(mclk!="")   printf "min_clk_mhz=%.0f\n", mclk            > SUMOUT
    if(watts!="")  printf "pkg_watts=%.1f\n", watts             > SUMOUT
    if(c1!="")     printf "cpufan1_rpm=%.0f\n", c1              > SUMOUT
    if(c2!="")     printf "cpufan2_rpm=%.0f\n", c2              > SUMOUT
    if(s1!="")     printf "sysfan1_rpm=%.0f\n", s1              > SUMOUT
    if(s2!="")     printf "sysfan2_rpm=%.0f\n", s2              > SUMOUT
  }' SUMOUT="$SUM" "$CSV"

echo
echo "=================== summary: $LABEL ==================="
cat "$SUM"
echo "======================================================"
echo "CSV:     $CSV"
echo "summary: $SUM"
echo
echo "Run the other side (e.g. --label after) with the SAME options, then:"
echo "  sudo bash $0 compare <before>.summary <after>.summary"
