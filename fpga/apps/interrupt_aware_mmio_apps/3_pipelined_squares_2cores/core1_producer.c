/*
 * core1_producer.c
 *
 * Producer running pinned to CPU 1.
 *
 * Task:
 *   Build an array a[0..9999] = i, compute squares b[i] = a[i] * a[i],
 *   and stream b[i] into the InterruptAwareMMIOFIFO via /dev/uio0
 *   (producer side of ISB #0 at 0x4000).
 *
 * Why this is interesting:
 *   The consumer (core 2) processes in bursts of 500 with long pauses
 *   (5, 6, 7 ... 24 s) between bursts. The FIFO depth is 256, so the
 *   producer fills it long before the consumer ever drains again.
 *   When enq_ready=0, instead of spin-polling MMIO we:
 *     1) arm PRODUCER_WAIT_REQUEST
 *     2) unmask the UIO IRQ
 *     3) block in read(uio_fd, &cnt, 4)
 *   The hart goes to WFI. The HW fires producer_wake on the falling
 *   crossing of producer_low_watermark (= depth * 3/4 = 192) when the
 *   consumer eventually drains enough items.
 *
 * Output:
 *   - Streams 10000 uint64_t values into ISB #0.
 *   - Prints [TIMING] lines so timing_test.sh can parse them.
 *
 * Build:
 *   make
 *
 * Run (launched by run_pipeline.sh as taskset -c 1):
 *   sudo taskset -c 1 ./bin/core1_producer
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

#define STATUS_DEQ_VALID(s)  (((s) >> 0) & 0x1u)
#define STATUS_ENQ_READY(s)  (((s) >> 1) & 0x1u)
#define STATUS_COUNT(s)      ((s) >> 2)

/* Configuration */
#define UIO_PATH       "/dev/uio0"     /* producer side of ISB #0 */
#define MMIO_SIZE      4096
#define N_ITEMS        5000
#define TARGET_CORE    1

/*
 * Note on "producer sleep via WFI":
 *   When the FIFO is full we arm PROD_WAIT_REQ and call read(uio_fd).
 *   The kernel parks this task. Because isolcpus=1 reserves CPU 1 only
 *   for this process, the only runnable task on CPU 1 is then the kernel
 *   idle thread, which on RISC-V executes the `wfi` instruction
 *   (arch_cpu_idle -> cpu_do_idle -> wfi). The producer-wake IRQ from
 *   the FIFO FSM hits the PLIC, wakes CPU 1 out of WFI, the kernel ISR
 *   runs, the parked task is set runnable, and read() returns.
 *   So the wall-time spent inside read() == the wall-time the hart is
 *   in WFI. We report that as ProducerSleep below.
 */

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

/* Pin this process to a single CPU. */
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

/* Drain any stale UIO event that may have been latched by the PLIC IP
 * before we got here (e.g. left over from a prior run). 50 ms is plenty. */
static void drain_stale_uio_event(int fd) {
    struct pollfd pfd = { .fd = fd, .events = POLLIN };
    int pret = poll(&pfd, 1, 50);
    if (pret > 0 && (pfd.revents & POLLIN)) {
        uint32_t stale = 0;
        if (read(fd, &stale, 4) == 4) {
            fprintf(stderr, "[Core 1] drained stale producer-wake event_count=%u\n",
                    stale);
        }
    }
}

