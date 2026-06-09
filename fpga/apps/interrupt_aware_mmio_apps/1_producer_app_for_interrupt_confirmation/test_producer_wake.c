/*
 * test_producer_wake.c
 *
 * Bring-up test for the producer-wake interrupt of InterruptAwareMMIOFIFO.
 *
 * What it does:
 *   1. Opens /dev/uio<N> and mmaps the 4 KB MMIO window.
 *   2. Drains any stale data.
 *   3. Enqueues until the SW-visible enq_ready bit clears
 *      (count >= producer_high_watermark, default = depth).
 *   4. Writes 1 to PRODUCER_WAIT_REQUEST to arm the wait.
 *   5. Spawns a worker thread that, after a 2-second delay, dequeues
 *      enough entries to push count below producer_low_watermark
 *      (default = depth*3/4).
 *   6. The main thread issues read(uio_fd, &irq_cnt, 4). This should
 *      block in the kernel (the hart goes to WFI) until the producer-
 *      wake IRQ fires from the falling crossing of producer_low_wm.
 *   7. Prints elapsed wall time and the IRQ count delta.
 *
 * Build (host, RISC-V cross):
 *   make
 *
 * Run on the FPGA (as root, UIO mmap usually requires it):
 *   sudo ./test_producer_wake          # defaults to /dev/uio0
 *   sudo ./test_producer_wake /dev/uio1
 *
 * Expected output (depth=256, width=64):
 *   open  /dev/uio0 ...
 *   mmap  base=0x...
 *   drained 0 stale entries: count=0
 *   pre-fill: enq 256 entries  -> count=256  enq_ready=0
 *   arm PRODUCER_WAIT_REQUEST
 *   blocking on read()...
 *   [worker]  T+2.00s  draining 72 entries (target count <= prod_low_wm)
 *   [worker]  drained=72 -> count=184  enq_ready=1
 *   unblocked!  elapsed = 2.0xx s  irq_count=1
 *   final: count=184  enq_ready=1  deq_valid=1  irq_status=0x1
 *   PASS: woke at ~T+2s as expected
 *
 * If 'unblocked!' returns immediately on first run, the FSM may have
 * auto-cleared a stale wake; re-arm and re-test.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <poll.h>
#include <time.h>

/* MMIO offsets (must match InterruptAwareMMIOFIFO.scala regmap) */
#define ISB_STATUS              0x00
#define ISB_ENQ                 0x08
#define ISB_DEQ                 0x10
#define ISB_PROD_WAIT_REQ       0x18
#define ISB_CONS_WAIT_REQ       0x20
#define ISB_IRQ_STATUS          0x28
#define ISB_PROD_HIGH_WM        0x30
#define ISB_PROD_LOW_WM         0x38
#define ISB_CONS_HIGH_WM        0x40
#define ISB_CONS_LOW_WM         0x48

/* STATUS layout: bit0 = deq_valid, bit1 = enq_ready, bits[31:2] = count */
#define STATUS_DEQ_VALID(s)     (((s) >> 0) & 0x1u)
#define STATUS_ENQ_READY(s)     (((s) >> 1) & 0x1u)
#define STATUS_COUNT(s)         ((s) >> 2)

/* IRQ_STATUS layout: bit0=prod_wake, bit1=cons_wake,
                      bit8=prod_waiting, bit9=cons_waiting */
#define IRQ_PROD_WAKE(i)        (((i) >> 0) & 0x1u)
#define IRQ_CONS_WAKE(i)        (((i) >> 1) & 0x1u)
#define IRQ_PROD_WAITING(i)     (((i) >> 8) & 0x1u)
#define IRQ_CONS_WAITING(i)     (((i) >> 9) & 0x1u)

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

struct worker_arg {
    int    drain_count;
    double t0;
};

