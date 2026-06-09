/*
 * test_consumer_wake.c
 *
 * Bring-up test for the CONSUMER-wake interrupt of InterruptAwareMMIOFIFO.
 *
 * What it does (mirror of test_producer_wake.c):
 *   1. Opens /dev/uio<N> (consumer side -- odd-numbered uio nodes when paired
 *      with the dual-IRQ isb_uio.ko driver: uio1, uio3, uio5, uio7) and mmaps
 *      the 4 KB MMIO window (note: producer and consumer UIOs of the same
 *      ISB map the SAME physical window; both can read/write all registers).
 *   2. Drains any stale data so count starts at 0.
 *   3. Writes 1 to CONSUMER_WAIT_REQUEST to arm the wait.
 *      Sanity-checks IRQ_STATUS shows cons_waiting=1, cons_wake=0  (= 0x200).
 *   4. Unmasks the IRQ and absorbs any stale event left over from a prior run.
 *   5. Spawns a worker thread that, after a 2-second delay, enqueues
 *      (cons_high_wm + 8) entries. Crossing cons_high_wm going UP fires
 *      consumer_wake.
 *   6. The main thread issues read(uio_fd, &irq_cnt, 4). This should block
 *      in the kernel (hart goes to WFI) until the rising crossing fires.
 *   7. Prints elapsed wall time and the IRQ count delta.
 *
 * Build (host, RISC-V cross):
 *   make
 *
 * Run on the FPGA (as root, UIO mmap usually requires it):
 *   sudo ./test_consumer_wake               # defaults to /dev/uio1
 *   sudo ./test_consumer_wake /dev/uio3
 *   sudo ./test_consumer_wake /dev/uio5
 *   sudo ./test_consumer_wake /dev/uio7
 *
 * Expected output (depth=256, default cons_high=64, cons_low=0):
 *   open  /dev/uio1 ...
 *   mmap  base=0x...
 *   watermarks: prod_high=256 prod_low=192  cons_high=64 cons_low=0
 *   drained N stale entries: count=0
 *   pre-state: count=0  deq_valid=0
 *   arm CONSUMER_WAIT_REQUEST
 *   irq_status after arm = 0x200  (cons_waiting=1 cons_wake=0)
 *   blocking on read()...
 *   [worker]  T+2.00s  enqueueing 72 entries (target count > cons_high_wm)  count=0  enq_ready=1
 *   [worker]  enqueued=72  -> count=72  deq_valid=1
 *   unblocked!  elapsed = 2.0xx s  irq_count=1
 *   final: count=72  enq_ready=1  deq_valid=1  irq_status=0x2
 *   PASS: woke at ~T+2s as expected
 *
 * Note: the consumer FSM is symmetric to the producer FSM:
 *   - producer waits while count < prod_high_wm; wake on FALLING crossing through prod_low_wm
 *   - consumer waits while count <= cons_high_wm; wake on RISING crossing through cons_high_wm
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
    int    enq_count;
    double t0;
};

static void *filler(void *arg) {
    struct worker_arg *wa = (struct worker_arg *)arg;
    /* Sleep 2 seconds so the main thread is definitely parked in read() */
    struct timespec ts = { .tv_sec = 2, .tv_nsec = 0 };
    nanosleep(&ts, NULL);

    uint64_t st = rd64(ISB_STATUS);
    fprintf(stderr, "[worker]  T+%.2fs  enqueueing %d entries "
                    "(target count > cons_high_wm)  count=%lu  enq_ready=%u\n",
            now_s() - wa->t0, wa->enq_count,
            (unsigned long)STATUS_COUNT(st), (unsigned)STATUS_ENQ_READY(st));

    int enqd = 0;
    for (int i = 0; i < wa->enq_count; i++) {
        st = rd64(ISB_STATUS);
        if (!STATUS_ENQ_READY(st)) {
            fprintf(stderr, "[worker]  enq_ready=0 after %d enqueues, count=%lu\n",
                    enqd, (unsigned long)STATUS_COUNT(st));
            break;
        }
        wr64(ISB_ENQ, (uint64_t)0xBEEF0000ULL | (uint32_t)i);
        enqd++;
    }
    st = rd64(ISB_STATUS);
    fprintf(stderr, "[worker]  enqueued=%d  -> count=%lu  deq_valid=%u\n",
            enqd, (unsigned long)STATUS_COUNT(st), (unsigned)STATUS_DEQ_VALID(st));
    return NULL;
}

