/*
 * core2_consumer.c
 *
 * Consumer running pinned to CPU 2.
 *
 * Task:
 *   Open output.txt for writing. Read 10000 uint64_t values from
 *   InterruptAwareMMIOFIFO via /dev/uio1 (consumer side of ISB #0)
 *   in 20 bursts of 500 items. For each item, write (value + 1) to
 *   output.txt. Between bursts, sleep for an increasing amount of
 *   time (5, 6, 7 ... 24 s) so the producer can fill the FIFO.
 *
 *   At the end, dump every visible debug register and the producer
 *   and consumer wake statistics. Also emit [TIMING] lines for
 *   timing_test.sh to parse.
 *
 * Wake protocol:
 *   When deq_valid=0 (FIFO empty), we arm CONS_WAIT_REQUEST and block
 *   in read(uio_fd, &cnt, 4). The HW fires consumer_wake on the rising
 *   crossing of consumer_high_watermark (default = depth/4 = 64) when
 *   the producer enqueues enough items.
 *
 * Build:
 *   make
 *
 * Run (launched by run_pipeline.sh as taskset -c 2):
 *   sudo taskset -c 2 ./bin/core2_consumer
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sched.h>
#include <sys/mman.h>
#include <poll.h>
#include <time.h>

/* ------------ MMIO register map (matches InterruptAwareMMIOFIFO.scala) ------- */
#define ISB_STATUS         0x00
#define ISB_ENQ            0x08
#define ISB_DEQ            0x10
#define ISB_PROD_WAIT_REQ  0x18
#define ISB_CONS_WAIT_REQ  0x20
#define ISB_IRQ_STATUS     0x28
#define ISB_PROD_HIGH_WM   0x30
#define ISB_PROD_LOW_WM    0x38
#define ISB_CONS_HIGH_WM   0x40
#define ISB_CONS_LOW_WM    0x48

/* Debug event counters (present only when isDebug=true in the Chisel config;
 * the deployed Genesys2 80 MHz config sets isDebug=true for all 4 ISBs). */
#define ISB_DBG_P_WAIT_REQ      0x50  /* p_wait_req_count        */
#define ISB_DBG_P_IMMEDIATE     0x58  /* p_immediate_irq_count   */
#define ISB_DBG_P_NORMAL_WAKE   0x60  /* p_normal_wake_count     */
#define ISB_DBG_P_WAKE_PENDING  0x68  /* p_wake_pending_count    */
#define ISB_DBG_P_WAKE_CLEARING 0x70  /* p_wake_clearing_count   */
#define ISB_DBG_C_WAIT_REQ      0x78  /* c_wait_req_count        */
#define ISB_DBG_C_IMMEDIATE     0x80  /* c_immediate_irq_count   */
#define ISB_DBG_C_NORMAL_WAKE   0x88  /* c_normal_wake_count     */
#define ISB_DBG_C_WAKE_PENDING  0x90  /* c_wake_pending_count    */
#define ISB_DBG_C_WAKE_CLEARING 0x98  /* c_wake_clearing_count   */

#define STATUS_DEQ_VALID(s)     (((s) >> 0) & 0x1u)
#define STATUS_ENQ_READY(s)     (((s) >> 1) & 0x1u)
#define STATUS_COUNT(s)         ((s) >> 2)

#define IRQ_PROD_WAKE(i)        (((i) >> 0) & 0x1u)
#define IRQ_CONS_WAKE(i)        (((i) >> 1) & 0x1u)
#define IRQ_PROD_WAITING(i)     (((i) >> 8) & 0x1u)
#define IRQ_CONS_WAITING(i)     (((i) >> 9) & 0x1u)

/* Configuration */
#define UIO_PATH          "/dev/uio1"   /* consumer side of ISB #0 */
#define MMIO_SIZE         4096
#define N_ITEMS           5000
#define ITEMS_PER_BURST   500
#define N_BURSTS          (N_ITEMS / ITEMS_PER_BURST)   /* 10 */
#define INITIAL_PAUSE_S   5
#define OUTPUT_FILE       "output.txt"
#define TARGET_CORE       2

static volatile uint8_t *g_mmio;

static inline uint64_t rd64(unsigned off) {
    return *(volatile uint64_t *)(g_mmio + off);
}
static inline void wr64(unsigned off, uint64_t val) {
    *(volatile uint64_t *)(g_mmio + off) = val;
}

