# Chat Session

- **Assistant:** GitHub Copilot
- **Turns:** 43

---

## Turn 1
*2026-06-08 22:41:42 · copilot/claude-opus-4.7*

### 🧑 User

I have designed an ISB for passing data between cores. Im impelementing pipelined MPSoCs. The main idea is pipelining each and pin tasks to each core. The cores will use the isbs to pass data between each stage. Check the attached app for checking the software side handling of this.

The ISB contains an enqueue interface and a dequeue interface. Each interface uses a ready-valid handshake protocol and is exposed to software through MMIO registers.

 Enqueue Interface
The producer core writes data into the ISB through the enqueue interface.
The ISB generates the `ready` signal.
When free space is available in the ISB, `ready` is asserted.
The producer transfers an item by writing data and asserting `valid`.
When both `ready` and `valid` are asserted, the ISB accepts the item and stores it.

Dequeue Interface
The consumer core retrieves data from the ISB through the dequeue interface.
The ISB generates the `valid` signal.
When at least one item is available in the ISB, `valid` is asserted.
The consumer accepts an item by asserting `ready`.
When both `ready` and `valid` are asserted, the ISB removes the item from the buffer.

enq_valid,enq_data, deq_ready,deq_data and status register is connected to the MMIO interface. Status register has enq_ready,deq_valid,count in its register. Check the scala implementation of the isb and get an idea.

# Problem
With a pure ready-valid implementation, blocked producers and consumers must continuously poll the ISB state. For example:

Producer:
    while (!ready)
        poll

Consumer:
    while (!valid)
        poll

This results in:
- high dynamic power consumption,
- unnecessary instruction execution,
- low useful work performed by the cores.

# Proposed Extension
To eliminate busy-wait polling, the ISB is extended with a sleep/wakeup mechanism.

For the producer side, an additional MMIO register called: PRODUCER_WAIT_REQUEST is introduced.
This register is written by the producer when the enqueue interface becomes blocked.
The ISB maintains the following internal state:
producer_waiting
producer_low_watermark
producer_high_watermark

The `producer_high_watermark` is typically configured to the FIFO depth.(by default)
The `producer_low_watermark` determines the occupancy level at which the producer should resume execution.(We can write to this in software after mapping that to a MMIO register)

# Producer Operation
When the FIFO occupancy reaches the producer high watermark, the enqueue `ready` signal is deasserted.
The producer then:
   1. Writes PRODUCER_WAIT_REQUEST.
   2. Executes WFI.

Upon receiving the wait request, the ISB immediately evaluates the current occupancy.
### Case 1
If:occupancy <= producer_low_watermark , then ISB immediately interrupts which causes the producer to resume execution without entering a prolonged sleep state.

### Case 2
If:occupancy > producer_low_watermark, then ISB records: producer_waiting = 1 which indicates that the producer is blocked and waiting for space.

# Wakeup Operation

While the producer is sleeping, the consumer continues removing items from the ISB.

When the occupancy crosses the producer low watermark:
occupancy > low_watermark
           ↓
occupancy <= low_watermark
then ISB checks:producer_waiting
If asserted:register producer_waiting = 0, and then wakeup interrupt is then delivered to the producer.
The producer resumes execution and continues enqueueing items.

# Lost-Wakeup Analysis
With this mechanism, the producer registers its wait request before entering the sleep state.
Furthermore, the ISB immediately evaluates the occupancy when the wait request is received.
Therefore, if space already becomes available before the producer sleeps, the wakeup is generated immediately and is stored in a pending register.
As a result, the design avoids the classic lost-wakeup race condition in which a wakeup event occurs before the producer has registered its interest in sleeping.
Addtionially , the interrupt strom is also avoided.

What do you think of this design? LEts discuss before implementing.(i have renamed the earlier implementation as a text file. We will implemenet it all over again.) Ask me questions if you have further stuff to clarify before implementing. Also check whether there are any  coner cases which cause lost wake up signals or race conditions.

First analyze the isb implementation and also the apps to get an idea of the current implementatiion

### 🤖 GitHub Copilot

I've analyzed all three pieces:

1. **

** — the current pure ready/valid + count-in-status MMIO FIFO (RegFile or BRAM).
2. **`SequentialISBwithInterrupt.scala`** (from commit `b731197a`, now removed) — the earlier attempt: level-sensitive `NOT_EMPTY`/`NOT_FULL` conditions OR'd into a single PLIC line, gated by two SW-armed IE bits with static depth/4 thresholds.
3. **

 + the JPEG decoder cores** — pure busy-polling (

 / 

) in `isb_*_blocking()`, exactly the pattern you want to replace.

Before writing any Chisel, let me lay out what I think you're proposing, then call out the gaps / corner cases I see.

---

## How I read your proposal

A symmetric pair of "sleep request" registers (producer + consumer side), with an **immediate re-evaluation on write** so the producer/consumer cannot miss a wakeup between checking `ready`/`valid` and executing `WFI`. Concretely, per ISB instance:

| Register | RW | Purpose |
|---|---|---|
| `STATUS` | R | unchanged: 

 |
| `ENQ` | W | unchanged |
| `DEQ` | R | unchanged |
| `PRODUCER_LOW_WATERMARK` | RW | occupancy at which a sleeping producer is woken (default `depth/2`?) |
| `PRODUCER_HIGH_WATERMARK` | RW | (your description says "typically depth" — see Q4 below) |
| `PRODUCER_WAIT_REQUEST` | W1S | producer arms sleep here, then `WFI` |
| `CONSUMER_HIGH_WATERMARK` | RW | occupancy at which a sleeping consumer is woken |
| `CONSUMER_LOW_WATERMARK` | RW | (symmetric, see Q4) |
| `CONSUMER_WAIT_REQUEST` | W1S | consumer arms sleep here, then `WFI` |
| `IRQ_PENDING` | R/W1C | which side caused the wake (1 bit producer, 1 bit consumer) |

Internal HW state per side:
- `producer_waiting` (1 bit), `consumer_waiting` (1 bit)
- `producer_pending`, `consumer_pending` — latch fed to PLIC

State machine (producer side, symmetric for consumer):

```
on write PRODUCER_WAIT_REQUEST:
    if occupancy <= producer_low_watermark:
        producer_pending := 1         // immediate fire
        producer_waiting := 0
    else:
        producer_waiting := 1

on consumer dequeue causing occupancy transition (>low) -> (<=low):
    if producer_waiting:
        producer_waiting := 0
        producer_pending := 1

IRQ line to PLIC := producer_pending || consumer_pending
on SW W1C to IRQ_PENDING bits: clear the corresponding *_pending
```

This is essentially a classic "armed-edge-with-immediate-recheck" wakeup — it does solve the lost-wakeup race in the canonical form (the **arm-then-check** sequence is inverted into a HW-side **arm-and-recheck-atomically**).

---

## Corner cases / things I want to nail down before coding

### A. Where the pending bit gets cleared
Your description does not say. Three options; I'd recommend **(c)**:
- (a) HW pulses the line one cycle → PLIC gateway latches → fragile for Linux S-mode + WFI window.
- (b) Level-sensitive on `(occupancy <= low) && producer_waiting` — re-fires forever if the producer doesn't take the wake fast enough or if it momentarily refills.
- (c) **Latched `*_pending` bit, level-sensitive output, SW W1C** — clean, idempotent, no spurious re-fires, plays nicely with PLIC + Linux GIC-style handlers + bare-metal `mip.MEIP`.

### B. PLIC line count per ISB
The prior attempt OR'd producer + consumer onto **one** line because `uio_pdrv_genirq` only consumes the first IRQ. With your scheme it's cleaner to expose **two separate `IntSourceNode` lines per ISB** (one producer-wake, one consumer-wake) so on Linux you can register two UIO instances or a single driver with two IRQs; on bare-metal you get unambiguous routing. Confirm?

### C. Lost-wakeup edge I want to verify with you
The scenario: producer sees `enq.ready==0` in SW, then before the producer issues the `PRODUCER_WAIT_REQUEST` write, the consumer drains **the entire FIFO**. With your design, the write triggers immediate re-evaluation, sees `occupancy <= low_watermark`, latches `producer_pending`, and `WFI` returns immediately. ✓ safe — **as long as the `occupancy <= low_watermark` test on the `WAIT_REQUEST` write uses the current-cycle occupancy, not a stale registered copy.** I'll make sure the comparator combinationally reads the FIFO `count` in the same cycle as the TL write fires.

### D. WFI semantics on Rocket
Rocket's `WFI` wakes on any pending bit in `mip`/`sip` regardless of `mstatus.MIE`. So even if the producer's ISR window is closed when the IRQ arrives, `WFI` still returns. The hazard is only the *narrower* one inside the PLIC gateway, which (c) avoids because the line stays high until SW W1C. ✓

### E. Race when producer arms while it is *already* waiting (double-arm)
SW shouldn't do this, but if `PRODUCER_WAIT_REQUEST` is written twice without an intervening wake, the second write re-runs the immediate check. If the FIFO is still full it just leaves `producer_waiting=1`; harmless. Fine.

### F. Race when crossing happens in the same cycle as `WAIT_REQUEST` write
Consumer dequeues in cycle N taking occupancy from `low+1 → low`, and the TL write of `WAIT_REQUEST` also lands in cycle N. With (c), the priority is "immediate check wins" — the comparator sees `count == low` ⇒ fires immediately, `producer_waiting` stays 0. No double-latch, no miss. ✓

### G. Watermark crossing detection
I'll implement it as `RegNext(occupancy <= low_watermark)` falling-vs-rising edge OR equivalently fire when `(occupancy <= low_watermark) && producer_waiting`. The latter is simpler and correct given (c)'s SW-W1C — once latched, `producer_waiting:=0` so it cannot re-fire on the same wait-cycle.

