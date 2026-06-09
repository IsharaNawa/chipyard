#!/bin/bash
#
# run_pipeline.sh - Launch the 2-core interrupt-aware pipelined squares app
#
# Pins the consumer to CPU 2 and the producer to CPU 1, starts the consumer
# first (so it can drain anything stale and arm CONS_WAIT before the producer
# starts pushing), then starts the producer. Waits for both. Forwards their
# stdout/stderr so the caller can capture [TIMING] lines.
#
# Usage:
#   sudo ./run_pipeline.sh

set -e

BIN_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}" )" && pwd)/bin"
PROD="$BIN_DIR/core1_producer"
CONS="$BIN_DIR/core2_consumer"

if [ "$EUID" -ne 0 ]; then
    echo "[SCRIPT] Error: must run as root for /dev/uioN access."  >&2
    echo "[SCRIPT] Try: sudo ./run_pipeline.sh"                    >&2
    exit 1
fi

if [ ! -x "$PROD" ] || [ ! -x "$CONS" ]; then
    echo "[SCRIPT] Error: binaries missing. Run 'make' first."     >&2
    exit 1
fi

# Sanity: make sure /dev/uio0 and /dev/uio1 exist (the dual-IRQ isb_uio.ko
# driver must be loaded; uio0 = producer side, uio1 = consumer side of ISB#0).
if [ ! -e /dev/uio0 ] || [ ! -e /dev/uio1 ]; then
    echo "[SCRIPT] Error: /dev/uio0 or /dev/uio1 missing. Load isb_uio.ko first:" >&2
    echo "[SCRIPT]   sudo insmod /lib/modules/isb_uio/isb_uio.ko"                  >&2
    exit 1
fi

# Optional: warn if CPUs 1 and 2 aren't isolated. (bootargs has isolcpus=1,2,3,4)
if [ -r /sys/devices/system/cpu/isolated ]; then
    ISO=$(cat /sys/devices/system/cpu/isolated)
    echo "[SCRIPT] isolated CPUs = '$ISO'"
fi

# Clean stale output from a prior run
rm -f output.txt

echo "[SCRIPT] =============================================================="
echo "[SCRIPT]   2-core interrupt-aware pipelined squares app"
echo "[SCRIPT]   producer = core 1 (/dev/uio0), consumer = core 2 (/dev/uio1)"
echo "[SCRIPT] =============================================================="

# Start consumer first so it's ready (and the FIFO is drained) before
# the producer slams in 256 items.
echo "[SCRIPT] [1/2] starting consumer on CPU 2..."
taskset -c 2 "$CONS" &
CONS_PID=$!
echo "[SCRIPT]   consumer PID=$CONS_PID"

# Brief settle so the consumer has time to open /dev/uio1 and arm.
sleep 1

echo "[SCRIPT] [2/2] starting producer on CPU 1..."
taskset -c 1 "$PROD" &
PROD_PID=$!
echo "[SCRIPT]   producer PID=$PROD_PID"

echo "[SCRIPT] waiting for both to finish..."
wait $PROD_PID
PROD_EXIT=$?
echo "[SCRIPT] producer (PID $PROD_PID) exit=$PROD_EXIT"

wait $CONS_PID
CONS_EXIT=$?
echo "[SCRIPT] consumer (PID $CONS_PID) exit=$CONS_EXIT"

echo "[SCRIPT] =============================================================="
echo "[SCRIPT]   pipeline finished. output.txt = $(wc -l < output.txt 2>/dev/null || echo 0) lines"
echo "[SCRIPT] =============================================================="

# Non-zero exit if either side failed
[ "$PROD_EXIT" -eq 0 ] && [ "$CONS_EXIT" -eq 0 ]
