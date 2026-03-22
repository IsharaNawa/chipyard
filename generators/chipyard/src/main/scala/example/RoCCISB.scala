/*
 * RoCC-based Inter-Stage Buffer (ISB) for point-to-point core-to-core communication.
 *
 * This bypasses the TileLink bus entirely, providing a direct Decoupled FIFO channel
 * between a writer core and a reader core via the RoCC coprocessor interface.
 *
 * Architecture:
 *   Core 0 (ISBWriterRoCC [with data FIFO]) ──> Core 1 (ISBReaderRoCC)
 *
 * The writer core uses custom0 instructions:
 *   funct=0: blocking write (rs1 data → FIFO, stalls if full)
 *   funct=1: query status (returns 1 in rd if FIFO has space, 0 if full)
 *
 * The reader core uses custom1 instructions:
 *   funct=0: blocking read (FIFO data → rd, stalls if empty)
 *   funct=1: query status (returns 1 in rd if FIFO has data, 0 if empty)
 *
 * Usage: Include WithMultiRoCC and WithRoCCISB in your config:
 *   new chipyard.example.WithRoCCISB(writerTileId=0, readerTileId=1, width=64, depth=256) ++
 *   new chipyard.config.WithMultiRoCC ++
 *   new freechips.rocketchip.rocket.WithNBigCores(2) ++
 *   new chipyard.config.AbstractConfig
 */

package chipyard.example

import chisel3._
import chisel3.util._
import chisel3.experimental.SourceInfo
import org.chipsalliance.cde.config.{Parameters, Field, Config}
import freechips.rocketchip.diplomacy._
import freechips.rocketchip.tile._
import freechips.rocketchip.subsystem._
import chipyard.config.{WithMultiRoCC, MultiRoCCKey}

// ────────────────────────────────────────────────────────
// ISB Diplomacy Protocol
// ────────────────────────────────────────────────────────

case class ISBEdgeParams(width: Int)

object ISBNodeImp extends SimpleNodeImp[ISBEdgeParams, ISBEdgeParams, ISBEdgeParams, DecoupledIO[UInt]] {
  def edge(pd: ISBEdgeParams, pu: ISBEdgeParams, p: Parameters, sourceInfo: SourceInfo): ISBEdgeParams = {
    require(pd.width == pu.width, s"ISB width mismatch: ${pd.width} vs ${pu.width}")
    pd
  }
  def bundle(e: ISBEdgeParams): DecoupledIO[UInt] = Decoupled(UInt(e.width.W))
  def render(e: ISBEdgeParams): RenderedEdge = RenderedEdge(colour = "#00cc00", label = "ISB")
}

case class ISBSourceNode(ep: ISBEdgeParams)(implicit valName: ValName)
  extends SourceNode(ISBNodeImp)(Seq(ep))

case class ISBSinkNode(ep: ISBEdgeParams)(implicit valName: ValName)
  extends SinkNode(ISBNodeImp)(Seq(ep))

// ────────────────────────────────────────────────────────
// RoCC Writer Accelerator
// ────────────────────────────────────────────────────────

class ISBWriterRoCC(opcodes: OpcodeSet, val isbWidth: Int = 64, val isbDepth: Int = 256)(implicit p: Parameters)
    extends LazyRoCC(opcodes, nPTWPorts = 0) {
  val isbNode = ISBSourceNode(ISBEdgeParams(isbWidth))
  override lazy val module = new ISBWriterRoCCModuleImp(this)
}

class ISBWriterRoCCModuleImp(outer: ISBWriterRoCC)
    extends LazyRoCCModuleImp(outer) {
  val cmd = Queue(io.cmd)
  val (out, _) = outer.isbNode.out(0)

  // Data FIFO lives inside the tile's clock domain (avoids ClockSinkDomain issues)
  val dataFifo = Module(new Queue(UInt(outer.isbWidth.W), outer.isbDepth))

  // Connect FIFO output to the ISBNode (which crosses tile boundary to the reader)
  out.valid := dataFifo.io.deq.valid
  out.bits  := dataFifo.io.deq.bits
  dataFifo.io.deq.ready := out.ready

  val isWrite = cmd.bits.inst.funct === 0.U
  val isQuery = cmd.bits.inst.funct === 1.U

  // Default: don't enqueue into the FIFO
  dataFifo.io.enq.valid := false.B
  dataFifo.io.enq.bits  := cmd.bits.rs1

  io.resp.valid    := false.B
  io.resp.bits.rd  := cmd.bits.inst.rd
  io.resp.bits.data := dataFifo.io.enq.ready  // for status query

  cmd.ready := false.B

  when (isWrite) {
    // funct=0: blocking write — push rs1 into the data FIFO
    dataFifo.io.enq.valid := cmd.valid
    cmd.ready := dataFifo.io.enq.ready
  } .elsewhen (isQuery) {
    // funct=1: non-blocking status query — returns 1 if FIFO has space
    io.resp.valid := cmd.valid
    io.resp.bits.data := dataFifo.io.enq.ready
    cmd.ready := io.resp.fire
  }

  io.busy := cmd.valid
  io.interrupt := false.B
}

// ────────────────────────────────────────────────────────
// RoCC Reader Accelerator
// ────────────────────────────────────────────────────────