### H. The producer's `PRODUCER_HIGH_WATERMARK`
You say it's "typically configured to the FIFO depth (default)." But the FIFO already deasserts `enq.ready` at `count == depth`. So a producer-side high watermark only matters if you want the producer to **proactively** sleep earlier (e.g., at `depth - 8`) to avoid the next-iteration backpressure stall. Two valid interpretations:
- (H1) Pure wakeup mechanism: producer high watermark is **unused**; producer arms only after it observes `enq.ready==0`. The HW only needs `producer_low_watermark`.
- (H2) Proactive throttling: HW deasserts `enq.ready` at `count >= producer_high_watermark`, so the producer sleeps before truly full. This is a behavior change.

Which do you want? I'd default to **(H1)** for minimal surprise; (H2) is a knob we can add later.

### I. Consumer side
Symmetric. `CONSUMER_HIGH_WATERMARK` is the "wake the consumer when ≥ this many items accumulate" threshold (so the consumer wakes for a batch, not for every single item). Default `1`? Or `max(1, depth/4)` for hysteresis like the prior design? I lean **`1` as default, SW-writable** so the JPEG decoder cores can tune per-stage.

### J. Reset and boot
All `*_waiting`, `*_pending`, `*_IE` (if we still keep IE bits — see K) reset to 0, watermarks reset to safe defaults (producer_low = depth/2, consumer_high = 1). PLIC line is held low through boot — no spurious early IRQ.

### K. Do we still want IE bits?
With the "arm-and-recheck" model, IE is implicit: writing `*_WAIT_REQUEST` arms; the pending bit acts as the "armed" state. We don't need an explicit `IE` register. Simpler. Agreed?

---

## Questions for you (please pick one per item)

1. **Pending/clear semantics** — go with (c) latched `IRQ_PENDING` + SW W1C? (recommended)
2. **PLIC routing** — expose **two PLIC lines per ISB** (producer-wake, consumer-wake) instead of one OR'd line? (recommended)
3. **Producer high watermark** — (H1) unused / no extra throttling, or (H2) HW gates `enq.ready` early?
4. **Default watermarks** — producer_low = `depth/2`, consumer_high = `1`? Or something else?
5. **Drop the IE register** and let `WAIT_REQUEST` be the sole "arm" signal? (recommended)
6. **Target software** — Linux (UIO + `WFI` in idle), bare-metal, or both? The prior attempt targeted Linux; I want to confirm the new app uses one or both.
7. **Per-core IRQ routing** — keep current `ibus.fromSync` (broadcast through PLIC, SW configures affinity), or wire each ISB's two IRQs to specific tile IDs as parameters? Most flexible is broadcast + SW affinity; most deterministic is HW-bound.

Once you answer these I'll draft `SequentialISBwithInterrupt.scala`, the matching `WithSequentialISBWithInterrupt` config fragment, and the user-space `isb_lib` extensions (`isb_wait_producer()`, `isb_wait_consumer()`, IRQ-clear helper).

---

## Turn 2
*2026-06-08 22:49:20 · copilot/claude-opus-4.7*

### 🧑 User

i dont know where you get producer_pending. It does not have in the description:

Below is the problem decription.

I have designed an ISB for passing data between cores. Im impelementing pipelined MPSoCs. The main idea is pipelining each and pin tasks to each core. The cores will use the isbs to pass data between each stage. Check the attached app for checking the software side handling of this.

The ISB contains an enqueue interface and a dequeue interface. Each interface uses a ready-valid handshake protocol and is exposed to software through MMIO registers.

Enqueue Interface
The producer core writes data into the ISB through the enqueue interface.
The ISB generates the ready signal.
When free space is available in the ISB, ready is asserted.
The producer transfers an item by writing data and asserting valid.
When both ready and valid are asserted, the ISB accepts the item and stores it.

Dequeue Interface
The consumer core retrieves data from the ISB through the dequeue interface.
The ISB generates the valid signal.
When at least one item is available in the ISB, valid is asserted.
The consumer accepts an item by asserting ready.
When both ready and valid are asserted, the ISB removes the item from the buffer.

enq_valid,enq_data, deq_ready,deq_data and status register is connected to the MMIO interface. Status register has enq_ready,deq_valid,count in its register. Check the scala implementation of the isb and get an idea.

Problem
With a pure ready-valid implementation, blocked producers and consumers must continuously poll the ISB state. For example:

Producer:
while (!ready)
poll

Consumer:
while (!valid)
poll

This results in:

high dynamic power consumption,
unnecessary instruction execution,
low useful work performed by the cores.
Proposed Extension
To eliminate busy-wait polling, the ISB is extended with a sleep/wakeup mechanism.

For the producer side, an additional MMIO register called: PRODUCER_WAIT_REQUEST is introduced.
This register is written by the producer when the enqueue interface becomes blocked.
The ISB maintains the following internal state:
producer_waiting
producer_low_watermark
producer_high_watermark

The producer_high_watermark is typically configured to the FIFO depth.(by default)
The producer_low_watermark determines the occupancy level at which the producer should resume execution.(We can write to this in software after mapping that to a MMIO register)

Producer Operation
When the FIFO occupancy reaches the producer high watermark, the enqueue ready signal is deasserted.
The producer then:

Writes PRODUCER_WAIT_REQUEST.
Executes WFI.
Upon receiving the wait request, the ISB immediately evaluates the current occupancy.

Case 1
If:occupancy <= producer_low_watermark , then ISB immediately interrupts which causes the producer to resume execution without entering a prolonged sleep state.

Case 2
If:occupancy > producer_low_watermark, then ISB records: producer_waiting = 1 which indicates that the producer is blocked and waiting for space.

Wakeup Operation
While the producer is sleeping, the consumer continues removing items from the ISB.

When the occupancy crosses the producer low watermark:
occupancy > low_watermark
↓
occupancy <= low_watermark
then ISB checks:producer_waiting
If asserted:register producer_waiting = 0, and then wakeup interrupt is then delivered to the producer.
The producer resumes execution and continues enqueueing items.

Lost-Wakeup Analysis
With this mechanism, the producer registers its wait request before entering the sleep state.
Furthermore, the ISB immediately evaluates the occupancy when the wait request is received.
Therefore, if space already becomes available before the producer sleeps, the wakeup is generated immediately and is stored in a pending register.
As a result, the design avoids the classic lost-wakeup race condition in which a wakeup event occurs before the producer has registered its interest in sleeping.
Addtionially , the interrupt strom is also avoided.

What do you think of this design? LEts discuss before implementing.(i have renamed the earlier implementation as a text file. We will implemenet it all over again.) Ask me questions if you have further stuff to clarify before implementing. Also check whether there are any coner cases which cause lost wake up signals or race conditions.

First analyze the isb implementation and also the apps to get an idea of the current implementatiion

Can you point out the producer_pending here so that i can answer your questions

### 🤖 GitHub Copilot

You're right — `producer_pending` is **my term**, not yours. I introduced it as an implementation detail. Let me show exactly where, and reconcile it with your description.

## Where I introduced `producer_pending`

In my proposed register table:
> `IRQ_PENDING` | R/W1C | which side caused the wake (1 bit producer, 1 bit consumer)

And in the state machine:
```
on write PRODUCER_WAIT_REQUEST:
    if occupancy <= producer_low_watermark:
        producer_pending := 1         // ← my addition
        producer_waiting := 0
    ...
on consumer dequeue causing occupancy transition:
    if producer_waiting:
        producer_waiting := 0
        producer_pending := 1         // ← my addition

IRQ line to PLIC := producer_pending || consumer_pending
```

## Why I introduced it (and where it maps to your description)

Your description says:

> *"if space already becomes available before the producer sleeps, the wakeup is generated immediately and is **stored in a pending register**."*

That phrase — **"stored in a pending register"** — is what I named `producer_pending`. So conceptually it **is** in your description; I just gave it a name.

But there is a real design choice hiding here. Your description mentions a "pending register" only for **Case 1** (immediate fire on `WAIT_REQUEST` write). It does **not** say whether the wakeup in the **Wakeup Operation section** (the later crossing event) also goes through that same pending register or fires the IRQ line directly for one cycle.

So my `producer_pending` is really asking: **do we use one persistent latched bit for both paths (immediate-fire and crossing-fire), or two different mechanisms?**

The three concrete options:

| Option | How the IRQ line behaves | Who clears it |
|---|---|---|
| **A. Pulse** | HW asserts the line for 1 cycle when wakeup fires (either path). | Nothing — PLIC gateway latches the edge. |
| **B. Level on `(occupancy <= low_watermark) && producer_waiting`** | Line is high whenever the wake condition is true and producer is armed. | Auto-clears when `producer_waiting` goes to 0 (i.e., when producer writes a new `WAIT_REQUEST` or SW clears `waiting`). |
| **C. Latched `producer_pending` bit, level-sensitive output, SW W1C** *(what I proposed)* | Line stays high from wakeup until SW writes a "clear" register. | Software writes `IRQ_PENDING` with 1 to clear. |

Your description is **ambiguous between A and C**. The phrase "stored in a pending register" leans toward C, but A is also valid if "pending register" means "the PLIC gateway's pending bit."

**Why I prefer C:** with Rocket + PLIC + Linux, option A has a narrow race where the 1-cycle pulse can be missed if the PLIC gateway is between clock-domain crossings or if the IRQ is masked at the exact wrong instant. Option C is bulletproof and matches how almost every real peripheral (UART, GPIO, NIC) exposes its IRQ status.

---

## So my real question (rephrased without the name)

> When the wakeup fires (either immediate-on-`WAIT_REQUEST` or later-on-crossing), should the ISB:
> - **(A)** pulse the PLIC line for 1 cycle and trust the PLIC gateway, or
> - **(C)** latch a "wake-pending" bit, drive the PLIC line high until software writes a clear register?