static void *drainer(void *arg) {
    struct worker_arg *wa = (struct worker_arg *)arg;
    /* Sleep 2 seconds so the main thread is definitely parked in read() */
    struct timespec ts = { .tv_sec = 2, .tv_nsec = 0 };
    nanosleep(&ts, NULL);

    uint64_t st = rd64(ISB_STATUS);
    fprintf(stderr, "[worker]  T+%.2fs  draining %d entries "
                    "(target count <= prod_low_wm)  count=%lu  deq_valid=%u\n",
            now_s() - wa->t0, wa->drain_count,
            (unsigned long)STATUS_COUNT(st), (unsigned)STATUS_DEQ_VALID(st));

    int drained = 0;
    for (int i = 0; i < wa->drain_count; i++) {
        st = rd64(ISB_STATUS);
        if (!STATUS_DEQ_VALID(st)) {
            fprintf(stderr, "[worker]  deq_valid=0 after %d drains, count=%lu\n",
                    drained, (unsigned long)STATUS_COUNT(st));
            break;
        }
        (void)rd64(ISB_DEQ);
        drained++;
    }
    st = rd64(ISB_STATUS);
    fprintf(stderr, "[worker]  drained=%d  -> count=%lu  enq_ready=%u\n",
            drained, (unsigned long)STATUS_COUNT(st), (unsigned)STATUS_ENQ_READY(st));
    return NULL;
}