static double now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static int pin_to_cpu(int cpu) {
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);
    if (sched_setaffinity(0, sizeof(set), &set) != 0) {
        perror("sched_setaffinity");
        return -1;
    }
    return 0;
}

/* Drain a possibly-pending UIO event from a previous run / stale latch. */
static void drain_stale_uio_event(int fd) {
    struct pollfd pfd = { .fd = fd, .events = POLLIN };
    int pret = poll(&pfd, 1, 50);
    if (pret > 0 && (pfd.revents & POLLIN)) {
        uint32_t stale = 0;
        if (read(fd, &stale, 4) == 4) {
            fprintf(stderr, "[Core 2] drained stale consumer-wake event_count=%u\n",
                    stale);
        }
    }
}

int main(void) {
    /* ---- pin to dedicated isolated core ---- */
    if (pin_to_cpu(TARGET_CORE) != 0) return 1;
    fprintf(stderr, "[Core 2] pinned to CPU %d\n", TARGET_CORE);

    /* ---- open UIO + mmap ---- */
    fprintf(stderr, "[Core 2] open %s ...\n", UIO_PATH);
    int fd = open(UIO_PATH, O_RDWR | O_SYNC);
    if (fd < 0) { perror("open " UIO_PATH); return 1; }

    g_mmio = mmap(NULL, MMIO_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (g_mmio == MAP_FAILED) { perror("mmap"); return 1; }
    fprintf(stderr, "[Core 2] mmap base=%p\n", (void *)g_mmio);

    uint32_t prod_high = (uint32_t)rd64(ISB_PROD_HIGH_WM);
    uint32_t prod_low  = (uint32_t)rd64(ISB_PROD_LOW_WM);
    uint32_t cons_high = (uint32_t)rd64(ISB_CONS_HIGH_WM);
    uint32_t cons_low  = (uint32_t)rd64(ISB_CONS_LOW_WM);
    fprintf(stderr,
            "[Core 2] watermarks: prod_high=%u prod_low=%u  cons_high=%u cons_low=%u\n",
            prod_high, prod_low, cons_high, cons_low);

    /* ---- output file ---- */
    FILE *fp = fopen(OUTPUT_FILE, "w");
    if (!fp) { perror("fopen " OUTPUT_FILE); return 1; }
    fprintf(stderr, "[Core 2] writing to %s\n", OUTPUT_FILE);

    /* ---- prime IRQ path ---- */
    uint32_t unmask = 1;
    if (write(fd, &unmask, 4) != 4) {
        perror("write(unmask)");
        return 1;
    }
    drain_stale_uio_event(fd);

    /* ---- counters & timers ---- */
    int      wake_count   = 0;     /* consumer_wake events received */
    double   blocked_s    = 0.0;   /* total time blocked in read() */
    double   pause_s      = 0.0;   /* total time spent in inter-burst sleeps */
    uint64_t deqd         = 0;     /* total items dequeued */

    double t_start = now_s();

    for (int burst = 0; burst < N_BURSTS; burst++) {
        int got = 0;
        while (got < ITEMS_PER_BURST) {
            uint64_t st = rd64(ISB_STATUS);
            while (STATUS_DEQ_VALID(st) && got < ITEMS_PER_BURST) {
                uint64_t v = rd64(ISB_DEQ);
                fprintf(fp, "%llu\n", (unsigned long long)(v + 1));
                got++;
                deqd++;
                st = rd64(ISB_STATUS);
            }
            if (got >= ITEMS_PER_BURST) break;

            /* FIFO drained -> arm CONS_WAIT_REQ and sleep on the IRQ */
            wr64(ISB_CONS_WAIT_REQ, 1);
            if (write(fd, &unmask, 4) != 4) {
                perror("write(re-unmask)");
                return 1;
            }

            double t_block = now_s();
            uint32_t evt = 0;
            ssize_t n = read(fd, &evt, 4);   /* hart goes to WFI here */
            double dt = now_s() - t_block;
            if (n != 4) { perror("read"); return 1; }

            blocked_s += dt;
            wake_count++;
            fprintf(stderr,
                    "[Core 2] consumer-wake #%d in burst %d at got=%d/%d  blocked=%.3f ms\n",
                    wake_count, burst, got, ITEMS_PER_BURST, dt * 1000.0);
        }

        /* Flush this burst to disk so a reader can tail output.txt */
        fflush(fp);

        /* Inter-burst pause: 5, 6, 7 ... seconds (per user spec) */
        int pause = INITIAL_PAUSE_S + burst;
        uint64_t cur = STATUS_COUNT(rd64(ISB_STATUS));
        fprintf(stderr,
                "[Core 2] burst %d done  deqd=%llu  pause %ds (FIFO count before pause=%lu)\n",
                burst, (unsigned long long)deqd, pause, (unsigned long)cur);

        double t_pause = now_s();
        struct timespec ts = { .tv_sec = pause, .tv_nsec = 0 };
        while (nanosleep(&ts, &ts) == -1 && errno == EINTR) { /* retry */ }
        pause_s += now_s() - t_pause;
    }

    double t_end = now_s();
    double total_ms     = (t_end - t_start) * 1000.0;
    double blocked_ms   = blocked_s * 1000.0;
    double pause_ms     = pause_s   * 1000.0;
    double active_ms    = total_ms - blocked_ms - pause_ms;

    fclose(fp);

    /* ---- final register dump for visibility ---- */
    uint64_t fin_status = rd64(ISB_STATUS);
    uint64_t fin_irq    = rd64(ISB_IRQ_STATUS);

    /* Debug event counters (registers exposed when isDebug=true). These
     * count distinct, mutually exclusive FSM events. They are the
     * ground-truth for what the HW saw. */
    uint32_t d_p_wait     = (uint32_t)rd64(ISB_DBG_P_WAIT_REQ);
    uint32_t d_p_imm      = (uint32_t)rd64(ISB_DBG_P_IMMEDIATE);
    uint32_t d_p_normal   = (uint32_t)rd64(ISB_DBG_P_NORMAL_WAKE);
    uint32_t d_p_pending  = (uint32_t)rd64(ISB_DBG_P_WAKE_PENDING);
    uint32_t d_p_clearing = (uint32_t)rd64(ISB_DBG_P_WAKE_CLEARING);
    uint32_t d_c_wait     = (uint32_t)rd64(ISB_DBG_C_WAIT_REQ);
    uint32_t d_c_imm      = (uint32_t)rd64(ISB_DBG_C_IMMEDIATE);
    uint32_t d_c_normal   = (uint32_t)rd64(ISB_DBG_C_NORMAL_WAKE);
    uint32_t d_c_pending  = (uint32_t)rd64(ISB_DBG_C_WAKE_PENDING);
    uint32_t d_c_clearing = (uint32_t)rd64(ISB_DBG_C_WAKE_CLEARING);

    fprintf(stderr, "[Core 2] DONE  deqd=%llu  wrote %s\n",
            (unsigned long long)deqd, OUTPUT_FILE);
    fprintf(stderr, "[Core 2] ===== final ISB debug register dump =====\n");
    fprintf(stderr, "[Core 2]   STATUS       = 0x%llx  (count=%lu enq_ready=%u deq_valid=%u)\n",
            (unsigned long long)fin_status,
            (unsigned long)STATUS_COUNT(fin_status),
            (unsigned)STATUS_ENQ_READY(fin_status),
            (unsigned)STATUS_DEQ_VALID(fin_status));
    fprintf(stderr, "[Core 2]   IRQ_STATUS   = 0x%llx  (prod_wake=%u cons_wake=%u prod_waiting=%u cons_waiting=%u)\n",
            (unsigned long long)fin_irq,
            (unsigned)IRQ_PROD_WAKE(fin_irq),
            (unsigned)IRQ_CONS_WAKE(fin_irq),
            (unsigned)IRQ_PROD_WAITING(fin_irq),
            (unsigned)IRQ_CONS_WAITING(fin_irq));
    fprintf(stderr, "[Core 2]   PROD_HIGH_WM = %u\n", prod_high);
    fprintf(stderr, "[Core 2]   PROD_LOW_WM  = %u\n", prod_low);
    fprintf(stderr, "[Core 2]   CONS_HIGH_WM = %u\n", cons_high);
    fprintf(stderr, "[Core 2]   CONS_LOW_WM  = %u\n", cons_low);
    fprintf(stderr, "[Core 2]   --- producer-side debug counters ---\n");
    fprintf(stderr, "[Core 2]   p_wait_req_count      = %u\n", d_p_wait);
    fprintf(stderr, "[Core 2]   p_immediate_irq_count = %u  (count <= prod_low_wm at arm time -> wake fired immediately)\n", d_p_imm);
    fprintf(stderr, "[Core 2]   p_normal_wake_count   = %u  (count crossed prod_low_wm going DOWN while producer was waiting)\n", d_p_normal);
    fprintf(stderr, "[Core 2]   p_wake_pending_count  = %u  (count > prod_low_wm at arm time -> entered waiting)\n", d_p_pending);
    fprintf(stderr, "[Core 2]   p_wake_clearing_count = %u  (stale producer_wake latch cleared by re-arm)\n", d_p_clearing);
    fprintf(stderr, "[Core 2]   --- consumer-side debug counters ---\n");
    fprintf(stderr, "[Core 2]   c_wait_req_count      = %u\n", d_c_wait);
    fprintf(stderr, "[Core 2]   c_immediate_irq_count = %u  (count >= cons_high_wm at arm time -> wake fired immediately)\n", d_c_imm);
    fprintf(stderr, "[Core 2]   c_normal_wake_count   = %u  (count crossed cons_high_wm going UP while consumer was waiting)\n", d_c_normal);
    fprintf(stderr, "[Core 2]   c_wake_pending_count  = %u  (count < cons_high_wm at arm time -> entered waiting)\n", d_c_pending);
    fprintf(stderr, "[Core 2]   c_wake_clearing_count = %u  (stale consumer_wake latch cleared by re-arm)\n", d_c_clearing);
    fprintf(stderr, "[Core 2]   --- consumer timing summary ---\n");
    fprintf(stderr, "[Core 2]   wake_count   = %d\n", wake_count);
    fprintf(stderr, "[Core 2]   blocked_ms   = %.3f\n", blocked_ms);
    fprintf(stderr, "[Core 2]   pause_ms     = %.3f\n", pause_ms);
    fprintf(stderr, "[Core 2]   active_ms    = %.3f\n", active_ms);
    fprintf(stderr, "[Core 2]   total_ms     = %.3f\n", total_ms);
    fprintf(stderr, "[Core 2] ==========================================\n");

    /* ---- TIMING lines parsed by timing_test.sh ---- */
    fprintf(stdout, "[Core 2] [TIMING] ConsumerTotal: %.3f ms\n",        total_ms);
    fprintf(stdout, "[Core 2] [TIMING] ConsumerActive: %.3f ms\n",       active_ms);
    fprintf(stdout, "[Core 2] [TIMING] ConsumerBlocked: %.3f ms\n",      blocked_ms);
    fprintf(stdout, "[Core 2] [TIMING] ConsumerPause: %.3f ms\n",        pause_ms);
    fprintf(stdout, "[Core 2] [TIMING] ConsumerWakeCount: %d count\n",   wake_count);
    fprintf(stdout, "[Core 2] [TIMING] ConsumerItems: %llu count\n",
            (unsigned long long)deqd);
    /* Debug counters (one [TIMING] line per counter so timing_test.sh
     * can parse and average them across iterations). */
    fprintf(stdout, "[Core 2] [TIMING] DbgPWaitReq: %u count\n",         d_p_wait);
    fprintf(stdout, "[Core 2] [TIMING] DbgPImmediate: %u count\n",       d_p_imm);
    fprintf(stdout, "[Core 2] [TIMING] DbgPNormalWake: %u count\n",      d_p_normal);
    fprintf(stdout, "[Core 2] [TIMING] DbgPWakePending: %u count\n",     d_p_pending);
    fprintf(stdout, "[Core 2] [TIMING] DbgPWakeClearing: %u count\n",    d_p_clearing);
    fprintf(stdout, "[Core 2] [TIMING] DbgCWaitReq: %u count\n",         d_c_wait);
    fprintf(stdout, "[Core 2] [TIMING] DbgCImmediate: %u count\n",       d_c_imm);
    fprintf(stdout, "[Core 2] [TIMING] DbgCNormalWake: %u count\n",      d_c_normal);
    fprintf(stdout, "[Core 2] [TIMING] DbgCWakePending: %u count\n",     d_c_pending);
    fprintf(stdout, "[Core 2] [TIMING] DbgCWakeClearing: %u count\n",    d_c_clearing);
    fflush(stdout);

    munmap((void *)g_mmio, MMIO_SIZE);
    close(fd);
    return 0;
}