int main(int argc, char **argv) {
    const char *path = (argc > 1) ? argv[1] : "/dev/uio1";

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

    /* Drain anything stale so the FIFO starts empty (count=0).
     * This is the symmetric counterpart to the producer test's pre-fill. */
    uint64_t st = rd64(ISB_STATUS);
    int pre = 0;
    while (STATUS_DEQ_VALID(st) && pre < 200000) {
        (void)rd64(ISB_DEQ);
        st = rd64(ISB_STATUS);
        pre++;
    }
    fprintf(stderr, "drained %d stale entries: count=%lu\n",
            pre, (unsigned long)STATUS_COUNT(st));

    fprintf(stderr, "pre-state: count=%lu  deq_valid=%u\n",
            (unsigned long)STATUS_COUNT(st), (unsigned)STATUS_DEQ_VALID(st));

    /* Re-arm: write any value to CONSUMER_WAIT_REQUEST. The HW first
     * auto-clears any pending wake, then re-evaluates: if count is already
     * > cons_high_wm, wake fires immediately; otherwise consumer_waiting
     * latches and we wait for a rising crossing. */
    fprintf(stderr, "arm CONSUMER_WAIT_REQUEST\n");
    wr64(ISB_CONS_WAIT_REQ, 1);

    /* Sanity: IRQ_STATUS should show consumer_waiting=1 and cons_wake=0
     * -> low byte = 0, high-nibble byte = 0x02 -> 0x200 */
    uint32_t irqs = (uint32_t)rd64(ISB_IRQ_STATUS);
    fprintf(stderr, "irq_status after arm = 0x%x  (cons_waiting=%u cons_wake=%u)\n",
            irqs, IRQ_CONS_WAITING(irqs), IRQ_CONS_WAKE(irqs));

    /* Unmask the IRQ so the kernel actually delivers it.
     * The isb_uio driver disables the IRQ in its top-half and re-enables on
     * a write of uint32_t 1 to the fd. Do this BEFORE read(). */
    uint32_t unmask = 1;
    if (write(fd, &unmask, 4) != 4) {
        perror("write(unmask)");
        return 1;
    }

    /* Drain any STALE IRQ event left behind by a prior run (same reasoning
     * as the producer test -- PLIC IP can re-latch after enable_irq). */
    {
        struct pollfd pfd = { .fd = fd, .events = POLLIN };
        int pret = poll(&pfd, 1, 50);
        if (pret > 0 && (pfd.revents & POLLIN)) {
            uint32_t stale = 0;
            if (read(fd, &stale, 4) == 4) {
                fprintf(stderr, "drained stale IRQ event_count=%u "
                                "(leftover from prior run; HW is fine)\n",
                        stale);
            }
            if (write(fd, &unmask, 4) != 4) {
                perror("write(re-unmask)");
                return 1;
            }
        }
    }

    /* Spawn filler that pushes count above cons_high_wm */
    int enq_n = (int)cons_high + 8;
    if (enq_n <= 0) enq_n = 1;
    struct worker_arg wa = { .enq_count = enq_n, .t0 = now_s() };
    pthread_t th;
    if (pthread_create(&th, NULL, filler, &wa) != 0) {
        perror("pthread_create");
        return 1;
    }

    /* Block until the consumer-wake IRQ fires */
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
        fprintf(stderr, "WARN: woke immediately. Possible stale wake or count already > cons_high_wm.\n");
    else
        fprintf(stderr, "WARN: elapsed outside [1.5, 3.0] s window\n");

    munmap((void *)g_mmio, 4096);
    close(fd);
    return 0;
}