The other questions from my previous message stand:
- **2.** Two PLIC lines per ISB (producer + consumer) vs one OR'd line?
- **3.** Does `producer_high_watermark` actually gate `enq.ready` early (H2), or is it unused except as "the FIFO is full" (H1)?
- **4.** Default values for the watermarks?
- **6.** Target SW: Linux UIO, bare-metal, or both?
- **7.** PLIC broadcast (any core) vs HW-bound to a specific tile?

(I dropped my old Q1 since you've now seen what `producer_pending` was; and Q5 "drop the IE register" follows automatically — your description has no IE register, so we don't add one.)

---

## Turn 3
*2026-06-08 22:52:28 · copilot/claude-opus-4.7*

### 🧑 User

"if space already becomes available before the producer sleeps, the wakeup is generated immediately and is stored in a pending register."

 means that if the space already available , the interrupt should be fired immediately and register producer_waiting = 0.

 Is that clear now? Can you again ask your questions again now that its cleared?

### 🤖 GitHub Copilot

Got it — thanks for clarifying. So in your model there is **no separate pending latch**. The state is just:

- `producer_waiting` (1 bit): "the producer asked to sleep and the FIFO didn't have space yet."
- The "wakeup" is an interrupt event that fires the moment one of these happens:
  - HW receives a `PRODUCER_WAIT_REQUEST` write **and** `occupancy <= producer_low_watermark` (immediate path — `producer_waiting` stays 0), **or**
  - HW observes a dequeue that takes occupancy from `> low_watermark` to `<= low_watermark` while `producer_waiting == 1` (later path — `producer_waiting` is cleared to 0).

That's much cleaner. One remaining detail I need from you, then I think we're done discussing:

---

## Q1 — How does the IRQ line behave electrically?

The PLIC sits between the ISB and the core. It needs to *capture* the wake event somehow. Two ways to drive the line:

- **(A) Pulse for 1 cycle when wake fires.** The PLIC gateway latches the rising edge into its own pending bit; the core's ISR reads PLIC `claim`, that clears the gateway. The ISB itself stays stateless (only `producer_waiting`).
  - Pro: matches your description literally — no extra HW state.
  - Con: pulse-based IRQs through Rocket's PLIC have historically been fragile; level-sensitive is the documented/recommended style. If the pulse and a PLIC enable change collide, the edge can be lost.

- **(B) Hold the line high until `producer_waiting` becomes 1 again (next sleep request) or SW writes a "clear" register.** Effectively, the IRQ output is registered for one extra cycle or until acknowledged.
  - Pro: robust, matches how PLIC expects level-sensitive sources.
  - Con: needs one extra register bit per side (the thing I was calling `producer_pending`), and one MMIO clear bit.

You explicitly said you want **no extra pending register** — so option **(A) pulse** is what your description specifies. I'll go with (A) **unless you want me to fall back to (B) for PLIC robustness.** Which one?

---

## Q2 — One IRQ line per ISB, or two?

Your description has independent producer and consumer wakeup paths. Two valid wirings:

- **(2a) Two PLIC lines per ISB**: `producer_wake_irq` and `consumer_wake_irq`. Each core's ISR knows unambiguously which side woke it.
- **(2b) One OR'd PLIC line per ISB**: the ISR reads the status register to figure out which side fired.

(2a) is cleaner; (2b) is what the prior implementation did because `uio_pdrv_genirq` only consumes the first IRQ. Bare-metal can use either.

---

## Q3 — Producer high watermark semantics

You wrote: *"When the FIFO occupancy reaches the producer high watermark, the enqueue `ready` signal is deasserted."*

Two readings:
- **(3a)** Literal: HW gates `enq.ready := !fullReg && (count < producer_high_watermark)`. Setting `producer_high_watermark < depth` lets the producer back off early.
- **(3b)** Loose: "high watermark = depth", so `enq.ready` deassertion is just the existing full condition; the high-watermark register is informational/SW-side only.

(3a) is more powerful and matches your wording literally. Confirm?

---

## Q4 — Default watermarks?

- `producer_high_watermark` default = `depth` (so backward compatible: same as current `!full`)
- `producer_low_watermark` default = `depth / 2`
- `consumer_high_watermark` default = `1` (wake as soon as any data arrives)
- `consumer_low_watermark` default = `0` (unused for consumer side, but symmetric)

Or do you want different defaults?

---

## Q5 — Target software for the new app?

Linux/UIO + idle `WFI`, or bare-metal `WFI`, or both? The prior implementation targeted Linux. The JPEG decoder app you attached is Linux user-space (

 + `pthread`-style core pinning), so I assume Linux is at least one target.

---

## Q6 — PLIC routing per core

