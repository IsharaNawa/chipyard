#!/bin/bash
#
# run_test.sh - Launch two-core RoCC ISB communication test
#
# Starts the consumer on isolated Core 1 first (it blocks waiting for data),
# then starts the producer on Core 0.
#

set -e

echo "=============================================="
echo "  RoCC ISB Two-Core Communication Test"
echo "=============================================="
echo ""

# Check if binaries exist
if [ ! -f core0_producer ]; then
    echo "Error: core0_producer not found"
    echo "Run 'make' first to build the binaries"
    exit 1
fi

if [ ! -f core1_consumer ]; then
    echo "Error: core1_consumer not found"
    echo "Run 'make' first to build the binaries"
    exit 1
fi

# Verify CPU isolation
echo "Checking CPU isolation status..."
if [ -f /sys/devices/system/cpu/isolated ]; then
    ISOLATED=$(cat /sys/devices/system/cpu/isolated)
    echo "  Isolated cores: '$ISOLATED'"
else
    echo "  Warning: Cannot read CPU isolation info"
fi

NPROC=$(nproc)
echo "  Available CPUs: $NPROC"
echo ""

echo "Starting two-core RoCC ISB test..."
echo "  Core 0: Producer (sends 100, 200, ..., 1000 via custom0)"
echo "  Core 1: Consumer (reads values via custom1)"
echo ""

# Start consumer first — it will block on isb_read() until data arrives
echo "[1/2] Starting Core 1 Consumer (isolated core)..."
taskset -c 1 ./core1_consumer &
CONSUMER_PID=$!
echo "  PID: $CONSUMER_PID"

# Brief delay to let consumer initialize and reach the blocking read
sleep 0.5

# Start producer
echo "[2/2] Starting Core 0 Producer..."
taskset -c 0 ./core0_producer &
PRODUCER_PID=$!
echo "  PID: $PRODUCER_PID"

echo ""
echo "Both processes started, waiting for completion..."
echo ""

# Wait for both processes
wait $PRODUCER_PID 2>/dev/null
PRODUCER_EXIT=$?
echo "Producer (PID $PRODUCER_PID) exited with code: $PRODUCER_EXIT"

wait $CONSUMER_PID 2>/dev/null
CONSUMER_EXIT=$?
echo "Consumer (PID $CONSUMER_PID) exited with code: $CONSUMER_EXIT"

echo ""
echo "=============================================="
if [ $PRODUCER_EXIT -eq 0 ] && [ $CONSUMER_EXIT -eq 0 ]; then
    echo "  TEST PASSED"
else
    echo "  TEST FAILED"
    echo "  Producer exit: $PRODUCER_EXIT"
    echo "  Consumer exit: $CONSUMER_EXIT"
fi
echo "=============================================="
