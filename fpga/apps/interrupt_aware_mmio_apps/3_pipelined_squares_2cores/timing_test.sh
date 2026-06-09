#!/bin/bash
#
# timing_test.sh - Multi-iteration timing harness for the 2-core pipelined
# squares app, modelled on the JPEG decoder timing scripts.
#
# Runs run_pipeline.sh N times, parses [TIMING] lines from the producer
# and consumer (including all 10 ISB hardware debug event counters
# exposed when isDebug=true), computes averages, and writes everything
# to Results.log in the current working directory.
#
# Usage:
#   sudo ./timing_test.sh <iterations>
#
# Example:
#   sudo ./timing_test.sh 5

GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

usage() {
    echo "Usage: $0 <iterations>"
    echo "  iterations : positive integer (number of times to run the pipeline)"
    echo "Example: sudo $0 5"
    exit 1
}

[ $# -eq 1 ] || usage
ITERATIONS=$1
if ! [[ "$ITERATIONS" =~ ^[0-9]+$ ]] || [ "$ITERATIONS" -lt 1 ]; then
    echo -e "${RED}Error: iterations must be a positive integer${NC}"
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}" )" && pwd)"
RUN_SCRIPT="$SCRIPT_DIR/run_pipeline.sh"

if [ ! -x "$RUN_SCRIPT" ]; then
    echo -e "${RED}Error: $RUN_SCRIPT not found or not executable${NC}"
    exit 1
fi

# -------- per-iteration arrays --------
declare -a prod_total prod_active prod_sleep prod_wake_cnt prod_items
declare -a cons_total cons_active cons_blocked cons_pause cons_wake_cnt cons_items

# producer-side HW debug counters
declare -a dbg_p_wait dbg_p_imm dbg_p_normal dbg_p_pending dbg_p_clearing
# consumer-side HW debug counters
declare -a dbg_c_wait dbg_c_imm dbg_c_normal dbg_c_pending dbg_c_clearing

LOG_FILE="Results.log"

{
    echo "================================================================"
    echo "  2-Core Interrupt-Aware Pipelined Squares App - Timing Log"
    echo "================================================================"
    echo "Date/Time:   $(date '+%Y-%m-%d %H:%M:%S')"
    echo "Iterations:  $ITERATIONS"
    echo "Producer:    core1_producer pinned to CPU 1 -> /dev/uio0"
    echo "Consumer:    core2_consumer pinned to CPU 2 -> /dev/uio1"
    echo "Items:       5000 uint64_t squares, 10 bursts of 500"
    echo "Pause:       5, 6, 7, ... s after each consumer burst"
    echo ""
    echo "Producer time decomposition:"
    echo "  ProducerActive : wall time actually pushing items via MMIO"
    echo "  ProducerSleep  : wall time hart is in WFI (blocked in read(uio0))"
    echo "                   waiting for the producer-wake IRQ"
    echo "  ProducerTotal  = ProducerActive + ProducerSleep"
    echo ""
    echo "ISB hardware debug event counters (one read at end of run):"
    echo "  Producer side:"
    echo "    p_wait_req_count      : number of PROD_WAIT_REQUEST writes"
    echo "    p_immediate_irq_count : count <= prod_low_wm at arm -> wake fired immediately"
    echo "    p_normal_wake_count   : count crossed prod_low_wm going DOWN while waiting"
    echo "    p_wake_pending_count  : count > prod_low_wm at arm -> entered waiting state"
    echo "    p_wake_clearing_count : a stale producer_wake latch was cleared by re-arm"
    echo "  Consumer side (symmetric, with cons_high_wm and direction UP):"
    echo "    c_wait_req_count, c_immediate_irq_count, c_normal_wake_count,"
    echo "    c_wake_pending_count, c_wake_clearing_count"
    echo "================================================================"
    echo ""
    echo "Per-iteration TIMING summary (ms / counts):"
    echo ""
    printf "%-4s %-12s %-12s %-12s %-7s %-7s %-12s %-12s %-12s %-12s %-7s %-7s\n" \
        "Iter" "ProdTot(ms)" "ProdAct(ms)" "ProdSlp(ms)" "PWakes" "PItems" \
        "ConsTot(ms)" "ConsAct(ms)" "ConsBlk(ms)" "ConsPau(ms)" "CWakes" "CItems"
    printf "%-4s %-12s %-12s %-12s %-7s %-7s %-12s %-12s %-12s %-12s %-7s %-7s\n" \
        "----" "------------" "------------" "------------" "-------" "-------" \
        "------------" "------------" "------------" "------------" "-------" "-------"
} > "$LOG_FILE"

echo -e "${BLUE}=================================================================${NC}"
echo -e "${BLUE}  2-Core Pipelined Squares Timing Test - $ITERATIONS iteration(s)${NC}"
echo -e "${BLUE}=================================================================${NC}"
echo -e "Log file: ${GREEN}$SCRIPT_DIR/$LOG_FILE${NC}"
echo ""