int main(int argc, char **argv) {
    const char *path = (argc > 1) ? argv[1] : "/dev/uio0";

    fprintf(stderr, "open  %s ...\n", path);
    int fd = open(path, O_RDWR | O_SYNC);
    if (fd < 0) { perror("open"); return 1; }

    g_mmio = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (g_mmio == MAP_FAILED) { perror("mmap"); return 1; }
    fprintf(stderr, "mmap  base=%p\n", (void *)g_mmio);

    /* Read watermarks so the test adapts to any depth */
    uint32_t prod_high = (uint32_t)rd64(ISB_PROD_HIGH_WM);
    uint32_t prod_low  = (uint32_t)rd64(ISB_PROD_LOW_WM);
    uint32_t cons_high = (uint32_t)rd64(ISB_CONS_HIGH_WM);
    uint32_t cons_low  = (uint32_t)rd64(ISB_CONS_LOW_WM);
    fprintf(stderr, "watermarks: prod_high=%u prod_low=%u  cons_high=%u cons_low=%u\n",
            prod_high, prod_low, cons_high, cons_low);

    /* Drain anything stale from a previous run */
    uint64_t st = rd64(ISB_STATUS);
    int pre = 0;
    while (STATUS_DEQ_VALID(st) && pre < 100000) {
        (void)rd64(ISB_DEQ);
        st = rd64(ISB_STATUS);
        pre++;
    }
    fprintf(stderr, "drained %d stale entries: count=%lu\n",
            pre, (unsigned long)STATUS_COUNT(st));

    /* Pre-fill until enq_ready=0 (count == prod_high_wm) */
    int enq = 0;
    while (STATUS_ENQ_READY(rd64(ISB_STATUS)) && enq < 200000) {
        wr64(ISB_ENQ, (uint64_t)0xDEAD0000ULL | (uint32_t)enq);
        enq++;
    }
    st = rd64(ISB_STATUS);
    fprintf(stderr, "pre-fill: enq %d entries  -> count=%lu  enq_ready=%u\n",
            enq, (unsigned long)STATUS_COUNT(st), (unsigned)STATUS_ENQ_READY(st));

    if (STATUS_ENQ_READY(st)) {
        fprintf(stderr, "ERROR: enq_ready is still 1 after pre-fill; bailing\n");
        return 1;
    }

    /* Re-arm: write any value to PRODUCER_WAIT_REQUEST. The HW first
     * auto-clears any pending wake, then re-evaluates: if count is already
     * <= prod_low_wm, wake fires immediately; otherwise producer_waiting
     * latches and we wait for a falling crossing. */
    fprintf(stderr, "arm PRODUCER_WAIT_REQUEST\n");
    wr64(ISB_PROD_WAIT_REQ, 1);

    /* Sanity: IRQ_STATUS should show producer_waiting=1 and prod_wake=0 */
    uint32_t irqs = (uint32_t)rd64(ISB_IRQ_STATUS);
    fprintf(stderr, "irq_status after arm = 0x%x  (prod_waiting=%u prod_wake=%u)\n",
            irqs, IRQ_PROD_WAITING(irqs), IRQ_PROD_WAKE(irqs));

    /* Unmask the IRQ so the kernel actually delivers it.
     * uio_pdrv_genirq masks the IRQ after each event; you must write
     * a uint32_t 1 to the fd to re-enable. Do this BEFORE read(). */
    uint32_t unmask = 1;
    if (write(fd, &unmask, 4) != 4) {
        perror("write(unmask)");
        return 1;
    }

    /* Drain any STALE IRQ event left behind by a prior run.
     *
     * Background: uio_pdrv_genirq calls disable_irq_nosync() inside its
     * top-half. The PLIC gateway re-latches IP after complete if the line
     * was still high (which it is, because the prior test exited without
     * clearing producerWake). Lowering the line in our arm above does NOT
     * clear that latched IP bit. The very first enable_irq() will therefore
     * deliver one immediate spurious interrupt.
     *
     * We absorb that spurious event here so the real wait below is clean.
     */
    {
        struct pollfd pfd = { .fd = fd, .events = POLLIN };
        /* 50 ms grace is far more than enough on an 80 MHz Rocket */
        int pret = poll(&pfd, 1, 50);
        if (pret > 0 && (pfd.revents & POLLIN)) {
            uint32_t stale = 0;
            if (read(fd, &stale, 4) == 4) {
                fprintf(stderr, "drained stale IRQ event_count=%u "
                                "(leftover from prior run; HW is fine)\n",
                        stale);
            }
            /* Handler disabled the IRQ on its way out -- re-enable */
            if (write(fd, &unmask, 4) != 4) {
                perror("write(re-unmask)");
                return 1;
            }
        }
    }

    /* Spawn drainer that pushes count below prod_low_wm */
    int drain_n = (int)prod_high - (int)prod_low + 8;
    if (drain_n <= 0) drain_n = 1;
    struct worker_arg wa = { .drain_count = drain_n, .t0 = now_s() };
    pthread_t th;
    if (pthread_create(&th, NULL, drainer, &wa) != 0) {
        perror("pthread_create");
        return 1;
    }

    /* Block until the producer-wake IRQ fires */
    fprintf(stderr, "blocking on read()...\n");
    double t_block = now_s();
    uint32_t irq_count = 0;
    ssize_t n = read(fd, &irq_count, 4);
    double elapsed = now_s() - t_block;

    if (n != 4) {
        perror("read");
        pthread_join(th, NULL);
        return 1;
    }

    pthread_join(th, NULL);

    st   = rd64(ISB_STATUS);
    irqs = (uint32_t)rd64(ISB_IRQ_STATUS);
    fprintf(stderr,
            "unblocked!  elapsed = %.3f s  irq_count=%u\n"
            "final: count=%lu  enq_ready=%u  deq_valid=%u  irq_status=0x%x\n",
            elapsed, irq_count,
            (unsigned long)STATUS_COUNT(st),
            (unsigned)STATUS_ENQ_READY(st), (unsigned)STATUS_DEQ_VALID(st),
            irqs);

    /* PASS criterion */
    if (elapsed >= 1.5 && elapsed <= 3.0)
        fprintf(stderr, "PASS: woke at ~T+2s as expected\n");
    else if (elapsed < 0.05)
        fprintf(stderr, "WARN: woke immediately. Possible stale wake or count already <= prod_low_wm.\n");
    else
        fprintf(stderr, "WARN: elapsed outside [1.5, 3.0] s window\n");

    munmap((void *)g_mmio, 4096);
    close(fd);
    return 0;
}
