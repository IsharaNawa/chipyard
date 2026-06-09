/*
  InterruptAwareMMIOFIFO

  An Inter-Stage Buffer (ISB) for pipelined multi-core SoCs that exposes a
  ready-valid FIFO through MMIO AND provides a hardware sleep/wake-up
  mechanism so that blocked producer/consumer cores can WFI instead of
  busy-polling.

  Storage backend is reused from SequentialISB.scala (same package):
    - SequentialRegFifo  : register file (LUT-based, isRegFile=true)
    - SequentialBramFifo : SyncReadMem (BRAM-mapped, isRegFile=false)

  Per ISB instance we expose:
    - One TileLink slave node (MMIO register file, 4KB region)
    - Two independent IntSourceNodes routed through the PLIC:
        * producer_wake_irq  (interrupt index 0 on the DT node)
        * consumer_wake_irq  (interrupt index 1 on the DT node)

  Both IRQ source nodes are attached to a single SimpleDevice so the
  generated device tree contains ONE node per ISB with `reg` plus a two-
  element `interrupts` property. Linux can route producer/consumer wakes to
  different harts via `/proc/irq/<N>/smp_affinity`. (Stock uio_pdrv_genirq
  only binds the first IRQ; a small custom UIO module can later expose both
  lines as separate /dev/uioN files.)

  See the design description in chat for the full protocol; key invariants:
    * SW-visible enq_ready := (count < producer_high_watermark)  AND inner.fifo_ready
    * SW-visible deq_valid := (count > consumer_low_watermark)   AND inner.deq.valid
    * Wake state machines auto-clear stale producer_wake / consumer_wake on
      the next *_WAIT_REQUEST write.
    * Interrupt lines are level-sensitive (PLIC-friendly), kept latched until
      auto-cleared.
 */

package chipyard.example

import chisel3._
import chisel3.util._
import freechips.rocketchip.prci._
import freechips.rocketchip.subsystem.{BaseSubsystem, PBUS}
import org.chipsalliance.cde.config.{Parameters, Field, Config}
import freechips.rocketchip.diplomacy._
import freechips.rocketchip.resources.SimpleDevice
import freechips.rocketchip.regmapper.{RegField, RegFieldDesc}
import freechips.rocketchip.tilelink._
import freechips.rocketchip.interrupts._

// DOC include start: IAMMIOFIFO params
case class InterruptAwareMMIOFIFOParams(
    isRegFile : Boolean = true,     // storage type: true=RegFifo, false=BramFifo
    address   : BigInt  = 0x4000,   // MMIO base address of the peripheral
    width     : Int     = 32,       // data width in bits
    depth     : Int     = 10,       // FIFO depth (number of slots)
    isDebug   : Boolean = false     // expose per-event debug counters in MMIO
)
// DOC include end: IAMMIOFIFO params

case object InterruptAwareMMIOFIFOKey extends Field[List[InterruptAwareMMIOFIFOParams]](Nil)

class InterruptAwareMMIOFIFOTopIO extends Bundle {
  val producer_waiting = Output(Bool())
  val consumer_waiting = Output(Bool())
}

trait HasInterruptAwareMMIOFIFOTopIO {
  def io: InterruptAwareMMIOFIFOTopIO
}