# parse_timing <output> <stage-name>  ->  prints number (or 0 if not found)
parse_timing() {
    local out="$1" stage="$2"
    local v
    v=$(echo "$out" | grep -E "\[TIMING\] ${stage}:" | tail -1 | awk '{print $(NF-1)}')
    echo "${v:-0}"
}

for i in $(seq 1 "$ITERATIONS"); do
    echo -e "${YELLOW}--- Iteration $i / $ITERATIONS ---${NC}"

    # Run the pipeline and capture combined output (stdout has [TIMING] lines)
    OUTPUT=$("$RUN_SCRIPT" 2>&1)
    RC=$?

    if [ $RC -ne 0 ]; then
        echo -e "${RED}Iteration $i failed (exit=$RC). Output:${NC}"
        echo "$OUTPUT"
        echo "[Iteration $i FAILED with exit=$RC]" >> "$LOG_FILE"
        continue
    fi

    # ---- Producer timing ----
    pt=$(parse_timing "$OUTPUT" "ProducerTotal")
    pa=$(parse_timing "$OUTPUT" "ProducerActive")
    ps=$(parse_timing "$OUTPUT" "ProducerSleep")
    pw=$(parse_timing "$OUTPUT" "ProducerWakeCount")
    pi=$(parse_timing "$OUTPUT" "ProducerItems")

    # ---- Consumer timing ----
    ct=$(parse_timing "$OUTPUT" "ConsumerTotal")
    ca=$(parse_timing "$OUTPUT" "ConsumerActive")
    cb=$(parse_timing "$OUTPUT" "ConsumerBlocked")
    cp=$(parse_timing "$OUTPUT" "ConsumerPause")
    cw=$(parse_timing "$OUTPUT" "ConsumerWakeCount")
    ci=$(parse_timing "$OUTPUT" "ConsumerItems")

    # ---- Hardware debug event counters (read from MMIO at end of run) ----
    d_p_wait=$(parse_timing     "$OUTPUT" "DbgPWaitReq")
    d_p_imm=$(parse_timing      "$OUTPUT" "DbgPImmediate")
    d_p_normal=$(parse_timing   "$OUTPUT" "DbgPNormalWake")
    d_p_pending=$(parse_timing  "$OUTPUT" "DbgPWakePending")
    d_p_clearing=$(parse_timing "$OUTPUT" "DbgPWakeClearing")
    d_c_wait=$(parse_timing     "$OUTPUT" "DbgCWaitReq")
    d_c_imm=$(parse_timing      "$OUTPUT" "DbgCImmediate")
    d_c_normal=$(parse_timing   "$OUTPUT" "DbgCNormalWake")
    d_c_pending=$(parse_timing  "$OUTPUT" "DbgCWakePending")
    d_c_clearing=$(parse_timing "$OUTPUT" "DbgCWakeClearing")

    prod_total+=("$pt"); prod_active+=("$pa"); prod_sleep+=("$ps")
    prod_wake_cnt+=("$pw"); prod_items+=("$pi")
    cons_total+=("$ct"); cons_active+=("$ca"); cons_blocked+=("$cb")
    cons_pause+=("$cp"); cons_wake_cnt+=("$cw"); cons_items+=("$ci")

    dbg_p_wait+=("$d_p_wait");       dbg_p_imm+=("$d_p_imm")
    dbg_p_normal+=("$d_p_normal");   dbg_p_pending+=("$d_p_pending")
    dbg_p_clearing+=("$d_p_clearing")
    dbg_c_wait+=("$d_c_wait");       dbg_c_imm+=("$d_c_imm")
    dbg_c_normal+=("$d_c_normal");   dbg_c_pending+=("$d_c_pending")
    dbg_c_clearing+=("$d_c_clearing")

    {
        printf "%-4d %-12s %-12s %-12s %-7s %-7s %-12s %-12s %-12s %-12s %-7s %-7s\n" \
            "$i" "$pt" "$pa" "$ps" "$pw" "$pi" "$ct" "$ca" "$cb" "$cp" "$cw" "$ci"
        echo ""
        echo "  iteration $i HW debug counters:"
        printf "    producer: wait_req=%s immediate=%s normal_wake=%s wake_pending=%s wake_clearing=%s\n" \
            "$d_p_wait" "$d_p_imm" "$d_p_normal" "$d_p_pending" "$d_p_clearing"
        printf "    consumer: wait_req=%s immediate=%s normal_wake=%s wake_pending=%s wake_clearing=%s\n" \
            "$d_c_wait" "$d_c_imm" "$d_c_normal" "$d_c_pending" "$d_c_clearing"
        echo ""
        echo "----- Iteration $i full output -----"
        echo "$OUTPUT"
        echo "----- End iteration $i output -----"
        echo ""
    } >> "$LOG_FILE"

    echo "    producer total=${pt}ms active=${pa}ms sleep=${ps}ms wakes=${pw}"
    echo "    consumer total=${ct}ms active=${ca}ms blocked=${cb}ms pause=${cp}ms wakes=${cw}"
    echo "    hw debug:  P[w=${d_p_wait} imm=${d_p_imm} norm=${d_p_normal} pend=${d_p_pending} clr=${d_p_clearing}]"
    echo "               C[w=${d_c_wait} imm=${d_c_imm} norm=${d_c_normal} pend=${d_c_pending} clr=${d_c_clearing}]"