class ISBReaderRoCC(opcodes: OpcodeSet, val isbWidth: Int = 64)(implicit p: Parameters)
    extends LazyRoCC(opcodes, nPTWPorts = 0) {
  val isbNode = ISBSinkNode(ISBEdgeParams(isbWidth))
  override lazy val module = new ISBReaderRoCCModuleImp(this)
}

class ISBReaderRoCCModuleImp(outer: ISBReaderRoCC)
    extends LazyRoCCModuleImp(outer) {
  val cmd = Queue(io.cmd)
  val (in, _) = outer.isbNode.in(0)

  val isRead  = cmd.bits.inst.funct === 0.U
  val isQuery = cmd.bits.inst.funct === 1.U

  // Default: don't consume from the FIFO
  in.ready := false.B

  io.resp.valid     := false.B
  io.resp.bits.rd   := cmd.bits.inst.rd
  io.resp.bits.data := in.bits

  cmd.ready := false.B

  when (isRead) {
    // funct=0: blocking read — pop data from the FIFO into rd
    in.ready      := cmd.valid && io.resp.ready
    io.resp.valid := cmd.valid && in.valid
    io.resp.bits.data := in.bits
    cmd.ready := io.resp.fire
  } .elsewhen (isQuery) {
    // funct=1: non-blocking status query — returns 1 if FIFO has data
    io.resp.valid     := cmd.valid
    io.resp.bits.data := in.valid
    cmd.ready := io.resp.fire
  }

  io.busy := cmd.valid
  io.interrupt := false.B
}

// ────────────────────────────────────────────────────────
// Subsystem integration trait
// ────────────────────────────────────────────────────────

case class RoCCISBParams(
  writerTileId: Int,
  readerTileId: Int,
  width: Int = 64,
  depth: Int = 256
)

case object RoCCISBKey extends Field[Seq[RoCCISBParams]](Nil)

trait CanHaveRoCCISB { this: BaseSubsystem with InstantiatesHierarchicalElements =>
  p(RoCCISBKey).zipWithIndex.foreach { case (params, i) =>
    val writerTile = totalTiles.getOrElse(params.writerTileId,
      throw new Exception(s"ISB channel $i: tile ${params.writerTileId} not found"))
    val readerTile = totalTiles.getOrElse(params.readerTileId,
      throw new Exception(s"ISB channel $i: tile ${params.readerTileId} not found"))

    val writerRoCC = writerTile match {
      case r: RocketTile => r.roccs.collectFirst { case w: ISBWriterRoCC => w }
      case _ => None
    }
    val readerRoCC = readerTile match {
      case r: RocketTile => r.roccs.collectFirst { case r: ISBReaderRoCC => r }
      case _ => None
    }

    require(writerRoCC.isDefined,
      s"ISB channel $i: No ISBWriterRoCC on tile ${params.writerTileId}. " +
      "Ensure WithRoCCISB and WithMultiRoCC are in the config.")
    require(readerRoCC.isDefined,
      s"ISB channel $i: No ISBReaderRoCC on tile ${params.readerTileId}. " +
      "Ensure WithRoCCISB and WithMultiRoCC are in the config.")

    // Direct connection: writer's ISBNode → reader's ISBNode
    // The data FIFO lives inside ISBWriterRoCC (in the tile's clock domain)
    readerRoCC.get.isbNode := writerRoCC.get.isbNode
  }
}

// ────────────────────────────────────────────────────────
// Config fragments
// ────────────────────────────────────────────────────────

/**
 * Adds a RoCC-based ISB channel between two tiles.
 *
 * This config fragment:
 *   1. Registers the ISB channel parameters in RoCCISBKey
 *   2. Adds ISBWriterRoCC (custom0) to the writer tile
 *   3. Adds ISBReaderRoCC (custom1) to the reader tile
 *
 * Each tile gets at most one writer and one reader RoCC.
 * Must be paired with WithMultiRoCC in the config chain.
 */
class WithRoCCISB(
  writerTileId: Int,
  readerTileId: Int,
  width: Int = 64,
  depth: Int = 256,
  writerOpcode: OpcodeSet = OpcodeSet.custom0,
  readerOpcode: OpcodeSet = OpcodeSet.custom1
) extends Config((site, here, up) => {
  case RoCCISBKey => up(RoCCISBKey) ++ Seq(
    RoCCISBParams(writerTileId, readerTileId, width, depth))
  case MultiRoCCKey => {
    val existing = up(MultiRoCCKey, site)
    val isbChannels = up(RoCCISBKey)

    // Only add a writer/reader RoCC if this tile doesn't already have one
    val writerNeeded = !isbChannels.exists(_.writerTileId == writerTileId)
    val readerNeeded = !isbChannels.exists(_.readerTileId == readerTileId)

    val w = width
    val d = depth
    var updated = existing

    if (writerNeeded) {
      updated = updated + (writerTileId -> (updated.getOrElse(writerTileId, Nil) ++ Seq(
        (p: Parameters) => LazyModule(new ISBWriterRoCC(writerOpcode, w, d)(p))
      )))
    }
    if (readerNeeded) {
      updated = updated + (readerTileId -> (updated.getOrElse(readerTileId, Nil) ++ Seq(
        (p: Parameters) => LazyModule(new ISBReaderRoCC(readerOpcode, w)(p))
      )))
    }

    updated
  }
})
