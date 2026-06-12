#!/bin/bash
# Functional test for ugreen-leds-mon.sh: stubbed CLI, real /proc counters,
# run under dash (TrueNAS post-init sh). Verifies initial solid writes,
# blink on activity, and solid again when idle.
set -e
T=/tmp/ledmon-test
rm -rf "$T"
mkdir -p "$T"

# stub CLI: log every invocation
cat > "$T/ugreen_leds_cli" <<'EOF'
#!/bin/sh
echo "$@" >> /tmp/ledmon-test/calls.log
EOF
chmod 755 "$T/ugreen_leds_cli"

cp /mnt/c/Users/Basti/Desktop/ugreen-idx6011-pro-nas-display/packaging/leds/ugreen-leds-mon.sh "$T/"

# watch ALL sd* disks (whichever backs /tmp will show the activity)
ALLDISKS=$(ls /sys/block | grep '^sd' | sort | head -6 | tr '\n' ' ')
[ -n "$ALLDISKS" ] || { echo "no sd* disk in /sys/block"; exit 1; }
NIC=$(ls /sys/class/net | grep -v lo | head -1)
echo "using disks=$ALLDISKS nic=$NIC"

cat > "$T/ugreen-leds-mon.conf" <<EOF
INTERVAL=1
DISKS="$ALLDISKS"
NICS="$NIC"
EOF

UGREEN_LEDS_CLI="$T/ugreen_leds_cli" UGREEN_LEDS_PIDFILE="$T/pid" \
    dash "$T/ugreen-leds-mon.sh" > "$T/mon.log" 2>&1 &
MON=$!
sleep 2.5

echo "--- phase 1 (idle): expect initial solid writes"
cat "$T/calls.log" 2>/dev/null || echo "(no calls yet)"

# generate disk activity for ~3s
dd if=/dev/zero of=/tmp/ledmon-burn bs=1M count=600 conv=fsync 2>/dev/null
sleep 2
echo "--- phase 2 (after disk I/O): expect a -blink call for disk1"
tail -5 "$T/calls.log"

rm -f /tmp/ledmon-burn
sync
sleep 4
echo "--- phase 3 (idle again): expect disk1 back to solid (-on -brightness)"
tail -3 "$T/calls.log"

kill "$MON" 2>/dev/null || true
echo "--- full call log:"
cat "$T/calls.log"
echo "--- monitor stdout:"
cat "$T/mon.log"