class InterruptAwareMMIOFIFOTL(params: InterruptAwareMMIOFIFOParams, beatBytes: Int)(implicit p: Parameters)
  extends ClockSinkDomain(ClockSinkParameters())(p) {

  // -------------------------------------------------------------------------
  // Watermark defaults + elaboration-time invariant checks
  // -------------------------------------------------------------------------
  private val defaultProducerHigh = params.depth
  private val defaultProducerLow  = params.depth * 3 / 4
  private val defaultConsumerHigh = params.depth / 4
  private val defaultConsumerLow  = 0

  require(defaultProducerLow < defaultProducerHigh,
    s"InterruptAwareMMIOFIFO@0x${params.address.toString(16)}: " +
    s"producer_low_watermark ($defaultProducerLow) must be < producer_high_watermark ($defaultProducerHigh)")
  require(defaultConsumerLow < defaultConsumerHigh,
    s"InterruptAwareMMIOFIFO@0x${params.address.toString(16)}: " +
    s"consumer_low_watermark ($defaultConsumerLow) must be < consumer_high_watermark ($defaultConsumerHigh)")
  require(params.depth >= 2,
    s"InterruptAwareMMIOFIFO@0x${params.address.toString(16)}: depth must be >= 2 (got ${params.depth})")

  // -------------------------------------------------------------------------
  // Diplomatic nodes
  //   * One SimpleDevice that owns the reg property AND both interrupts.
  //     This avoids DT-node de-duplication issues when many instances share
  //     a compatible string.
  //   * One TLRegisterNode for the MMIO register file.
  //   * Two IntSourceNodes (producer and consumer) — both rooted at the same
  //     device so they appear as `interrupts = <prod_hwirq cons_hwirq>` on a
  //     single DT node.
  // -------------------------------------------------------------------------
  val device = new SimpleDevice("interrupt-aware-mmio-fifo",
    Seq("ishara,interrupt-aware-mmio-fifo"))

  val node = TLRegisterNode(
    address     = Seq(AddressSet(params.address, 4096 - 1)),
    device      = device,
    concurrency = 1,
    beatBytes   = beatBytes)

  // Two distinct interrupt sources, both tied to `device.int`. The DT will
  // emit them in declaration order: interrupt[0] = producer, [1] = consumer.
  val intnodeProd = IntSourceNode(IntSourcePortSimple(num = 1, resources = device.int))
  val intnodeCons = IntSourceNode(IntSourcePortSimple(num = 1, resources = device.int))

  override lazy val module = new ISBImpl
  class ISBImpl extends Impl with HasInterruptAwareMMIOFIFOTopIO {
    val io = IO(new InterruptAwareMMIOFIFOTopIO)
    withClockAndReset(clock, reset) {

      // -----------------------------------------------------------------
      // Storage FIFO — reuse from SequentialISB.scala (same package)
      // -----------------------------------------------------------------
      val impl = if (params.isRegFile) {
        Module(new SequentialRegFifo(params.width, params.depth))
      } else {
        Module(new SequentialBramFifo(params.width, params.depth))
      }
      impl.io.clock := clock
      impl.io.reset := reset.asBool

      // The inner FIFO's count is 30-bit (SequentialISB convention). Widen
      // both sides of every watermark comparison to a common width so
      // Chisel doesn't truncate.
      val watermarkW = log2Ceil(params.depth + 1)
      val cmpW       = math.max(watermarkW, impl.io.count.getWidth)
      val countWide  = impl.io.count.pad(cmpW)

      // -----------------------------------------------------------------
      // Watermark registers (RW, software-tunable at runtime)
      // -----------------------------------------------------------------
      val producerHighWM = RegInit(defaultProducerHigh.U(watermarkW.W))
      val producerLowWM  = RegInit(defaultProducerLow.U(watermarkW.W))
      val consumerHighWM = RegInit(defaultConsumerHigh.U(watermarkW.W))
      val consumerLowWM  = RegInit(defaultConsumerLow.U(watermarkW.W))

      // -----------------------------------------------------------------
      // Watermark-aware handshake signals (SW-visible enq_ready / deq_valid)
      //   * SW will sleep based on these (not based on inner full/empty)
      //   * TileLink writes/reads stall on these (back-pressure)
      // -----------------------------------------------------------------
      val swEnqReady = impl.io.enq.fifo_ready && (countWide < producerHighWM.pad(cmpW))
      val swDeqValid = impl.io.deq.valid       && (countWide > consumerLowWM.pad(cmpW))

      // TileLink-facing decoupled wires
      val enq = Wire(new DecoupledIO(UInt(params.width.W)))
      val deq = Wire(new DecoupledIO(UInt(params.width.W)))

      // ENQ path: TL strobe drives valid, we drive ready (gated by high WM)
      impl.io.enq.bits  := enq.bits
      impl.io.enq.valid := enq.valid
      enq.ready := swEnqReady

      // DEQ path: gate valid by low WM, pass ready through to inner FIFO
      deq.bits  := impl.io.deq.bits
      deq.valid := swDeqValid
      impl.io.deq.consumer_ready := deq.ready

      // -----------------------------------------------------------------
      // STATUS register (R): {count[29:0], enq_ready, deq_valid}
      // Exactly matches the layout used by SequentialISB so existing
      // user-space helpers (ISB_STATUS_DEQUEUE_VALID / _ENQUEUE_READY /
      // _COUNT_SHIFT) keep working.
      // -----------------------------------------------------------------
      val status = Cat(impl.io.count, swEnqReady, swDeqValid)

      // =================================================================
      // Producer wait/wake state machine
      // =================================================================
      val producerWaiting = RegInit(false.B)
      val producerWake    = RegInit(false.B)

      // One-cycle pulse when SW writes the PRODUCER_WAIT_REQUEST MMIO reg.
      // RegField.w on a DecoupledIO drives valid for one cycle on write.
      val producerWaitReq = Wire(Decoupled(UInt(1.W)))
      producerWaitReq.ready := true.B
      val producerWaitFire = producerWaitReq.valid

      // Falling crossing detector: count transitions (> low) -> (<= low)
      val countLEProducerLow     = countWide <= producerLowWM.pad(cmpW)
      val countLEProducerLowPrev = RegNext(countLEProducerLow, true.B) // count=0 at reset
      val producerFallingCross   = !countLEProducerLowPrev && countLEProducerLow

      when (producerWaitFire) {
        // Step 1: auto-clear any stale wake from a prior cycle (re-arm).
        producerWake := false.B
        // Step 2: immediate re-evaluation.
        when (countLEProducerLow) {
          // Case 1: space already available → fire immediately, do not sleep.
          producerWake    := true.B
          producerWaiting := false.B
        } .otherwise {
          // Case 2: not enough free space yet → record waiting state.
          producerWaiting := true.B
        }
      } .elsewhen (producerFallingCross && producerWaiting) {
        // Crossing-based wake: producer was waiting and count just dropped.
        producerWake    := true.B
        producerWaiting := false.B
      }

      // =================================================================
      // Consumer wait/wake state machine (symmetric)
      // =================================================================
      val consumerWaiting = RegInit(false.B)
      val consumerWake    = RegInit(false.B)

      val consumerWaitReq = Wire(Decoupled(UInt(1.W)))
      consumerWaitReq.ready := true.B
      val consumerWaitFire = consumerWaitReq.valid

      // Rising crossing detector: count transitions (< high) -> (>= high)
      val countGEConsumerHigh     = countWide >= consumerHighWM.pad(cmpW)
      val countGEConsumerHighPrev = RegNext(countGEConsumerHigh, false.B) // count=0 at reset
      val consumerRisingCross     = !countGEConsumerHighPrev && countGEConsumerHigh

      when (consumerWaitFire) {
        consumerWake := false.B
        when (countGEConsumerHigh) {
          // Case 1: data already available → fire immediately.
          consumerWake    := true.B
          consumerWaiting := false.B
        } .otherwise {
          consumerWaiting := true.B
        }
      } .elsewhen (consumerRisingCross && consumerWaiting) {
        consumerWake    := true.B
        consumerWaiting := false.B
      }

      // -----------------------------------------------------------------
      // Drive PLIC interrupt outputs (level-sensitive)
      // -----------------------------------------------------------------
      val (intOutProd, _) = intnodeProd.out(0)
      val (intOutCons, _) = intnodeCons.out(0)
      intOutProd(0) := producerWake
      intOutCons(0) := consumerWake

      io.producer_waiting := producerWaiting
      io.consumer_waiting := consumerWaiting

      // -----------------------------------------------------------------
      // IRQ_STATUS (R): bit0=producerWake, bit1=consumerWake,
      //                 bit8=producerWaiting, bit9=consumerWaiting
      // (Cat builds MSB-first; lay it out so bit indices match the doc.)
      // -----------------------------------------------------------------
      val irqStatus = Cat(
        0.U(22.W),                          // bits [31:10] reserved
        consumerWaiting,                    // bit  9
        producerWaiting,                    // bit  8
        0.U(6.W),                           // bits [7:2]   reserved
        consumerWake,                       // bit  1
        producerWake                        // bit  0
      )

      // =================================================================
      // Debug event counters (only instantiated if params.isDebug == true)
      // Counters saturate at 2^32-1 to avoid wrap surprising the analysis;
      // each increments on a distinct, mutually-exclusive event.
      // =================================================================
      val debugRegs: Seq[(Int, Seq[RegField])] = if (params.isDebug) {
        def saturatingInc(r: UInt): UInt = Mux(r.andR, r, r + 1.U)

        val pWaitReqCount      = RegInit(0.U(32.W))
        val pImmediateIrqCount = RegInit(0.U(32.W))
        val pNormalWakeCount   = RegInit(0.U(32.W))
        val pWakePendingCount  = RegInit(0.U(32.W))
        val pWakeClearingCount = RegInit(0.U(32.W))

        val cWaitReqCount      = RegInit(0.U(32.W))
        val cImmediateIrqCount = RegInit(0.U(32.W))
        val cNormalWakeCount   = RegInit(0.U(32.W))
        val cWakePendingCount  = RegInit(0.U(32.W))
        val cWakeClearingCount = RegInit(0.U(32.W))

        // Producer events
        when (producerWaitFire) {
          pWaitReqCount := saturatingInc(pWaitReqCount)
          // We are clearing a stale wake if the latch was high when the
          // request arrived.
          when (producerWake) {
            pWakeClearingCount := saturatingInc(pWakeClearingCount)
          }
          when (countLEProducerLow) {
            pImmediateIrqCount := saturatingInc(pImmediateIrqCount)
          } .otherwise {
            pWakePendingCount := saturatingInc(pWakePendingCount)
          }
        } .elsewhen (producerFallingCross && producerWaiting) {
          pNormalWakeCount := saturatingInc(pNormalWakeCount)
        }

        // Consumer events
        when (consumerWaitFire) {
          cWaitReqCount := saturatingInc(cWaitReqCount)
          when (consumerWake) {
            cWakeClearingCount := saturatingInc(cWakeClearingCount)
          }
          when (countGEConsumerHigh) {
            cImmediateIrqCount := saturatingInc(cImmediateIrqCount)
          } .otherwise {
            cWakePendingCount := saturatingInc(cWakePendingCount)
          }
        } .elsewhen (consumerRisingCross && consumerWaiting) {
          cNormalWakeCount := saturatingInc(cNormalWakeCount)
        }

        // We assign concrete offsets below in the main regmap section so
        // that the layout is one continuous, beat-aligned block.
        Seq(
          (0, Seq(RegField.r(32, pWaitReqCount,
            RegFieldDesc("p_wait_req_count", "Producer WAIT_REQUEST writes")))),
          (1, Seq(RegField.r(32, pImmediateIrqCount,
            RegFieldDesc("p_immediate_irq_count", "Producer immediate-fire events (Case 1)")))),
          (2, Seq(RegField.r(32, pNormalWakeCount,
            RegFieldDesc("p_normal_wake_count", "Producer crossing-based wake events")))),
          (3, Seq(RegField.r(32, pWakePendingCount,
            RegFieldDesc("p_wake_pending_count", "Producer entered waiting state (Case 2)")))),
          (4, Seq(RegField.r(32, pWakeClearingCount,
            RegFieldDesc("p_wake_clearing_count", "Producer stale-wake clears on re-arm")))),
          (5, Seq(RegField.r(32, cWaitReqCount,
            RegFieldDesc("c_wait_req_count", "Consumer WAIT_REQUEST writes")))),
          (6, Seq(RegField.r(32, cImmediateIrqCount,
            RegFieldDesc("c_immediate_irq_count", "Consumer immediate-fire events (Case 1)")))),
          (7, Seq(RegField.r(32, cNormalWakeCount,
            RegFieldDesc("c_normal_wake_count", "Consumer crossing-based wake events")))),
          (8, Seq(RegField.r(32, cWakePendingCount,
            RegFieldDesc("c_wake_pending_count", "Consumer entered waiting state (Case 2)")))),
          (9, Seq(RegField.r(32, cWakeClearingCount,
            RegFieldDesc("c_wake_clearing_count", "Consumer stale-wake clears on re-arm"))))
        )
      } else Seq()

      // =================================================================
      // MMIO Register Map
      // =================================================================
      // Every register slot is `beatBytes` bytes wide and lives at a
      // multiple of beatBytes so 64-bit-wide ENQ/DEQ never straddle beats.
      val beatAlign  = beatBytes
      val fieldBytes = (params.width + 7) / 8
      require(fieldBytes <= beatAlign,
        s"InterruptAwareMMIOFIFO: width=${params.width} exceeds beatBytes=$beatAlign; widen the bus or shrink the data field.")

      val statusOff   = 0
      val enqOff      = statusOff   + beatAlign            // 0x08
      val deqOff      = enqOff      + beatAlign            // 0x10
      val pWaitOff    = deqOff      + beatAlign            // 0x18
      val cWaitOff    = pWaitOff    + beatAlign            // 0x20
      val irqStatOff  = cWaitOff    + beatAlign            // 0x28
      val pHighWMOff  = irqStatOff  + beatAlign            // 0x30
      val pLowWMOff   = pHighWMOff  + beatAlign            // 0x38
      val cHighWMOff  = pLowWMOff   + beatAlign            // 0x40
      val cLowWMOff   = cHighWMOff  + beatAlign            // 0x48
      val debugBase   = cLowWMOff   + beatAlign            // 0x50

      // Translate debug placeholders (index i) to concrete offsets.
      val debugAtOffsets: Seq[(Int, Seq[RegField])] = debugRegs.map { case (idx, fs) =>
        (debugBase + idx * beatAlign, fs)
      }

      val mainRegs: Seq[(Int, Seq[RegField])] = Seq(
        statusOff  -> Seq(RegField.r(32, status,
                          RegFieldDesc("status", "{count[29:0], enq_ready, deq_valid}"))),
        enqOff     -> Seq(RegField.w(params.width, enq,
                          RegFieldDesc("enq", "Enqueue data; write strobes enq.valid"))),
        deqOff     -> Seq(RegField.r(params.width, deq,
                          RegFieldDesc("deq", "Dequeue data; read strobes deq.ready"))),
        pWaitOff   -> Seq(RegField.w(1, producerWaitReq,
                          RegFieldDesc("producer_wait_request", "Arm producer sleep (data ignored)"))),
        cWaitOff   -> Seq(RegField.w(1, consumerWaitReq,
                          RegFieldDesc("consumer_wait_request", "Arm consumer sleep (data ignored)"))),
        irqStatOff -> Seq(RegField.r(32, irqStatus,
                          RegFieldDesc("irq_status",
                            "bit0=producer_wake, bit1=consumer_wake, bit8=producer_waiting, bit9=consumer_waiting"))),
        pHighWMOff -> Seq(RegField(watermarkW, producerHighWM,
                          RegFieldDesc("producer_high_watermark", s"default=${defaultProducerHigh}"))),
        pLowWMOff  -> Seq(RegField(watermarkW, producerLowWM,
                          RegFieldDesc("producer_low_watermark",  s"default=${defaultProducerLow}"))),
        cHighWMOff -> Seq(RegField(watermarkW, consumerHighWM,
                          RegFieldDesc("consumer_high_watermark", s"default=${defaultConsumerHigh}"))),
        cLowWMOff  -> Seq(RegField(watermarkW, consumerLowWM,
                          RegFieldDesc("consumer_low_watermark",  s"default=${defaultConsumerLow}")))
      )

      node.regmap((mainRegs ++ debugAtOffsets): _*)
    }
  }
}

