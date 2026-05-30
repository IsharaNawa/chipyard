/*
  SequentialISB with Interrupt support.

  Functionally identical FIFO storage to SequentialISB (RegFile or BRAM backed),
  but adds two level-sensitive interrupt outputs per ISB instance routed
  through the PLIC, plus software-controlled enable bits to suppress IRQs
  during the steady state.

  Interrupt 0 (NOT_EMPTY): asserted when (count >= notEmptyThreshold) AND
    the consumer has enabled it. The consumer arms this just before going
    to sleep (when the FIFO was empty) so it wakes only after some data
    has accumulated.

  Interrupt 1 (NOT_FULL): asserted when ((depth - count) >= notFullThreshold)
    AND the producer has enabled it. The producer arms this just before
    going to sleep (when the FIFO was full) so it wakes only after some
    space has been freed.

  Thresholds default to depth/4 to add hysteresis: consumers and producers
  wake up only when there is a meaningful batch to process, instead of
  ping-ponging an IRQ every single element.

  Both IE bits are reset to 0 so the line is held low through early boot;
  Linux/OpenSBI will not see spurious IRQs before software arms them.
 */

package chipyard.example

import sys.process._

import chisel3._
import chisel3.util._
import chisel3.experimental.{IntParam, BaseModule}
import freechips.rocketchip.amba.axi4._
import freechips.rocketchip.prci._
import freechips.rocketchip.subsystem.{BaseSubsystem, PBUS}
import org.chipsalliance.cde.config.{Parameters, Field, Config}
import freechips.rocketchip.diplomacy._
import freechips.rocketchip.regmapper.{HasRegMap, RegField, RegFieldDesc}
import freechips.rocketchip.tilelink._
import freechips.rocketchip.interrupts._
import freechips.rocketchip.util.UIntIsOneOf

// DOC include start: ISB IRQ params
case class SequentialISBWithIRQParams(
    isRegFile : Boolean = true,      // type of the memory element
    address : BigInt = 0x4000,       // address of the peripheral
    width: Int = 32,                 // width of the peripheral (data width in bits)
    depth: Int = 10,                 // size of the buffer
    notEmptyThreshold: Int = 0,      // 0 => auto = max(1, depth/4)
    notFullThreshold:  Int = 0       // 0 => auto = max(1, depth/4)
)
// DOC include end: ISB IRQ params

case object SequentialISBWithIRQKey extends Field[List[SequentialISBWithIRQParams]](Nil)

// Re-use the existing IO bundle and FIFO implementations from SequentialISB.scala.
// We only need to add interrupt logic and an extra RegMap entry; the storage
// FIFOs (SequentialRegFifo / SequentialBramFifo) and the SequentialISBTopIO
// bundle are unchanged and imported from the same package.