int main(void) {
    /* ---- pin to dedicated isolated core ---- */
    if (pin_to_cpu(TARGET_CORE) != 0) return 1;
    fprintf(stderr, "[Core 1] pinned to CPU %d\n", TARGET_CORE);

    /* ---- open UIO + mmap ---- */
    fprintf(stderr, "[Core 1] open %s ...\n", UIO_PATH);
    int fd = open(UIO_PATH, O_RDWR | O_SYNC);
    if (fd < 0) { perror("open " UIO_PATH); return 1; }

    g_mmio = mmap(NULL, MMIO_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (g_mmio == MAP_FAILED) { perror("mmap"); return 1; }
    fprintf(stderr, "[Core 1] mmap base=%p\n", (void *)g_mmio);

    uint32_t prod_high = (uint32_t)rd64(ISB_PROD_HIGH_WM);
    uint32_t prod_low  = (uint32_t)rd64(ISB_PROD_LOW_WM);
    uint32_t cons_high = (uint32_t)rd64(ISB_CONS_HIGH_WM);
    uint32_t cons_low  = (uint32_t)rd64(ISB_CONS_LOW_WM);
    fprintf(stderr,
            "[Core 1] watermarks: prod_high=%u prod_low=%u  cons_high=%u cons_low=%u\n",
            prod_high, prod_low, cons_high, cons_low);

    /* ---- build the source array ---- */
    static uint64_t src[N_ITEMS];
    for (int i = 0; i < N_ITEMS; i++) src[i] = (uint64_t)i;

    /* Pre-arm: unmask the UIO IRQ once so we can wait on it whenever the
     * FIFO fills up. The kernel disables it again inside the top-half;
     * we re-unmask after each wake event. */
    uint32_t unmask = 1;
    if (write(fd, &unmask, 4) != 4) {
        perror("write(unmask)");
        return 1;
    }
    drain_stale_uio_event(fd);

    /* ---- counters & timers ---- */
    int      wake_count   = 0;     /* number of producer_wake events received */
    double   sleep_s      = 0.0;   /* total wall time spent in WFI (blocked in read()) */
    uint64_t enqd         = 0;     /* number of items pushed */

    double t_start = now_s();

    while (enqd < N_ITEMS) {
        /* Drain as many items as the FIFO will currently accept */
        uint64_t st = rd64(ISB_STATUS);
        while (STATUS_ENQ_READY(st) && enqd < N_ITEMS) {
            uint64_t v = src[enqd];
            wr64(ISB_ENQ, v * v);     /* push the square */
            enqd++;
            st = rd64(ISB_STATUS);
        }

        if (enqd >= N_ITEMS) break;

        /* FIFO full -> arm PROD_WAIT_REQ and sleep on the IRQ */
        wr64(ISB_PROD_WAIT_REQ, 1);
        /* Re-unmask the IRQ before blocking (kernel masked it after the
         * previous event; even on first iteration the unmask above may
         * already be consumed by the stale drain). */
        if (write(fd, &unmask, 4) != 4) {
            perror("write(re-unmask)");
            return 1;
        }

        double t_block = now_s();
        uint32_t evt = 0;
        ssize_t n = read(fd, &evt, 4);   /* hart goes to WFI here */
        double dt = now_s() - t_block;
        if (n != 4) { perror("read"); return 1; }

        sleep_s += dt;
        wake_count++;
        fprintf(stderr, "[Core 1] producer-wake #%d at enqd=%llu  sleep=%.3f ms\n",
                wake_count, (unsigned long long)enqd, dt * 1000.0);
    }

    double t_end = now_s();
    double total_ms     = (t_end - t_start) * 1000.0;
    double sleep_ms     = sleep_s * 1000.0;
    double active_ms    = total_ms - sleep_ms;

    /* ---- end of stream: drop a sentinel so the consumer knows we're done? ----
     * The consumer reads exactly N_ITEMS items, so no sentinel is required.
     * Just make sure all of our writes are visible. */
    __sync_synchronize();

    /* ---- final register dump for visibility ---- */
    uint64_t fin_status = rd64(ISB_STATUS);
    uint64_t fin_irq    = rd64(ISB_IRQ_STATUS);
    fprintf(stderr, "[Core 1] DONE  enqd=%llu\n", (unsigned long long)enqd);
    fprintf(stderr, "[Core 1] final STATUS=0x%llx (count=%lu enq_ready=%u deq_valid=%u)\n",
            (unsigned long long)fin_status,
            (unsigned long)STATUS_COUNT(fin_status),
            (unsigned)STATUS_ENQ_READY(fin_status),
            (unsigned)STATUS_DEQ_VALID(fin_status));
    fprintf(stderr, "[Core 1] final IRQ_STATUS=0x%llx\n", (unsigned long long)fin_irq);

    /* ---- TIMING lines parsed by timing_test.sh ----
     * Keep the format consistent: "[Core 1] [TIMING] StageName: X.XXX <unit>"
     * ProducerActive: wall time doing real MMIO writes.
     * ProducerSleep:  wall time the hart spent in WFI waiting for producer-wake IRQ.
     *                 (kernel idle thread on isolcpu=1 executes wfi -> woken by PLIC). */
    fprintf(stdout, "[Core 1] [TIMING] ProducerTotal: %.3f ms\n",        total_ms);
    fprintf(stdout, "[Core 1] [TIMING] ProducerActive: %.3f ms\n",       active_ms);
    fprintf(stdout, "[Core 1] [TIMING] ProducerSleep: %.3f ms\n",        sleep_ms);
    fprintf(stdout, "[Core 1] [TIMING] ProducerWakeCount: %d count\n",   wake_count);
    fprintf(stdout, "[Core 1] [TIMING] ProducerItems: %llu count\n",
            (unsigned long long)enqd);
    fflush(stdout);

    munmap((void *)g_mmio, MMIO_SIZE);
    close(fd);
    return 0;
}