// ---------------------------------------------------------------------------
// Subsystem trait: instantiate one device per entry of InterruptAwareMMIOFIFOKey
// ---------------------------------------------------------------------------
trait CanHavePeripheryInterruptAwareMMIOFIFO { this: BaseSubsystem =>

  private val pbus = locateTLBusWrapper(PBUS)

  val interrupt_aware_mmio_fifo_waiting = p(InterruptAwareMMIOFIFOKey).zipWithIndex.map {
    case (params, i) =>
      val portName = s"interrupt_aware_mmio_fifo_$i"
      val isb = LazyModule(new InterruptAwareMMIOFIFOTL(params, pbus.beatBytes)(p))
      isb.clockNode := pbus.fixedClockNode
      pbus.coupleTo(portName) {
        isb.node := TLFragmenter(pbus.beatBytes, pbus.blockBytes) := _
      }

      // Route both PLIC lines (producer + consumer) via ibus.fromSync.
      // Linux configures hart affinity at runtime via /proc/irq/N/smp_affinity.
      ibus.fromSync := isb.intnodeProd
      ibus.fromSync := isb.intnodeCons

      InModuleBody {
        val pwait = IO(Output(Bool())).suggestName(s"iamf_producer_waiting_$i")
        val cwait = IO(Output(Bool())).suggestName(s"iamf_consumer_waiting_$i")
        pwait := isb.module.io.producer_waiting
        cwait := isb.module.io.consumer_waiting
        (pwait, cwait)
      }
  }
}

// ---------------------------------------------------------------------------
// Config fragment: append one ISB instance per invocation.
// ---------------------------------------------------------------------------
class WithInterruptAwareMMIOFIFO(
    isRegFile : Boolean = true,
    address   : BigInt  = 0x4000,
    width     : Int     = 32,
    depth     : Int     = 10,
    isDebug   : Boolean = false
) extends Config((site, here, up) => {
  case InterruptAwareMMIOFIFOKey => up(InterruptAwareMMIOFIFOKey) ++ List(
    InterruptAwareMMIOFIFOParams(
      isRegFile = isRegFile,
      address   = address,
      width     = width,
      depth     = depth,
      isDebug   = isDebug))
})