class SequentialISBWithIRQTL(params: SequentialISBWithIRQParams, beatBytes: Int)(implicit p: Parameters)
  extends ClockSinkDomain(ClockSinkParameters())(p) {

  val device = new SimpleDevice("sequential-isb-irq", Seq("ishara,sequential-isb-irq"))
  val node = TLRegisterNode(Seq(AddressSet(params.address, 4096 - 1)), device, "reg/control", beatBytes = beatBytes)

  // One combined interrupt line per ISB. uio_pdrv_genirq only consumes the
  // first interrupt of a platform device, so we OR the two conditions in HW
  // and let software inspect the status register to disambiguate. The two IE
  // bits below still independently gate each condition, so producers and
  // consumers can each ignore the other side's wake-up.
  val intnode = IntSourceNode(IntSourcePortSimple(num = 1, resources = device.int))

  // Resolve effective thresholds (auto = max(1, depth/4)).
  private val autoThresh = math.max(1, params.depth / 4)
  private val effNotEmptyThreshold = if (params.notEmptyThreshold <= 0) autoThresh else params.notEmptyThreshold
  private val effNotFullThreshold  = if (params.notFullThreshold  <= 0) autoThresh else params.notFullThreshold

  override lazy val module = new ISBImpl
  class ISBImpl extends Impl with HasSequentialISBTopIO {
    val io = IO(new SequentialISBTopIO)
    withClockAndReset(clock, reset) {

      val enq = Wire(new DecoupledIO(UInt(params.width.W)))
      val deq = Wire(new DecoupledIO(UInt(params.width.W)))
      val status = Wire(UInt(32.W))

      // Re-use storage FIFOs defined in SequentialISB.scala (same package).
      val impl = if (params.isRegFile) {
        Module(new SequentialRegFifo(params.width, params.depth))
      } else {
        Module(new SequentialBramFifo(params.width, params.depth))
      }

      impl.io.clock := clock
      impl.io.reset := reset.asBool

      impl.io.enq.bits  := enq.bits
      impl.io.enq.valid := enq.valid
      enq.ready := impl.io.enq.fifo_ready

      deq.bits  := impl.io.deq.bits
      deq.valid := impl.io.deq.valid
      impl.io.deq.consumer_ready := deq.ready

      status := Cat(impl.io.count, impl.io.enq.fifo_ready, impl.io.deq.valid)
      io.sequential_busy := true.B

      // -----------------------------
      // IRQ enable bits (software writable). Reset to 0 so neither line is
      // ever asserted until the worker bare-metal/userspace code arms it.
      // -----------------------------
      val notEmptyIE = RegInit(false.B)
      val notFullIE  = RegInit(false.B)

      // Width-safe count comparisons. impl.io.count is up to 30 bits; widen
      // the constants to match.
      val countW = impl.io.count.getWidth
      val notEmptyCond = impl.io.count >= effNotEmptyThreshold.U(countW.W)
      val freeCount    = params.depth.U((countW + 1).W) - impl.io.count
      val notFullCond  = freeCount >= effNotFullThreshold.U((countW + 1).W)

      // Drive the single combined interrupt line (level-sensitive). Software
      // reads the status register after a wake to figure out which side
      // (or both) is asserted.
      val (intOut, _) = intnode.out(0)
      intOut(0) := (notEmptyCond && notEmptyIE) || (notFullCond && notFullIE)

      // -----------------------------
      // Register map.
      //   0x00  : status            (RO,  32 bits: {count[29:0], fifo_ready, deq_valid})
      //   enqOff: enq               (W,   params.width bits)
      //   deqOff: deq               (R,   params.width bits)
      //   ieOff : IRQ enable        (RW,  bit0 = notEmptyIE, bit1 = notFullIE)
      //   thOff : threshold readout (RO,  high16 = notFullThresh, low16 = notEmptyThresh)
      // -----------------------------
      val statusBytes = 4
      val fieldBytes  = (params.width + 7) / 8
      val beatAlign   = beatBytes
      val enqOffset   = ((statusBytes + beatAlign - 1) / beatAlign) * beatAlign
      val deqOffset   = enqOffset + ((fieldBytes + beatAlign - 1) / beatAlign) * beatAlign
      val ieOffset    = deqOffset + ((fieldBytes + beatAlign - 1) / beatAlign) * beatAlign
      val thOffset    = ieOffset  + beatAlign

      val threshRO = Cat(
        effNotFullThreshold.U(16.W),
        effNotEmptyThreshold.U(16.W)
      )

      node.regmap(
        0x00       -> Seq(RegField.r(32, status)),
        enqOffset  -> Seq(RegField.w(params.width, enq)),
        deqOffset  -> Seq(RegField.r(params.width, deq)),
        ieOffset   -> Seq(
          RegField(1, notEmptyIE, RegFieldDesc("notEmptyIE", "Enable NOT_EMPTY interrupt (consumer wake)")),
          RegField(1, notFullIE,  RegFieldDesc("notFullIE",  "Enable NOT_FULL interrupt (producer wake)"))
        ),
        thOffset   -> Seq(RegField.r(32, threshRO))
      )
    }
  }
}

trait CanHavePeripherySequentialISBWithIRQ { this: BaseSubsystem =>

  private val pbus = locateTLBusWrapper(PBUS)

  val sequential_isb_irq_busy = p(SequentialISBWithIRQKey).zipWithIndex.map { case (params, i) =>
    val portName = s"sequential_isb_irq_$i"

    val isb = LazyModule(new SequentialISBWithIRQTL(params, pbus.beatBytes)(p))
    isb.clockNode := pbus.fixedClockNode
    pbus.coupleTo(portName) { isb.node := TLFragmenter(pbus.beatBytes, pbus.blockBytes) := _ }

    // Route the two interrupt lines into the subsystem's interrupt bus.
    // pbus / ibus are on the same clock domain in our setup, so fromSync is correct.
    ibus.fromSync := isb.intnode

    val busy = InModuleBody {
      val b = IO(Output(Bool())).suggestName(s"sequential_isb_irq_busy_$i")
      b := isb.module.io.sequential_busy
      b
    }
    busy
  }
}

class WithSequentialISBWithIRQ(
    isRegFile: Boolean = true,
    address:   BigInt  = 0x4000,
    width:     Int     = 32,
    depth:     Int     = 10,
    notEmptyThreshold: Int = 0,
    notFullThreshold:  Int = 0
) extends Config((site, here, up) => {
  case SequentialISBWithIRQKey => up(SequentialISBWithIRQKey) ++ List(
    SequentialISBWithIRQParams(
      isRegFile = isRegFile,
      address   = address,
      width     = width,
      depth     = depth,
      notEmptyThreshold = notEmptyThreshold,
      notFullThreshold  = notFullThreshold))
})
