/**
 * rocc_diag.c - Diagnostic for RoCC ISB custom instructions (v6)
 *
 * Tests RoCC on BOTH cores:
 *   - Core 0 has ISBWriterRoCC (custom0)
 *   - Core 1 has ISBReaderRoCC (custom1)
 *
 * Tests Core 0 first (writer query, xd=1) since console stays on Core 0.
 * If Core 0 stalls, we lose console — but that's informative too.
 * Then tests Core 1 (reader query, xd=1) using fork to protect console.
 *
 * Usage: ./rocc_diag
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sched.h>
#include <sys/wait.h>
#include <setjmp.h>
#include <fcntl.h>
#include "rocc_isb.h"

static sigjmp_buf jmp_env;
static volatile sig_atomic_t got_sigill = 0;

static void sigill_handler(int sig)
{
    got_sigill = 1;
    siglongjmp(jmp_env, 1);
}

static void pin_to_core(int core_id)
{
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    if (sched_setaffinity(0, sizeof(cpuset), &cpuset) != 0) {
        perror("sched_setaffinity");
        _exit(99);
    }
    sched_yield();
}

static uint64_t read_cycle(void)
{
    uint64_t c;
    asm volatile ("rdcycle %0" : "=r"(c));
    return c;
}

/*
 * Test 1: Core 0, custom0 write query (xd=1)
 * This runs IN the main process (no fork) so output goes directly to console.
 * If it stalls, we lose the console — but that tells us Core 0 RoCC is broken.
 */
static int test_core0_writer(void)
{
    printf("--- Test 1: Core 0 ISBWriterRoCC (custom0) ---\n");
    pin_to_core(0);
    printf("  Pinned to CPU %d\n", sched_getcpu());

    /* Verify core is alive: read cycle counter */
    uint64_t c1 = read_cycle();
    uint64_t c2 = read_cycle();
    printf("  Cycle counter: %lu -> %lu (delta=%lu) OK\n", c1, c2, c2 - c1);

    /* Set up SIGILL handler */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigill_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGILL, &sa, NULL);

    /* First: isb_write xd=0 (no response needed - safest test)
     * This writes 0x42 into the ISB FIFO. Pipeline does NOT wait for response.
     * If it stalls here, cmd.ready is false -> hardware issue. */
    printf("  Testing isb_write(0x42) [custom0, funct=0, xd=0]...\n");
    printf("  (If no output after this, Core 0 pipeline is frozen)\n");
    fflush(stdout);

    got_sigill = 0;
    if (sigsetjmp(jmp_env, 1) == 0) {
        asm volatile ("fence" ::: "memory");
        isb_write(0x42);
        printf("  isb_write OK! (xd=0, no response needed)\n");
    } else {
        printf("  SIGILL on isb_write! XS might be 0.\n");
        return -1;
    }

    /* Second: isb_write_query xd=1 (response needed - tests response path too) */
    printf("  Testing isb_write_query() [custom0, funct=1, xd=1]...\n");
    printf("  (If no output after this, response path is broken)\n");
    fflush(stdout);

    got_sigill = 0;
    if (sigsetjmp(jmp_env, 1) == 0) {
        asm volatile ("fence" ::: "memory");
        uint64_t r = isb_write_query();
        printf("  isb_write_query OK! result=%lu (1=space, 0=full)\n", r);
    } else {
        printf("  SIGILL on isb_write_query!\n");
        return -1;
    }

    printf("  Core 0 ISBWriterRoCC: ALL PASSED\n\n");
    return 0;
}

/*
 * Test 2: Core 1, custom1 read query (xd=1)
 * Uses fork: child on Core 1 (might stall), parent stays on Core 0 (console).
 */
static int marker_pipe[2];

static void mark_step(const char *step)
{
    char buf[80];
    int len = snprintf(buf, sizeof(buf), "%s\n", step);
    (void)!write(marker_pipe[1], buf, len);
}

static char last_step[80];

static const char *drain_markers(void)
{
    char buf[512];
    const char *result = NULL;
    ssize_t n;
    while ((n = read(marker_pipe[0], buf, sizeof(buf) - 1)) > 0) {
        buf[n] = '\0';
        char *last_nl = strrchr(buf, '\n');
        if (last_nl) {
            *last_nl = '\0';
            char *start = strrchr(buf, '\n');
            start = start ? start + 1 : buf;
            strncpy(last_step, start, sizeof(last_step) - 1);
            last_step[sizeof(last_step) - 1] = '\0';
            result = last_step;
        }
    }
    return result;
}