Should each ISB's IRQ(s) be:
- **(6a)** broadcast (route through PLIC, any core can claim, SW configures which hart's `mie` is enabled), or
- **(6b)** statically bound at elaboration time to specific tile IDs (producer IRQ → tile N, consumer IRQ → tile M)?

(6a) is what the prior design did and is more flexible.

---

Just answer Q1–Q6 and I'll implement.

---

## Turn 4
*2026-06-08 23:08:01 · copilot/claude-opus-4.7*

### 🧑 User

Q1. I need PLIC robustness. Lets choose B. You can use addtional state managing registers.
Q2. I would like to propose two lines per ISB. This will enable the isb to wake up both consumer and producer independently. I hope this will work with linux without any issues. Tell me about it.
Q3. producer_high_watermark will 99% of the time would be the depth of the isb. However, i want the software developer to set this arbitrary. Therefore lets expose this register using MMIO. Software developer will use this when initializing the isb. If it is not set, use the defualt values. which are depth for producer_high_watermark and 0 for consumer_low_watermak.
Q4. use below:
producer_high_watermark default = depth
producer_low_watermark default = depth / 4 * 3
consumer_high_watermark default = depth / 4 
consumer_low_watermark default = 0 

These defualts can be overwritten using the exposed mmio registers.

Q5. We would be using Linux and not bare metal.

Q6. Which is the easiest to implement? Which one is linux compatible. Which one gives more performance and how much?

Also ask additonal questions regarding mmio regmap or other stuff if you have. Lets finalize the implementation before impelementing

### 🤖 GitHub Copilot

Good answers. Let me address Q2 and Q6 carefully, then ask the remaining questions.

---

## Q2 follow-up — two PLIC lines on Linux

**Yes, it works**, but with one caveat about the userspace driver.

- **Device tree / PLIC**: trivial. A platform device can declare `interrupts = <X Y>;` (or `interrupts-extended`) and the PLIC binds both. Each line gets its own hwirq.
- **Kernel side**: standard — `platform_get_irq(pdev, 0)` and `platform_get_irq(pdev, 1)` return both.
- **The catch — `uio_pdrv_genirq`**: this generic driver only handles **the first IRQ** of the device. Same limitation the prior implementation hit.

Three ways to expose two IRQs to user-space, in increasing complexity:

1. **Two DT nodes per ISB, same MMIO, one IRQ each.** Hacky overlay but it works with stock `uio_pdrv_genirq` — you get 

 (producer) and 

 (consumer). Cleanest from the SW side.
2. **One DT node, two IRQs, small custom UIO kernel module (~80 LOC).** Each IRQ gets its own UIO file descriptor. This is what most peripherals with multi-IRQ UIO needs end up doing.
3. **Single OR'd IRQ + status-register disambiguation.** Reverts to the prior design's pattern; loses the independence benefit.

Recommendation: go with **(2)** — write a tiny custom `isb_uio` module. I can scaffold it. But for the **first** bring-up we can use **(1)** and only add the module once correctness is proven. Pick one to target now?

---

## Q6 — PLIC routing

| Option | Easiest? | Linux compatible? | Performance |
|---|---|---|---|
| **(6a) Broadcast through PLIC** | **Trivial** — one line of Chisel: `ibus.fromSync := isb.intnode`. | **Yes — standard.** Linux configures hart routing dynamically via `/proc/irq/<N>/smp_affinity`. | PLIC adds ~5–15 cycles of IRQ delivery latency. |
| **(6b) Static HW binding to tile IDs** | Hard — need to look up specific tiles, build per-tile IntSinkNodes, bypass the normal `ibus`. | Awkward — DT/PLIC convention assumes the PLIC routes everything. Linux *can* deal with it, but you fight the framework. | Saves maybe 5–10 cycles per IRQ. **Negligible** vs. the ~hundreds of cycles a WFI wake-up + ISR + scheduler hop already costs. |

**Recommendation: (6a) broadcast.** It's the only sensible choice for Linux, it's what the prior implementation did, and the performance "loss" is in the noise compared to a Linux ISR round-trip. Confirm?

---

## Confirmed defaults (Q3 + Q4 summary)

```
producer_high_watermark : default = depth          (SW writable)
producer_low_watermark  : default = depth * 3 / 4  (SW writable)
consumer_high_watermark : default = depth / 4      (SW writable)
consumer_low_watermark  : default = 0              (SW writable)
```

Important sanity rules I'll enforce in HW (since SW can write anything):
- `producer_high_watermark` saturates at `depth` (writes > depth clamp).
- `producer_low_watermark < producer_high_watermark` is **not** enforced — if SW sets them equal, the producer will wake immediately every time. That's their problem.
- Same for consumer.

OK with this?

---

## Remaining questions

### Q7 — IRQ clear mechanism (option B from Q1)

Since we picked **B (level-sensitive, latched)**, we need a way for SW to acknowledge. Two flavors:

- **(7a) Auto-clear when producer writes the next `PRODUCER_WAIT_REQUEST`.** The latch is implicitly cleared by re-arming. No explicit clear register needed.
- **(7b) Explicit `IRQ_ACK` register (W1C).** SW must write 1 to bit 0 (producer) or bit 1 (consumer) inside the ISR.

(7b) is more conventional and lets SW read the IRQ cause robustly. (7a) is simpler but couples the wake protocol with the clear.

I lean **(7b)** because it's the standard pattern and the ISR needs *some* MMIO write anyway to keep the PLIC happy.

### Q8 — Live watermark writes

If SW writes `producer_low_watermark` while `producer_waiting == 1`, should HW immediately re-evaluate? E.g., SW lowers the threshold to wake earlier.

- **(8a)** Comparators are purely combinational on current registers — yes, re-evaluation happens automatically on the next dequeue. (Simpler — what I'd do by default.)
- **(8b)** Also fire wake-up immediately on the write itself if condition becomes true. (Edge case, probably unneeded.)

I'd go **(8a)**.

### Q9 — Register map layout

Proposed (32-bit registers, beat-aligned for TileLink beatBytes=8):

| Offset | Name | RW | Notes |
|---|---|---|---|
| `0x00` | `STATUS` | R | unchanged: 

 |
| `0x08` | `ENQ` | W | data, `params.width` bits |
| `0x10` | `DEQ` | R | data, `params.width` bits |
| `0x18` | `PRODUCER_WAIT_REQUEST` | W | any write arms producer sleep |
| `0x20` | `CONSUMER_WAIT_REQUEST` | W | any write arms consumer sleep |
| `0x28` | `IRQ_STATUS` | R/W1C | bit0=producer_wake, bit1=consumer_wake (also shows `producer_waiting`, `consumer_waiting` on read in upper bits?) |
| `0x30` | `PRODUCER_HIGH_WATERMARK` | RW | default `depth` |
| `0x38` | `PRODUCER_LOW_WATERMARK` | RW | default `depth*3/4` |
| `0x40` | `CONSUMER_HIGH_WATERMARK` | RW | default `depth/4` |
| `0x48` | `CONSUMER_LOW_WATERMARK` | RW | default `0` |

Watermark registers will be `log2Ceil(depth+1)` bits wide internally but exposed as 32-bit MMIO.

Looks OK? Any reordering / additions you want?

### Q10 — Should `producer_waiting` and `consumer_waiting` be SW-readable?

Useful for debugging. I'd expose them inside `IRQ_STATUS` (e.g., bits 8 and 9). Yes/no?

### Q11 — File naming and config name

- New file: `generators/chipyard/src/main/scala/example/SequentialISBwithInterrupt.scala`
- Config fragment: `WithSequentialISBWithInterrupt(...)` (keeping the prior name) or rename to something cleaner like `WithSleepableISB(...)`?

---

Answer **2 (driver path)**, **7 (ack)**, **9 (regmap OK or changes)**, **10 (expose waiting bits)**, **11 (naming)** — then I implement.

---

## Turn 5
*2026-06-08 23:31:13 · copilot/claude-opus-4.7*

### 🧑 User

I will answer below first then move onto Q2 follow up.

Q6. lets stick with 6a broadcasting impelementation
Q3+Q4. For the below:
producer_low_watermark < producer_high_watermark is not enforced — if SW sets them equal, the producer will wake immediately every time. That's their problem.

Actaully you should check this. This high value should always be higher than low watermark. Add this check.
Q7. Do 7a. I dont want to write additional mmio operations, it will make more traffic in the bus. Auto reset when pushing the next producer wait request.

Q8. Lets skip live watermak for now. The water marks are set at the initialization of the isb device on SW. in the middle of the software process, there will not be any changes to the watermarks. But you can do 8a. if it is not much of a hassle and easilty implementable.

Q9. status is fine.
there shoudl be two registers enq_data , enq_valid and deq_data, deq_ready. I dont see them listed.
for irq_status, add waiting bits to as well.

I dont understand the R/W1C, RW values. 

others looks okay.

Q10. yes please. 

Q11. SHould be okay.

Q2 follow up:

if we use One OR'd PLIC line per ISB: the ISR reads the status register to figure out which side fired, then will it cause any issues? WIll it cause any race conditions errors etc? Tell me the answer to this.

You can ask follow up questions. Also answer the questions that i have asked.

### 🤖 GitHub Copilot

Good. Answers first, then the open items.

---

## Answer: register access types (R, W, RW, W1C)

These are standard MMIO conventions used in `RegField`:

- **R** — read-only. Writes ignored (or fault, depending on bus). Used for `STATUS`.
- **W** — write-only. Reads return 0 / undefined. Used for `ENQ` data input.
- **RW** — read-write. SW writes a value, SW can read back the same value. Used for watermark configuration registers.
- **W1C** — "write-1-to-clear". For each bit, writing `1` clears that bit; writing `0` leaves it alone. Standard for IRQ-status registers so that two ISRs racing on different bits don't clobber each other.

Since you picked **7a (auto-clear on next WAIT_REQUEST)**, we don't need W1C at all — the IRQ status bits will just be **R** (read-only, HW clears them).

---

## Answer: enq/deq register layout

You're right that I was sloppy. Let me explain how the current SequentialISB actually works (you don't need separate `valid`/`ready` registers):

- **`ENQ` (write-only)**: a single MMIO address. When SW writes to it, the TileLink write strobe automatically asserts `enq.valid` for that cycle, and the FIFO's `enq.ready` is the back-pressure: if `ready==0`, the TileLink transaction stalls (or completes silently — see Q12 below). SW doesn't write a separate `valid` bit.
- **`DEQ` (read-only)**: a single MMIO address. When SW reads it, the TileLink read strobe asserts `deq.ready` for that cycle, and the FIFO's `deq.valid` is the data-availability signal: if `valid==0`, the read returns stale data (or stalls).
- **`enq.ready` and `deq.valid`** are exposed in `STATUS` so SW can poll them.

This is exactly how the current 

 works — look at the regmap:
```scala
node.regmap(
  0x00 -> Seq(RegField.r(32, status)),
  enqOffset -> Seq(RegField.w(params.width, enq)),  // write triggers enq.valid
  deqOffset -> Seq(RegField.r(params.width, deq))   // read triggers deq.ready
)
```

So we keep the same scheme. No separate valid/ready MMIO bits.

---

## Q2 follow-up — One OR'd line: any issues?

**No race conditions, no correctness issues**, but there are functional/efficiency tradeoffs:

### Correctness — safe
The ISR reads `IRQ_STATUS`, sees which bits are set, dispatches to the producer handler or consumer handler accordingly. Even if both fire in the same cycle, the ISR processes both. Standard pattern (used by every shared-IRQ peripheral).

### Issues to be aware of

1. **Spurious wake-up on the "other" core.** If producer and consumer are on different harts, both harts' PLICs see the same line. Whichever hart claims first runs the ISR; the other hart's PLIC sees the line go low and ignores it. But if both are enabled, the kernel may schedule both ISRs — one of them will read `IRQ_STATUS` and find "nothing for me," do nothing, return. Extra wake-up that doesn't help. With `smp_affinity` you steer the IRQ to one hart.

2. **Wrong hart wakes from WFI.** If the producer is on hart 0 sleeping in WFI, and the consumer-wake bit fires, hart 0 wakes (since the OR'd line is high), runs the ISR, finds "not my bit", and the producer goes back to WFI. Wasted wake-up. With two lines, hart 0 only wakes for producer events.

3. **More complex ISR.** SW must read status, mask, and dispatch. Not hard, but it's code.

4. **UIO** — with one line, stock `uio_pdrv_genirq` works out of the box. With two lines, you need either two DT nodes or a custom kernel module.

### Performance comparison

For Linux + UIO + 5-core JPEG pipeline, the **single OR'd line** is honestly fine. The wasted wake-ups (#1, #2) happen only when both producer and consumer are sleeping simultaneously, which by construction is rare in a well-tuned pipeline (if both are sleeping, the pipeline has stalled — you have a bigger problem).

**My recommendation:** start with **one OR'd line per ISB** using stock `uio_pdrv_genirq`. It's the simplest path to a working Linux app. We can upgrade to two lines + custom UIO module later if measurements show the wasted wake-ups matter.

Your call. Confirm OR'd-line approach, or stick with two-lines-plus-custom-module?

---

## Confirmed so far

- Q1: **B** — level-sensitive, latched IRQ with HW state
- Q3+Q4: defaults as listed, **HW saturation `low < high`** (clamped at elaboration if param invalid; for SW-written values, HW silently clamps writes to keep invariant)
- Q6: **6a broadcast** via `ibus.fromSync`
- Q7: **7a auto-clear** on next `WAIT_REQUEST` write (so IRQ status bits are R, not W1C)
- Q8: **8a** — combinational on registers, automatic re-eval (easy, basically free)
- Q9: regmap with R/RW correctly applied (see below)
- Q10: expose `producer_waiting`, `consumer_waiting` in `IRQ_STATUS`
- Q11: keep file/config name

---

## Revised register map

| Offset | Name | Access | Bits | Notes |
|---|---|---|---|---|
| `0x00` | `STATUS` | R | 32 | 

 |
| `0x08` | `ENQ` | W | `params.width` | write triggers enq |
| `0x10` | `DEQ` | R | `params.width` | read triggers deq |
| `0x18` | `PRODUCER_WAIT_REQUEST` | W | 1 | any write arms producer sleep + immediate re-eval; also auto-clears producer_wake bit |
| `0x20` | `CONSUMER_WAIT_REQUEST` | W | 1 | mirror for consumer |
| `0x28` | `IRQ_STATUS` | R | 32 | bit0=producer_wake, bit1=consumer_wake, bit8=producer_waiting, bit9=consumer_waiting |
| `0x30` | `PRODUCER_HIGH_WATERMARK` | RW | `log2Ceil(depth+1)` | default `depth`; HW clamps writes to ≤ depth and to > producer_low |
| `0x38` | `PRODUCER_LOW_WATERMARK` | RW | `log2Ceil(depth+1)` | default `depth*3/4`; HW clamps writes to < producer_high |
| `0x40` | `CONSUMER_HIGH_WATERMARK` | RW | `log2Ceil(depth+1)` | default `depth/4`; HW clamps writes to > consumer_low |
| `0x48` | `CONSUMER_LOW_WATERMARK` | RW | `log2Ceil(depth+1)` | default `0`; HW clamps writes to < consumer_high |

---

## On the watermark ordering enforcement

You said: *"This high value should always be higher than low watermark. Add this check."*

A subtle point: do you want HW to **silently clamp** invalid writes, or **reject** them? Three options:

- **(a) Silently clamp**: SW writes `low=200` while `high=150`. HW stores `low = high-1 = 149`. SW can read back to verify. Robust, never breaks invariant.
- **(b) Ignore invalid writes**: HW drops the write. SW read-back shows old value. SW notices and corrects.
- **(c) Elaboration-time only**: Only check defaults at Chisel elaboration; trust SW at runtime.

(a) is what I'd do — safest, no extra MMIO traffic. OK?

---

## Q12 — TileLink stall on full FIFO write?

Right now, in 

, if SW writes `ENQ` when `enq.ready==0`, what happens? Looking at the existing code, it uses `RegField.w(width, enq)` with a `DecoupledIO`. The TileLink response will stall until the FIFO accepts the write, which **blocks the bus**. This is actually a problem with the current design — a misbehaving producer can hang the bus.

With the sleep mechanism, SW should always check `enq_ready` (via STATUS) before writing `ENQ`. The new flow is:

```
if (status.enq_ready) {
    write ENQ;
} else {
    write PRODUCER_WAIT_REQUEST;
    WFI;
}
```

So in practice this isn't a regression. Just confirming: keep the same blocking-write behavior? (Yes is the obvious answer; just flagging.)

---

## Q13 — IRQ-status read clearing?

A common pattern is "reading IRQ_STATUS clears the wake bits." This is convenient but adds a side-effect to reads. Since you said 7a (auto-clear on next WAIT_REQUEST), I'll **not** clear on read — status reflects the latched wake state until the next `WAIT_REQUEST` arms a new cycle. Confirm?

---

## Q14 — 

 width consistency

`STATUS` currently packs 

 into 30 bits (bits 2-31). The watermark registers will be `log2Ceil(depth+1)` bits. For a depth=256 ISB, that's 9 bits — well within range. Just confirming we don't need to expand `STATUS` width.

---

## Final summary

Please answer:
- **Q2** — One OR'd line (easy, recommended) or two separate lines (custom UIO module needed)?
- **Q12** — Confirm: keep blocking ENQ write on full?
- **Q13** — Confirm: no clear-on-read for IRQ_STATUS?
- **Watermark clamping** — silently clamp on invalid writes (option a)?

Then I implement.

---

## Turn 6
*2026-06-08 23:48:08 · copilot/claude-opus-4.7*

### 🧑 User

When SW writes to PRODUCER_WAIT_REQUEST, CONSUMER_WAIT_REQUEST, the TileLink write strobe automatically checks for those values i think. Think about that if it needed.

Q2 follow up
both consumer and producer can sleep at the same time. Consider a pipeline with 4 cores. If 4th core is the bottleneck, producer consumer pair in core 1 and 2 can sleep at the same time. Therefore, this can happen. Re-evalue this option of ORing, considering this scenario.

on the watermark orderin enforcement: assueme c , elaboration time only.

q12. yes keep it. Dont need to look for these. I only want you to think about if the current one is working with interrupt mechanism.

q13. confirm

q14 confirm.

I think all the questions are solved and only q2 to be answered. Lets disucss about it and finalize then lets impelement

### 🤖 GitHub Copilot

Let me reconsider Q2 with your bottleneck scenario, and then address the TileLink write-strobe point you raised.

---

## Re-evaluation: 4-core pipeline with bottleneck

You're right. Let me trace it carefully:

```
Core 1 → ISB_A → Core 2 → ISB_B → Core 3 → ISB_C → Core 4 (bottleneck)
```

If Core 4 is slow:
- ISB_C fills → Core 3 (producer of C) sleeps
- ISB_B fills (because Core 3 stops consuming) → Core 2 (producer of B) sleeps  
- Core 3 is also the **consumer of ISB_B**, and it's asleep waiting on a different ISB

So **both sides of ISB_B are sleeping simultaneously** — Core 2 (producer-wait) and Core 3 (consumer-wait, indirectly: Core 3 sleeps because ISB_C is full, but that means Core 3 isn't dequeueing ISB_B either).

Actually let me reconsider — Core 3 sleeps **as a producer of ISB_C**, not as a consumer of ISB_B. It just stops dequeueing as a side-effect. So it doesn't write `CONSUMER_WAIT_REQUEST` on ISB_B. So technically only ISB_C has both sides armed for sleep, and only ISB_B's *producer* is armed.

But your point still applies in a different topology: a slow consumer + fast producer scenario where the producer sleeps, OR cases where a core explicitly arms both directions because it sits between two ISBs. So **simultaneous producer+consumer sleep on the same ISB is a real possibility.**

### The real cost of one OR'd line in this scenario

The killer issue with one OR'd line is **affinity routing**, not race conditions:

- Producer = Core 2, Consumer = Core 3.
- One PLIC line per ISB. Linux assigns it to **one** hart via `smp_affinity`.
- If pinned to Core 2: producer-wake events deliver fine. **Consumer-wake events also go to Core 2**, who runs the ISR, sees "only consumer bit set, not for me," returns. Core 3 never gets woken. **Broken.**
- If pinned to **both** harts (broadcast affinity): both wake on every event. Core 2 wakes spuriously when consumer-wake fires (and vice versa). ~hundreds of wasted cycles per spurious wake (PLIC claim → ISR → mret → re-enter WFI). Functional but wasteful.

So with one OR'd line:
- **Default Linux affinity (one hart)** → **broken**.
- **Broadcast affinity** → works but ~2× spurious wakes.

With two separate PLIC lines:
- Producer line → affinity to Core 2 only.
- Consumer line → affinity to Core 3 only.
- Zero spurious wakes. Each hart sleeps exactly until its event arrives.

### Verdict

Your scenario makes the case clear: **two PLIC lines per ISB.**

---

## Linux integration — the practical path

Two ways to expose two IRQs to user-space without writing a custom kernel module:

### Option A — Two DT nodes per ISB, same MMIO region

Emit **two `SimpleDevice` nodes** in the DT, both pointing to the **same physical address range**, each with **one interrupt**. Different `compatible` strings (e.g., `"ishara,isb-irq-prod"` and `"ishara,isb-irq-cons"`) so `uio_pdrv_genirq` can match both via two `of_id_table` entries.

User-space sees 

 (producer) and 

 (consumer), each with `mmap()` of the same physical MMIO, each blocking on its own IRQ via `read(uio_fd)`.

**Pros:** uses stock `uio_pdrv_genirq`, no kernel module to maintain.  
**Cons:** DT has two nodes with overlapping `reg` (legal, slightly unusual). Need to modprobe with both `of_id` strings.

### Option B — One DT node, two IRQs, custom ~80 LOC UIO module

Write a small `isb_uio.ko` that creates two UIO devices per platform device, each backed by one IRQ. More work but standard pattern.

**Recommendation: Option A.** Cleanest, no kernel code, easy to bring up.

I can scaffold the Chisel side so each ISB emits two DT nodes (two `SimpleDevice` instances pointing at the same address, each owning one `IntSourceNode`).

---

## TileLink write-strobe — your point about WAIT_REQUEST registers

You're right to flag this. Let me clarify what happens:

For `PRODUCER_WAIT_REQUEST`, we don't actually need to store any data. We only need a **write-strobe pulse** — "SW touched this address." The standard pattern:

```scala
val producerWaitFire = WireDefault(false.B)
node.regmap(
  ...
  0x18 -> Seq(RegField(1,
    RegReadFn(_  => (true.B, 0.U)),               // reads return 0
    RegWriteFn((valid, _) => {                    // any write fires the pulse
      producerWaitFire := valid
      true.B
    })
  ))
)
```

`producerWaitFire` is high for one cycle when SW writes the register. That cycle, HW does:
```scala
when (producerWaitFire) {
  // immediate re-evaluation
  when (count <= producer_low_watermark) {
    producer_wake := true.B          // fires IRQ
    producer_waiting := false.B
  } .otherwise {
    producer_waiting := true.B
  }
  // and 7a: auto-clear any stale producer_wake from before
  // (Actually we want to re-arm cleanly — see below)
}
```

So no extra MMIO traffic, no separate valid bit — the TileLink write itself **is** the trigger. This is exactly the same mechanism the existing `ENQ` register uses (writes assert `enq.valid` for one cycle).

**No design change needed**, just confirming the implementation approach.

### Subtle point on 7a (auto-clear)

You said "auto-clear when pushing the next producer wait request." The cleanest semantics:

```
on PRODUCER_WAIT_REQUEST write:
  producer_wake := false.B          // clear stale wake from previous cycle
  // then immediately re-evaluate:
  if (count <= producer_low_watermark) {
    producer_wake := true.B         // fires fresh
    producer_waiting := false.B
  } else {
    producer_waiting := true.B
  }
```

Note in Chisel, the second assignment to `producer_wake` wins (last-write-wins for `Reg`), so this works. The IRQ line may glitch briefly but PLIC samples it after settling. Fine.

---

## Final plan — confirm and I implement

1. **Two PLIC lines per ISB** (producer_wake, consumer_wake).
2. **Linux integration via Option A**: two DT `SimpleDevice` nodes, same MMIO, one IRQ each, both consumed by `uio_pdrv_genirq`.
3. Watermark ordering: **elaboration-time check only** (option c) — fail at Scala elaboration if defaults or user-set params violate `low < high`.
4. Auto-clear on next `WAIT_REQUEST` (7a). No W1C, no read-clear.
5. IRQ_STATUS exposes both wake bits and both waiting bits as read-only.
6. Combinational re-evaluation on watermark writes (8a — basically free).

Files I'll create/modify:
- `generators/chipyard/src/main/scala/example/SequentialISBwithInterrupt.scala` — new
- 

 — add a `*WithIRQ*` config that uses the new `WithSequentialISBWithInterrupt(...)` fragment
- I will **not** touch the existing JPEG decoder app yet; SW changes can come after the HW is built.

Confirm and I'll write the Chisel.

---

## Turn 7
*2026-06-09 00:15:50 · copilot/claude-opus-4.7*

### 🧑 User

nice. I confirm. Now before implementing, 

Write the design description. Use only simple terms. Start from scratch(including basic enque deque operations). Then start writing the description. Each section should be clearly seperated. Addtionally all the MMIO registers shoudl be written with the intended outcome. The design also should consider all the points that we have discussed so far. Do this  and finalize the design.

### 🤖 GitHub Copilot

# SequentialISB with Interrupt — Design Description

---

## 1. Background: What the ISB Does

The **Inter-Stage Buffer (ISB)** is a hardware FIFO used to pass data between cores in a pipelined multi-core SoC. Each pipeline stage runs on a different core. A producer core writes items into the ISB; a consumer core reads them out.

The ISB is exposed to software through a small set of memory-mapped (MMIO) registers. Software performs **store** instructions to enqueue and **load** instructions to dequeue.

---

## 2. Enqueue and Dequeue (Ready-Valid Handshake)

### 2.1 Enqueue (producer side)

- The producer writes a data word to the `ENQ` register.
- The ISB asserts `enq_ready` when there is free space in the FIFO.
- The producer asserts `enq_valid` by performing the store (the TileLink write strobe is the `valid`).
- When both `enq_ready` and `enq_valid` are high in the same cycle, the ISB accepts the item.

### 2.2 Dequeue (consumer side)

- The consumer reads a data word from the `DEQ` register.
- The ISB asserts `deq_valid` when at least one item is stored.
- The consumer asserts `deq_ready` by performing the load (the TileLink read strobe is the `ready`).
- When both `deq_ready` and `deq_valid` are high in the same cycle, the ISB removes the item.

### 2.3 Status register

A read-only `STATUS` register exposes:
- `enq_ready` (1 bit) — the FIFO has free space.
- `deq_valid` (1 bit) — the FIFO has data.
- 

 (30 bits) — current item count.

Software can poll `STATUS` before performing `ENQ` / `DEQ` to avoid stalling the bus.

---

## 3. The Problem with Pure Polling

Without any wake-up mechanism, a blocked core must spin:

```
Producer:                          Consumer:
while (!enq_ready) poll;           while (!deq_valid) poll;
write ENQ;                         read DEQ;
```

This wastes power, occupies the core, and pollutes the bus with status reads. The goal of the interrupt extension is to let blocked cores enter `WFI` (Wait-For-Interrupt) and be woken precisely when the FIFO state changes enough to make progress worthwhile.

---

## 4. Watermarks

Two thresholds per direction define when sleep/wake decisions are made.

### 4.1 Producer-side watermarks

- `producer_high_watermark` — when 

 reaches this value, the producer is considered "blocked" and should sleep.
- `producer_low_watermark` — when 

 drops to this value or below, the sleeping producer is woken.

### 4.2 Consumer-side watermarks

- `consumer_low_watermark` — when 

 drops to this value, the consumer is considered "blocked" and should sleep.
- `consumer_high_watermark` — when 

 rises to this value or above, the sleeping consumer is woken.

### 4.3 Defaults

| Watermark | Default |
|---|---|
| `producer_high_watermark` | `depth` |
| `producer_low_watermark` | `depth * 3 / 4` |
| `consumer_high_watermark` | `depth / 4` |
| `consumer_low_watermark` | `0` |

All four are exposed as RW MMIO registers and can be overwritten by software at ISB initialization.

### 4.4 Watermark ordering check

At Chisel elaboration time, the design checks that:
- `producer_low_watermark < producer_high_watermark`
- `consumer_low_watermark < consumer_high_watermark`

Violations cause a compile-time error. SW writes at runtime are **not** clamped (we trust the developer).

---

## 5. Producer Sleep / Wake Protocol

### 5.1 Producer sleeps

The producer code looks like this:

```c
write data to ENQ;
if (count >= producer_high_watermark) {
    write PRODUCER_WAIT_REQUEST;
    wait_for_interrupt();
}
```

### 5.2 What the ISB does when `PRODUCER_WAIT_REQUEST` is written

In the same cycle as the TileLink write, the ISB **immediately re-evaluates** the current count:

- **Case 1 — space already available** (

):
  - Set `producer_wake = 1` (fires the interrupt line immediately).
  - Set `producer_waiting = 0` (the producer is not actually going to sleep for long).
  - The producer's `WFI` returns right away.

- **Case 2 — no space yet** (

):
  - Set `producer_waiting = 1` (record the sleep request).
  - Do not fire the interrupt yet.
  - The producer enters `WFI` and stays asleep.

In both cases, any previous stale value of `producer_wake` is cleared (re-arming is implicit).

### 5.3 What wakes the sleeping producer

While the producer sleeps, the consumer continues dequeuing. The ISB watches for a **falling crossing** of the watermark:

> 

 transitions from `> producer_low_watermark` to `<= producer_low_watermark`.

When this crossing happens **and** `producer_waiting == 1`:
- Set `producer_wake = 1` (the interrupt line goes high).
- Set `producer_waiting = 0`.

The `producer_wake` bit remains latched and the interrupt line stays high until the producer writes `PRODUCER_WAIT_REQUEST` again (which auto-clears the bit as part of the next arm cycle).

### 5.4 Lost-wake-up safety

Because the ISB re-evaluates the count **on the same cycle** as the `PRODUCER_WAIT_REQUEST` write, there is no race window. Two scenarios:

- **Space appears before the write reaches the ISB**: Case 1 fires immediately. `WFI` returns immediately. No sleep.
- **Space appears after the write reaches the ISB but before `WFI` executes**: the wake-up is latched in `producer_wake`. When the producer reaches `WFI`, the pending interrupt (already in the core's `mip` register) returns immediately.
- **Space appears strictly after the producer is asleep**: the crossing fires the interrupt normally.

No combination causes a missed wake-up. Spurious interrupts are also avoided because the wake bit only fires once per arm cycle.

---

## 6. Consumer Sleep / Wake Protocol

Symmetric to the producer side.

### 6.1 Consumer sleeps

```c
read DEQ;
if (count <= consumer_low_watermark) {
    write CONSUMER_WAIT_REQUEST;
    wait_for_interrupt();
}
```

### 6.2 On `CONSUMER_WAIT_REQUEST` write — immediate re-evaluation

- **Case 1 — data already available** (`count >= consumer_high_watermark`):
  - Set `consumer_wake = 1` immediately.
  - Set `consumer_waiting = 0`.

- **Case 2 — not enough data yet** (`count < consumer_high_watermark`):
  - Set `consumer_waiting = 1`.
  - Do not fire the interrupt.

### 6.3 Wake on rising watermark crossing

When 

 transitions from `< consumer_high_watermark` to `>= consumer_high_watermark` **and** `consumer_waiting == 1`:
- Set `consumer_wake = 1`.
- Set `consumer_waiting = 0`.

### 6.4 Lost-wake-up safety

Same argument as Section 5.4, symmetrically applied.

---

## 7. Interrupt Delivery

### 7.1 Two interrupt lines per ISB

Each ISB exposes **two independent interrupt lines** to the PLIC:

- `producer_wake_irq` — routed to the producer core.
- `consumer_wake_irq` — routed to the consumer core.

Two separate lines (rather than one OR'd line) are needed because:
- The producer core and consumer core are different harts.
- A producer-side event must not wake the consumer hart (and vice versa); otherwise the wrong hart leaves `WFI` only to find "not for me" and goes back to sleep, wasting hundreds of cycles each time.
- In pipelines where a bottleneck causes multiple stages to sleep simultaneously (e.g., a 4-core pipeline where the last core is slow and earlier producers fill up and sleep), this can happen frequently.

### 7.2 PLIC routing

Both interrupt lines are routed through the PLIC using `ibus.fromSync` (broadcast routing). At runtime, Linux assigns the IRQ to a specific hart using `/proc/irq/<N>/smp_affinity`.

### 7.3 Level-sensitive lines

Each interrupt line is held high (level-sensitive) until software re-arms by writing the corresponding `*_WAIT_REQUEST` register. This guarantees the PLIC gateway never misses the event.

### 7.4 Linux user-space integration

Each ISB is exposed as **two device-tree nodes** that point to the **same MMIO region** but each has **one interrupt**:
- Node A: `compatible = "ishara,isb-irq-prod"`, owns `producer_wake_irq`.
- Node B: `compatible = "ishara,isb-irq-cons"`, owns `consumer_wake_irq`.

Both nodes can be bound to the stock `uio_pdrv_genirq` kernel driver, exposing them as two separate 

 files. User-space `mmap()`s the MMIO region from either node and calls `read(uio_fd)` to block on the IRQ.

---

## 8. MMIO Register Map

All offsets are byte offsets from the ISB's base address. Beat-aligned for TileLink `beatBytes=8`.

| Offset | Name | Access | Width (bits) | Purpose / Intended Outcome |
|---|---|---|---|---|
| `0x00` | `STATUS` | R | 32 | Read: 

. Software uses this to check FIFO state without taking the slow path. |
| `0x08` | `ENQ` | W | `params.width` | Write data to enqueue. Hardware asserts `enq_valid` for one cycle on write. If `enq_ready == 0`, the TileLink transaction stalls until accepted. |
| `0x10` | `DEQ` | R | `params.width` | Read data to dequeue. Hardware asserts `deq_ready` for one cycle on read. If `deq_valid == 0`, returned data is stale; software must check `STATUS` first. |
| `0x18` | `PRODUCER_WAIT_REQUEST` | W | 1 | Any write arms producer sleep. Hardware immediately re-evaluates 

 and either fires `producer_wake_irq` (if 

) or sets `producer_waiting=1`. Auto-clears stale `producer_wake`. |
| `0x20` | `CONSUMER_WAIT_REQUEST` | W | 1 | Any write arms consumer sleep. Hardware immediately re-evaluates 

 and either fires `consumer_wake_irq` (if `count >= consumer_high_watermark`) or sets `consumer_waiting=1`. Auto-clears stale `consumer_wake`. |
| `0x28` | `IRQ_STATUS` | R | 32 | Read-only diagnostics: bit 0 = `producer_wake`, bit 1 = `consumer_wake`, bit 8 = `producer_waiting`, bit 9 = `consumer_waiting`. Useful for debugging. Reading does not clear anything. |
| `0x30` | `PRODUCER_HIGH_WATERMARK` | RW | `log2Ceil(depth+1)` | Set the count level at which the producer should sleep. Default: `depth`. |
| `0x38` | `PRODUCER_LOW_WATERMARK` | RW | `log2Ceil(depth+1)` | Set the count level at which the sleeping producer wakes. Default: `depth * 3 / 4`. |
| `0x40` | `CONSUMER_HIGH_WATERMARK` | RW | `log2Ceil(depth+1)` | Set the count level at which the sleeping consumer wakes. Default: `depth / 4`. |
| `0x48` | `CONSUMER_LOW_WATERMARK` | RW | `log2Ceil(depth+1)` | Set the count level at which the consumer should sleep. Default: `0`. |

### Notes on the register map

- The `*_WAIT_REQUEST` registers have **no payload**. Hardware acts on the write-strobe pulse only; the written value is ignored.
- Watermark writes take effect immediately (combinational). If the new threshold already matches the current count, the next dequeue/enqueue triggers the crossing.
- All `*_wake` and `*_waiting` bits reset to 0 at boot, so no spurious IRQ fires before software arms a sleep.

---

## 9. Software Usage Pattern (Reference Flow)

### Producer
```c
// One-time init
write PRODUCER_HIGH_WATERMARK = depth;          // optional, default
write PRODUCER_LOW_WATERMARK  = depth*3/4;      // optional, default

// Per item
while (1) {
    write ENQ = data;
    if ((read STATUS).count >= producer_high_watermark) {
        write PRODUCER_WAIT_REQUEST;
        wait_for_irq();   // blocks on UIO read or executes WFI
    }
}
```

### Consumer
```c
// One-time init
write CONSUMER_HIGH_WATERMARK = depth/4;        // optional, default
write CONSUMER_LOW_WATERMARK  = 0;              // optional, default

// Per item
while (1) {
    if ((read STATUS).count <= consumer_low_watermark) {
        write CONSUMER_WAIT_REQUEST;
        wait_for_irq();
    }
    data = read DEQ;
}
```

---

## 10. Summary of Design Decisions

| Decision | Choice |
|---|---|
| IRQ line type | Level-sensitive, latched in HW |
| IRQ ack mechanism | Auto-clear on next `*_WAIT_REQUEST` write (no W1C, no read-clear) |
| Number of IRQ lines per ISB | **Two** (producer_wake, consumer_wake) |
| PLIC routing | Broadcast via `ibus.fromSync`; Linux assigns affinity |
| Watermark order check | Elaboration-time only |
| Linux integration | Two DT nodes per ISB, both bound to stock `uio_pdrv_genirq` |
| Storage backend | Reuse existing `SequentialRegFifo` / `SequentialBramFifo` from 

 |
| File | `generators/chipyard/src/main/scala/example/SequentialISBwithInterrupt.scala` |
| Config fragment name | `WithSequentialISBWithInterrupt(...)` |
| Config key | `SequentialISBWithInterruptKey` |

---

If this matches your intent, confirm and I'll implement it.

---

## Turn 8
*2026-06-09 19:35:48 · copilot/claude-opus-4.7*

### 🧑 User

in the description this should be changed:

write data to ENQ;
if (count >= producer_high_watermark) {
    write PRODUCER_WAIT_REQUEST;
    wait_for_interrupt();
}

To 

check if isb is ready()
if(!enq_ready){
    write PRODUCER_WAIT_REQUEST;
    wait_for_interrupt();
}

Isnt this? This is also should be changed in the consumer side as well. Consumer checks for the deque_valid bit, if not true, wirte the wait request and wfi. This should be always corrected for the section ## 9. Software Usage Pattern (Reference Flow) as well.

Also i find another concern. Look at here:
The producer_wake bit remains latched and the interrupt line stays high until the producer writes PRODUCER_WAIT_REQUEST again (which auto-clears the bit as part of the next arm cycle).

But i have a question regarding this. since producer_wake bit reamins asserted, will the producer be able to go into a sleep using wfi instruction since that is the one used to trigger the interrupt? Can you explain that? I have the same concern regarding consumer side as well. Can you clarify this?

### 🤖 GitHub Copilot

_(no textual answer captured)_

---

## Turn 9
*2026-06-09 19:36:01 · copilot/claude-opus-4.7*

### 🧑 User

@agent Try Again

### 🤖 GitHub Copilot

Good catches on both. Let me address them in order.

---

## Concern 1: Software usage pattern — yes, you're right

Sleep should be triggered by what the producer actually **observes** (`enq_ready == 0` from `STATUS`), not by a count-vs-watermark comparison in software. The watermark is a hardware concern; software just looks at the handshake signal.

The corrected reference flows:

### Producer
```c
write ENQ = data;

if (!enq_ready) {                  // read STATUS, check the bit
    write PRODUCER_WAIT_REQUEST;
    wait_for_interrupt();
}
```

### Consumer
```c
if (!deq_valid) {                  // read STATUS, check the bit
    write CONSUMER_WAIT_REQUEST;
    wait_for_interrupt();
}

data = read DEQ;
```

This will be reflected in the design doc and in the implementation.

But notice this raises a subtlety with the **hardware watermarks**: with `producer_high_watermark = depth` (the default), `enq_ready` only goes low when the FIFO is genuinely full, so the SW check matches the HW criterion exactly. If a user sets `producer_high_watermark < depth`, then **HW must also deassert `enq_ready` at that lower threshold** — otherwise SW would never observe `!enq_ready` until full, and the lower watermark would be ignored. So:

> `enq_ready = (count < producer_high_watermark)` instead of 



Symmetrically:

> `deq_valid = (count > consumer_low_watermark)` instead of 



This is what your original problem statement actually said ("when the FIFO occupancy reaches the producer high watermark, the enqueue ready signal is deasserted") — I just hadn't carried it through cleanly. **OK to enforce this in HW?** With defaults (`producer_high = depth`, `consumer_low = 0`) the behavior matches the current ISB, so no regression.

---

## Concern 2: Can the core enter WFI while the interrupt line is held high?

**Yes, but only if software does one specific thing right before WFI: it must NOT clear/enable interrupts in a way that lets the pending interrupt be taken before WFI.** Let me explain carefully, because this is the most important correctness question.

### What WFI actually does on RISC-V

From the RISC-V privileged spec:
> WFI is allowed to complete (return) whenever an interrupt is **pending** at the current privilege level, **regardless of whether `mie`/`sie` is enabled** and **regardless of whether the trap would actually be taken**.

So WFI returns when `mip.MEIP` (or `sip.SEIP` on S-mode) has the bit corresponding to the ISB's PLIC IRQ set. The crucial word is "pending" — the line being high at the PLIC is enough.

### The two cases for our latched interrupt

**Case A — Line went high BEFORE `WFI`** (e.g., immediate-fire path, or wake fired between `WAIT_REQUEST` write and `WFI`):
- `mip` bit is already set when `WFI` executes.
- `WFI` is effectively a NOP: completes immediately, the trap is taken (if interrupts are enabled in `mstatus`) or the core just falls through.
- **The producer wakes correctly.** ✓

**Case B — Line goes high AFTER `WFI` has begun sleeping:**
- The pending bit transitions 0 → 1 in `mip`.
- `WFI` returns; the trap is taken or core falls through.
- **The producer wakes correctly.** ✓

### The dangerous case — and how Linux/UIO avoids it

If software runs:
```c
write PRODUCER_WAIT_REQUEST;
// ... long time passes, ISR fires, ISR returns ...
// ... but PRODUCER_WAIT_REQUEST is never re-armed ...
WFI;
```

then yes, `mip` still shows the old pending bit → WFI returns immediately. But this is **fine** in our protocol because the *next* time the producer wants to sleep, it will write `PRODUCER_WAIT_REQUEST` first, which auto-clears `producer_wake`, which drops the IRQ line, which clears `mip`. Then `WFI` correctly blocks.

So the rule is:

> **The `WAIT_REQUEST` write must always precede `WFI`.** This is the same rule your original protocol already requires. Auto-clear happens on the write, so by the time `WFI` executes the IRQ line is low (unless it has already legitimately re-fired in Case 1 or Case B above).

### What about the Linux UIO path?

Linux user-space doesn't execute `WFI` directly. It uses:
```c
write(uio_fd, &enable, sizeof(enable));   // re-arms UIO IRQ in kernel
read(uio_fd, &count, sizeof(count));      // blocks until next IRQ
```

The kernel's UIO ISR runs whenever the PLIC delivers the IRQ. The ISR:
1. Reads the IRQ status (and disables the IRQ at the PLIC to silence it).
2. Increments the UIO event counter.
3. Returns. User-space `read()` unblocks.

For the level-sensitive line to not retrigger immediately, the kernel must **mask the PLIC source** before returning from the ISR (this is standard `handle_fasteoi_irq` behavior). User-space then performs the ISB MMIO work (the `WAIT_REQUEST` write that auto-clears `producer_wake`, dropping the line), and finally calls `write(uio_fd, ...)` to re-enable the PLIC source.

So the user-space pattern is:

```c
// In producer loop:
while (1) {
    write ENQ = data;
    if (!enq_ready) {
        write PRODUCER_WAIT_REQUEST;   // clears producer_wake, arms or re-fires immediately
        // (line may already be high at PLIC if Case 1 fired)
        int dummy = 1;
        write(uio_fd, &dummy, 4);      // re-enable PLIC source
        read(uio_fd, &count, 4);       // block until IRQ
    }
}
```

The `write(uio_fd, ...)` → `read(uio_fd, ...)` sequence is the userspace analog of "arm and then WFI." The kernel re-enables the PLIC source and then waits; if the line is already high (Case 1 / Case B), the kernel sees it immediately and `read()` returns straight away.

### Conclusion

**The level-latched line is safe.** WFI / UIO `read()` both correctly handle the "line already high" case by returning immediately. The protocol guarantees:

- If you write `WAIT_REQUEST` and the line is **already low** → auto-clear is irrelevant, normal arm-then-wait flow.
- If you write `WAIT_REQUEST` and the line is **already high** from a prior wake → auto-clear drops it; HW immediately re-evaluates; if condition is met it fires again (Case 1), if not, line stays low and you wait normally.

There is **no scenario where the latched line prevents the core from sleeping when it should**, because the producer always writes `WAIT_REQUEST` (which auto-clears) before sleeping.

---

## One small clarification I want to add to the design

The phrase "auto-clear on next `WAIT_REQUEST` write" was a bit loose. The precise semantics:

```
on PRODUCER_WAIT_REQUEST write (in the same cycle):
  step 1: producer_wake := 0                 (clear stale)
  step 2: evaluate count vs producer_low_watermark
  step 3: if (count <= producer_low_watermark):
            producer_wake := 1               (overrides step 1 — last write wins)
            producer_waiting := 0
          else:
            producer_waiting := 1
            // producer_wake stays at 0 from step 1
```

In Chisel, the `:=` ordering naturally implements this. The interrupt line may dip low for less than one cycle if Case 1 fires — but since the PLIC samples after the cycle settles, this is invisible.

---

## Updated Section 9 (the part you asked me to correct)

### Producer
```c
// One-time init
write PRODUCER_HIGH_WATERMARK = depth;          // optional, default
write PRODUCER_LOW_WATERMARK  = depth*3/4;      // optional, default

while (1) {
    write ENQ = data;

    if (!enq_ready) {                            // read STATUS bit
        write PRODUCER_WAIT_REQUEST;
        wait_for_irq();                          // UIO read() or WFI
    }
}
```

### Consumer
```c
// One-time init
write CONSUMER_HIGH_WATERMARK = depth/4;        // optional, default
write CONSUMER_LOW_WATERMARK  = 0;              // optional, default

while (1) {
    if (!deq_valid) {                            // read STATUS bit
        write CONSUMER_WAIT_REQUEST;
        wait_for_irq();
    }

    data = read DEQ;
}
```

---

## Question for you

Do you confirm:
1. **HW must deassert `enq_ready` at `producer_high_watermark`** (not just at `depth`), so the SW-observable handshake signal matches the configured high watermark? (Same for `deq_valid` and `consumer_low_watermark`.)
2. The wake-line semantics explained above are correct for your use case?

If yes to both, I'll write the updated design doc + implement.

---

## Turn 10
*2026-06-09 20:10:40 · copilot/claude-opus-4.7*

### 🧑 User

concern 1: yes do that. Instead of count comparing with the depth, now it should be compared with the high_watermark. similarly, deq_valid should be compared against consumer_low_watermark. 

I have a question regarding concern 2:
So lets say:
write PRODUCER_WAIT_REQUEST;
WFI;

The core will write to PRODUCER_WAIT_REQUEST mmio address. Then before PRODUCER_WAIT_REQUEST propagates to the isb through the system bus and prepriphery bus, the core will execture WFI instruction. By that time, the interrupt line is still pending(since the last one is not cleared.). Then the core will immediately wakes. This is correct right? We cant always be sure that the wait request will go to the isb before sleeping.(someitmes the bus might be congested.)

### 🤖 GitHub Copilot

Excellent question. Your concern is **legitimate and worth understanding precisely**. Let me trace exactly what happens.

---

## The scenario you describe

Producer's second sleep cycle:

```
                                  ┌─────────────────────────────┐
core executes:                    │ ISB still has producer_wake │
  write WAIT_REQUEST    ──TL──►   │ latched from previous wake  │
                                  │ IRQ line still HIGH         │
  WFI                             └─────────────────────────────┘
```

If `WFI` executes **before** the line-clear has propagated all the way through `ISB → PLIC → mip.MEIP`, then `mip.MEIP` is still set, and `WFI` returns immediately. **Yes, this can happen.**

---

## Why this happens (the precise mechanics)

RISC-V's MMIO ordering is subtle:

1. **The TileLink store** to `WAIT_REQUEST` is issued by the core's LSU. For MMIO regions (PMA=IO), the core waits for the `AccessAck` response before retiring the store.
2. So by the time `WFI` is fetched, the ISB *has* received the write and cleared `producer_wake`.
3. **But** `mip.MEIP` is not updated by the store. `mip.MEIP` follows the PLIC's notify line, which follows the gateway, which follows the source line from the ISB — through 1–3 cycles of synchronizers + register stages.
4. So there is a **small window** (a few cycles) after the store retires where `mip.MEIP` is still high from the prior wake.
5. If `WFI` executes inside that window, it returns immediately.

This isn't a TileLink bug — it's just that **store completion ≠ interrupt-line-state propagation**.

---

## Why it doesn't break Linux (our target)

We're using Linux + UIO. Look at the actual sequence:

```c
// User-space producer loop:
while (1) {
    write ENQ = data;
    if (!enq_ready) {
        write PRODUCER_WAIT_REQUEST;     // ISB clears producer_wake
        int en = 1;
        write(uio_fd, &en, 4);           // tell kernel: re-enable PLIC source
        read(uio_fd, &cnt, 4);           // block on IRQ
    }
}
```

What happens at the kernel level:
1. **Previous IRQ delivery**: kernel ISR ran, masked the PLIC source, incremented UIO counter, returned.
2. **User-space `read(uio_fd)` returned** — but **the PLIC source is still masked**. The line being high inside the ISB is invisible to the core because the PLIC has it disabled.
3. **User-space writes `WAIT_REQUEST`** → ISB clears `producer_wake`, line drops.
4. **User-space `write(uio_fd, &en, 4)`** → kernel re-enables the PLIC source. At this point the line is already low (from step 3), so no spurious IRQ fires.
5. **User-space `read(uio_fd)`** → kernel blocks the task in `wait_event_interruptible`. The task sleeps in the scheduler, not in `WFI`. When the IRQ next fires, the kernel wakes the task.

The kernel **never executes `WFI` while our IRQ line is in an indeterminate state**. The `wait_event` sleep is a software construct that waits for an explicit event counter increment — and that increment only happens when the kernel ISR actually runs, which only happens when the PLIC fires after re-enable.

So **the propagation window you're worried about is entirely hidden by the mask/unmask dance Linux performs around UIO IRQs.** ✓

---

## What about a hypothetical bare-metal case?

(Not our target, but good to understand.) Bare-metal would do:

```c
write PRODUCER_WAIT_REQUEST;
WFI;        // ← race window here
```

Two safe patterns:

1. **Loop on the condition**:
   ```c
   while (!enq_ready) {
       write PRODUCER_WAIT_REQUEST;
       WFI;     // may spuriously return once; loop re-checks and re-arms
   }
   ```
   Cost: at most one wasted WFI per sleep — harmless.

2. **Read-back fence**: insert a load of any ISB register after the store. The load forces the prior store's effects to be visible at the PLIC before WFI:
   ```c
   write PRODUCER_WAIT_REQUEST;
   (void)read STATUS;       // round-trip ensures line state propagated
   WFI;
   ```
   Still not bulletproof against `mip` sync latency, but in practice always works in Rocket.

Since we're Linux-only, we don't need either — but if you ever bring up a bare-metal demo, use pattern (1).

---

## Direct answers to your question

> "The core will write to `PRODUCER_WAIT_REQUEST` mmio address. Then before `PRODUCER_WAIT_REQUEST` propagates to the isb through the system bus and peripheral bus, the core will execute WFI instruction."

The MMIO write **does** reach the ISB before `WFI` executes (because PMA=IO stores wait for `AccessAck`). What does NOT necessarily complete before `WFI` is the **propagation of the line-low signal from the ISB through the PLIC to `mip.MEIP`**.

> "By that time, the interrupt line is still pending (since the last one is not cleared.). Then the core will immediately wakes. This is correct right?"

Yes — for the bare-metal path. **For Linux/UIO, the kernel masks the PLIC source the moment the ISR runs and only re-enables it after the user has armed `WAIT_REQUEST`, so the spurious-wake window does not exist in our target setup.**

> "We can't always be sure that the wait request will go to the isb before sleeping. (sometimes the bus might be congested.)"

The wait-request **does** reach the ISB before `WFI` (synchronous MMIO). The unbounded part is `mip` update latency, which is small (≤ a few cycles) and is fully covered by Linux's masking.

---

## Updated confirmations

1. ✅ HW will deassert `enq_ready = (count < producer_high_watermark)` and `deq_valid = (count > consumer_low_watermark)`.
2. ✅ Software flow uses `!enq_ready` / `!deq_valid` (not count comparisons).
3. ✅ Wake-line latching is safe for Linux/UIO — no bare-metal-specific race.

Now I'll regenerate the final design doc with these corrections, then implement. Want me to proceed?

---