done

# Helpers for stats
calc_avg() {
    printf '%s\n' "$@" | awk '{sum+=$1; n++} END { if (n>0) printf "%.3f", sum/n; else printf "0"; }'
}
calc_avg_int() {
    printf '%s\n' "$@" | awk '{sum+=$1; n++} END { if (n>0) printf "%.2f", sum/n; else printf "0"; }'
}

avg_pt=$(calc_avg     "${prod_total[@]}")
avg_pa=$(calc_avg     "${prod_active[@]}")
avg_ps=$(calc_avg     "${prod_sleep[@]}")
avg_pw=$(calc_avg_int "${prod_wake_cnt[@]}")
avg_pi=$(calc_avg_int "${prod_items[@]}")

avg_ct=$(calc_avg     "${cons_total[@]}")
avg_ca=$(calc_avg     "${cons_active[@]}")
avg_cb=$(calc_avg     "${cons_blocked[@]}")
avg_cp=$(calc_avg     "${cons_pause[@]}")
avg_cw=$(calc_avg_int "${cons_wake_cnt[@]}")
avg_ci=$(calc_avg_int "${cons_items[@]}")

avg_dpw=$(calc_avg_int "${dbg_p_wait[@]}")
avg_dpi=$(calc_avg_int "${dbg_p_imm[@]}")
avg_dpn=$(calc_avg_int "${dbg_p_normal[@]}")
avg_dpp=$(calc_avg_int "${dbg_p_pending[@]}")
avg_dpc=$(calc_avg_int "${dbg_p_clearing[@]}")
avg_dcw=$(calc_avg_int "${dbg_c_wait[@]}")
avg_dci=$(calc_avg_int "${dbg_c_imm[@]}")
avg_dcn=$(calc_avg_int "${dbg_c_normal[@]}")
avg_dcp=$(calc_avg_int "${dbg_c_pending[@]}")
avg_dcc=$(calc_avg_int "${dbg_c_clearing[@]}")

{
    echo ""
    echo "================================================================"
    echo "  Averages over $ITERATIONS iteration(s)"
    echo "================================================================"
    echo ""
    echo "--- Producer (core 1, /dev/uio0) ---"
    printf "  ProducerTotal      : %12s ms\n"   "$avg_pt"
    printf "  ProducerActive     : %12s ms   (real MMIO work)\n"  "$avg_pa"
    printf "  ProducerSleep      : %12s ms   (hart in WFI waiting for producer-wake IRQ)\n"   "$avg_ps"
    printf "  ProducerWakeCount  : %12s\n"      "$avg_pw"
    printf "  ProducerItems      : %12s\n"      "$avg_pi"
    echo ""
    echo "--- Consumer (core 2, /dev/uio1) ---"
    printf "  ConsumerTotal      : %12s ms\n"   "$avg_ct"
    printf "  ConsumerActive     : %12s ms\n"   "$avg_ca"
    printf "  ConsumerBlocked    : %12s ms   (hart in WFI waiting for consumer-wake IRQ)\n"   "$avg_cb"
    printf "  ConsumerPause      : %12s ms   (deliberate inter-burst sleeps)\n"   "$avg_cp"
    printf "  ConsumerWakeCount  : %12s\n"      "$avg_cw"
    printf "  ConsumerItems      : %12s\n"      "$avg_ci"
    echo ""
    echo "--- Hardware debug event counters (producer side) ---"
    printf "  p_wait_req_count      : %12s\n"   "$avg_dpw"
    printf "  p_immediate_irq_count : %12s\n"   "$avg_dpi"
    printf "  p_normal_wake_count   : %12s\n"   "$avg_dpn"
    printf "  p_wake_pending_count  : %12s\n"   "$avg_dpp"
    printf "  p_wake_clearing_count : %12s\n"   "$avg_dpc"
    echo ""
    echo "--- Hardware debug event counters (consumer side) ---"
    printf "  c_wait_req_count      : %12s\n"   "$avg_dcw"
    printf "  c_immediate_irq_count : %12s\n"   "$avg_dci"
    printf "  c_normal_wake_count   : %12s\n"   "$avg_dcn"
    printf "  c_wake_pending_count  : %12s\n"   "$avg_dcp"
    printf "  c_wake_clearing_count : %12s\n"   "$avg_dcc"
    echo "================================================================"
} | tee -a "$LOG_FILE"

echo ""
echo -e "${GREEN}Full log written to: $SCRIPT_DIR/$LOG_FILE${NC}"