static void child_test_core1(void)
{
    char buf[80];
    close(marker_pipe[0]);

    mark_step("child_started");
    pin_to_core(1);
    snprintf(buf, sizeof(buf), "pinned_cpu_%d", sched_getcpu());
    mark_step(buf);

    /* Verify core 1 alive */
    uint64_t c1, c2;
    asm volatile ("rdcycle %0" : "=r"(c1));
    asm volatile ("rdcycle %0" : "=r"(c2));
    snprintf(buf, sizeof(buf), "cycles_ok_%lu", c2 - c1);
    mark_step(buf);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigill_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGILL, &sa, NULL);
    mark_step("sigill_handler_set");

    /* Test: isb_read_query (custom1, funct=1, xd=1) */
    got_sigill = 0;
    if (sigsetjmp(jmp_env, 1) == 0) {
        mark_step("before_fence");
        asm volatile ("fence" ::: "memory");
        mark_step("after_fence_before_custom1");
        uint64_t r = isb_read_query();
        snprintf(buf, sizeof(buf), "custom1_query_OK=%lu", r);
        mark_step(buf);
    } else {
        mark_step("SIGILL_custom1_query");
        _exit(1);
    }

    mark_step("ALL_DONE");
    _exit(0);
}

static int test_core1_reader(void)
{
    printf("--- Test 2: Core 1 ISBReaderRoCC (custom1) ---\n");

    if (pipe(marker_pipe) != 0) { perror("pipe"); return -1; }

    pid_t pid = fork();
    if (pid == 0) {
        child_test_core1();
        _exit(0);
    }

    /* Parent on Core 0 monitors child on Core 1 */
    close(marker_pipe[1]);
    {
        int flags = fcntl(marker_pipe[0], F_GETFL);
        fcntl(marker_pipe[0], F_SETFL, flags | O_NONBLOCK);
    }

    int timeout_sec = 15;
    int status;
    int elapsed = 0;
    const char *step = NULL;

    while (elapsed < timeout_sec) {
        pid_t w = waitpid(pid, &status, WNOHANG);
        const char *s = drain_markers();
        if (s) step = s;

        if (w > 0) {
            s = drain_markers();
            if (s) step = s;
            printf("  [last step: %s]\n\n", step ? step : "?");
            if (WIFEXITED(status)) {
                int code = WEXITSTATUS(status);
                if (code == 0) {
                    printf("  Core 1 ISBReaderRoCC: ALL PASSED!\n");
                } else if (code == 1) {
                    printf("  VERDICT: SIGILL on Core 1\n");
                }
            } else if (WIFSIGNALED(status)) {
                printf("  Child killed by signal %d\n", WTERMSIG(status));
            }
            return 0;
        }
        sleep(1);
        elapsed++;
        printf("  [%d/%ds] step: %s\n", elapsed, timeout_sec, step ? step : "(waiting)");
    }

    /* Timeout */
    const char *s2 = drain_markers();
    if (s2) step = s2;
    printf("\n  TIMEOUT after %d seconds.\n", timeout_sec);
    printf("  Last step: %s\n\n", step ? step : "(none)");
    kill(pid, SIGKILL);
    waitpid(pid, NULL, 0);

    if (step && strcmp(step, "after_fence_before_custom1") == 0) {
        printf("  VERDICT: Core 1 STALLED on isb_read_query (custom1)\n");
        printf("  The RoCC command was dispatched but pipeline froze.\n");
        printf("  Either cmd.ready is false or the instruction is replaying.\n");
    } else if (step && strcmp(step, "before_fence") == 0) {
        printf("  VERDICT: Core 1 stalled on FENCE instruction!\n");
        printf("  This suggests a prior memory operation is blocking.\n");
    } else {
        printf("  VERDICT: Stalled at: %s\n", step ? step : "(unknown)");
    }
    return -1;
}

int main(void)
{
    setbuf(stdout, NULL);

    printf("=== RoCC ISB Diagnostic v6 ===\n");
    printf("Core 0: ISBWriterRoCC (custom0)\n");
    printf("Core 1: ISBReaderRoCC (custom1)\n\n");

    /* Show ISA and CPU info */
    FILE *cpuinfo = fopen("/proc/cpuinfo", "r");
    if (cpuinfo) {
        char line[1024];
        while (fgets(line, sizeof(line), cpuinfo)) {
            if (strncmp(line, "isa", 3) == 0) {
                printf("ISA: %s", line);
                break;
            }
        }
        fclose(cpuinfo);
    }
    printf("CPU: %d, nproc: %ld\n\n", sched_getcpu(), sysconf(_SC_NPROCESSORS_ONLN));

    /* Test Core 0 first (direct, no fork) */
    pin_to_core(0);
    int rc = test_core0_writer();
    if (rc != 0) {
        printf("Core 0 test failed. Aborting.\n");
        return 1;
    }

    /* Test Core 1 (fork-based to protect console) */
    rc = test_core1_reader();

    printf("\n=== Diagnostic Complete ===\n");
    return rc;
}
