/*
 * RoCC-based Inter-Stage Buffer (ISB) for point-to-point core-to-core communication.
 *
 * Replaces the MMIO ISB path (Core → DCache → SystemBus → PBUS → ISB → return)
 * with a direct RoCC path (Core → RoCC → tile boundary → Queue → tile boundary → RoCC → Core).
 *
 * Uses custom diplomacy nodes following the ReRoCC pattern so that
 * the diplomacy framework automatically threads IO ports through tile boundaries.
 * No modifications to rocket-chip are needed.
 */

package chipyard.example

import chisel3._
import chisel3.util._
import chisel3.experimental.SourceInfo

import org.chipsalliance.cde.config.{Field, Parameters, Config}
import freechips.rocketchip.diplomacy._
import freechips.rocketchip.tile._
import freechips.rocketchip.rocket._
import freechips.rocketchip.subsystem._
import freechips.rocketchip.prci._
import chipyard.config.MultiRoCCKey

// ============================================================================
// ISB Diplomacy Protocol
// ============================================================================
// This section defines a custom diplomacy protocol for Inter-Stage Buffer (ISB).
// Diplomacy is Rocket Chip's framework for managing hardware interfaces and
// automatically routing them through hierarchical system boundaries (e.g., tile boundaries).
// By using custom diplomacy nodes, we avoid manual modifications to rocket-chip.

// Bundle parameters define the data type of the diplomacy protocol
case class ISBBundleParams(width: Int)

// ISBBundle is the actual data structure transmitted over the diplomacy link
class ISBBundle(val params: ISBBundleParams) extends Bundle {
  val data = new DecoupledIO(UInt(params.width.W))  // Valid/ready decoupled data pathway
}

// Source nodes send data; sinks receive. Both only need to know their width.
case class ISBSourcePortParams(width: Int)  // Writer-side parameters
case class ISBSinkPortParams(width: Int)    // Reader-side parameters

// Edge parameters combine source and sink params at the connection point
case class ISBEdgeParams(
  srcParams: ISBSourcePortParams,
  snkParams: ISBSinkPortParams
) {
  val bundle = ISBBundleParams(srcParams.width)  // Both sides must agree on width
}

// ISBImp is the domainacy implementation that defines how connections work.
// It specifies: how to create edge params, how to generate bundles, and visualization.
object ISBImp extends SimpleNodeImp[ISBSourcePortParams, ISBSinkPortParams, ISBEdgeParams, ISBBundle] {
  def edge(pd: ISBSourcePortParams, pu: ISBSinkPortParams, p: Parameters, sourceInfo: SourceInfo) =
    ISBEdgeParams(pd, pu)
  def bundle(e: ISBEdgeParams) = new ISBBundle(e.bundle)
  def render(e: ISBEdgeParams) = RenderedEdge(colour = "#00cc00", label = "ISB")  // Green for visualization
}

// Concrete node types for diplomacy graph construction
case class ISBSourceNode(params: ISBSourcePortParams)(implicit valName: ValName)
  extends SourceNode(ISBImp)(Seq(params))  // Creates outgoing ISB data port

case class ISBSinkNode(params: ISBSinkPortParams)(implicit valName: ValName)
  extends SinkNode(ISBImp)(Seq(params))    // Creates incoming ISB data port

// Adapter nodes are bidirectional and used inside modules like ISBQueue
case class ISBAdapterNode()(implicit valName: ValName)
  extends AdapterNode(ISBImp)({ s => s }, { s => s })  // Identity mapping on both sides

// ============================================================================
// ISBQueue: FIFO adapter that sits at the subsystem level between two tiles
// ============================================================================
// This module instances a standard Chisel Queue to buffer data flowing between
// two RoCC accelerators across tile boundaries. The diplomacy nodes handle
// automatic port creation at tile boundaries.

class ISBQueue(depth: Int)(implicit p: Parameters) extends ClockSinkDomain(ClockSinkParameters())(p) {
  val node = ISBAdapterNode()

  override lazy val module = new ISBQueueImpl
  class ISBQueueImpl extends Impl {
    withClockAndReset(clock, reset) {
      // Connect incoming diplomacy port -> Queue -> outgoing diplomacy port
      (node.in zip node.out).foreach { case ((in, _), (out, _)) =>
        val queue = Module(new Queue(UInt(in.params.width.W), depth))
        queue.io.enq <> in.data   // Enqueue from upstream tile
        out.data <> queue.io.deq  // Dequeue to downstream tile
      }
    }
  }
}

// Factory object for easy instantiation
object ISBQueue {
  def apply(depth: Int)(implicit p: Parameters): ISBAdapterNode = {
    val q = LazyModule(new ISBQueue(depth))
    q.node  // Return the diplomacy node for external wiring
  }
}

// ============================================================================
// ISBRoCC: RoCC accelerator with ISB diplomacy nodes
// ============================================================================
// This is a custom RoCC accelerator that supports point-to-point inter-tile
// communication via diplomacy. It can optionally function as a writer (sender)
// and/or reader (receiver) of data through custom ISB diplomacy ports.

case class ISBRoCCParams(
  hasWriter: Boolean = true,    // Can this core send data downstream?
  hasReader: Boolean = true,    // Can this core receive data from upstream?
  width: Int = 64,              // Data width in bits
  depth: Int = 16,              // FIFO depth (in ISBQueue)
  opcodes: OpcodeSet = OpcodeSet.custom0  // RoCC instruction opcode
)

class ISBRoCC(val isbParams: ISBRoCCParams)(implicit p: Parameters)
    extends LazyRoCC(isbParams.opcodes, nPTWPorts = 0) {

  override def shouldBeInlined = false  // Keep as separate module for clarity

  // Create optional diplomacy nodes for sending and receiving ISB data
  val isbSourceNode: Option[ISBSourceNode] =
    if (isbParams.hasWriter) Some(ISBSourceNode(ISBSourcePortParams(isbParams.width))) else None

  val isbSinkNode: Option[ISBSinkNode] =
    if (isbParams.hasReader) Some(ISBSinkNode(ISBSinkPortParams(isbParams.width))) else None

  override lazy val module = new ISBRoCCModuleImp(this)
}

class ISBRoCCModuleImp(outer: ISBRoCC)(implicit p: Parameters)
    extends LazyRoCCModuleImp(outer) {

  // Extract hardware interfaces from diplomacy nodes (if they exist)
  val isbOut: Option[ISBBundle] = outer.isbSourceNode.map(_.out(0)._1)  // Outgoing ISB channel
  val isbIn:  Option[ISBBundle] = outer.isbSinkNode.map(_.in(0)._1)      // Incoming ISB channel

  // Buffer RoCC commands and decode instruction function code
  val cmd = Queue(io.cmd, 1)
  val funct = cmd.bits.inst.funct
  // Four custom instructions: write, read, write status (inquiry), read status (inquiry)
  val doWrite       = funct === 0.U  // Enqueue rs1 to downstream FIFO
  val doRead        = funct === 1.U  // Dequeue from upstream FIFO
  val doWriteStatus = funct === 2.U  // Check if downstream is ready
  val doReadStatus  = funct === 3.U  // Check if upstream has data

  // Initialize all outputs to safe defaults (disabled/zero)
  isbOut.foreach { out =>
    out.data.valid := false.B  // No data being sent
    out.data.bits  := 0.U
  }
  isbIn.foreach { in =>
    in.data.ready := false.B   // Not ready to accept data
  }
  io.resp.valid     := false.B  // No response by default
  io.resp.bits.rd   := cmd.bits.inst.rd
  io.resp.bits.data := 0.U

  // Ready/valid status from ISB channels (with safe defaults for unused nodes)
  // If no writer exists, assume always ready (can't block a write that won't happen)
  val enqReady = isbOut.map(_.data.ready).getOrElse(true.B)
  // If no reader exists, assume never valid (can't satisfy a read that won't happen)
  val deqValid = isbIn.map(_.data.valid).getOrElse(false.B)

  // Define when the RoCC accelerator should stall
  val stallWrite = doWrite && !enqReady           // Writing when downstream not ready
  val stallRead  = doRead && (!deqValid || !io.resp.ready)  // Reading when no data or can't respond
  val stallResp  = (doWriteStatus || doReadStatus) && !io.resp.ready  // Status query can't respond

  // Command is accepted only when valid and not stalling
  cmd.ready := cmd.valid && !stallWrite && !stallRead && !stallResp

  // ================================
  // Instruction Execution Logic
  // ================================

  // funct=0: WRITE — enqueue rs1 value into downstream FIFO
  when (cmd.valid && doWrite) {
    isbOut.foreach { out =>
      out.data.valid := true.B        // Signal data is available
      out.data.bits  := cmd.bits.rs1  // Use rs1 register as payload
    }
  }

  // funct=1: READ — dequeue from upstream FIFO, return data via rd register
  when (cmd.valid && doRead) {
    isbIn.foreach { in =>
      in.data.ready := io.resp.ready  // Accept data only when we can respond
    }
    io.resp.valid     := deqValid                           // Valid response iff data available
    io.resp.bits.data := isbIn.map(_.data.bits).getOrElse(0.U)  // Return dequeued data
  }

  // funct=2: WRITE STATUS — return downstream FIFO's ready signal (non-blocking poll)
  when (cmd.valid && doWriteStatus) {
    io.resp.valid     := true.B       // Always returns immediately
    io.resp.bits.data := enqReady     // 1 = can write, 0 = downstream full
  }

  // funct=3: READ STATUS — return upstream FIFO's valid signal (non-blocking poll)
  when (cmd.valid && doReadStatus) {
    io.resp.valid     := true.B       // Always returns immediately
    io.resp.bits.data := deqValid     // 1 = data available, 0 = upstream empty
  }

  // Interface signals
  io.busy := cmd.valid      // Report busy while processing a command
  io.interrupt := false.B   // No interrupts generated
  io.mem.req.valid := false.B  // No memory requests (this accelerator doesn't access memory)
}

// ============================================================================
// Subsystem integration trait
// ============================================================================
// This trait is mixed into the top-level subsystem (e.g., BaseSubsystem) to
// orchestrate the wiring of ISBRoCC instances across multiple tiles.

trait CanHaveRoCCISBPipeline { this: BaseSubsystem with InstantiatesHierarchicalElements =>

  // Collect all ISBRoCC instances embedded in RocketTile roccs, sorted by tileId
  // This ensures deterministic ordering for pipelining adjacent tiles
  private val isbRoCCs: Seq[(Int, BaseTile, ISBRoCC)] = totalTiles.toSeq.sortBy(_._1).flatMap {
    case (id, tile) =>
      tile match {
        case r: RocketTile =>
          r.roccs.collect { case isb: ISBRoCC => (id, tile, isb) }
        case _ => Nil
      }
  }

  // Pipeline adjacent tiles: each writer tile's source connects to next reader tile's sink
  // This creates a linear pipeline: tile0 -> queue -> tile1 -> queue -> tile2, etc.
  isbRoCCs.sliding(2).zipWithIndex.foreach {
    case (Seq((_, _, wRoCC), (_, _, rRoCC)), idx) =>
      (wRoCC.isbSourceNode, rRoCC.isbSinkNode) match {
        case (Some(srcNode), Some(snkNode)) =>
          val depth = wRoCC.isbParams.depth
          // Create ISBQueue synchronized to the system bus clock domain
          val queue = locateTLBusWrapper(SBUS).generateSynchronousDomain {
            LazyModule(new ISBQueue(depth))
          }
          queue.suggestName(s"isb_queue_$idx")
          queue.clockNode := locateTLBusWrapper(SBUS).fixedClockNode
          
          // Diplomacy connection: writer -> queue -> reader
          // Diplomacy automatically generates cross-tile boundary ports because
          // srcNode is in the writer's tile context and snkNode is in the reader's.
          snkNode := queue.node := srcNode
        case _ =>
          // Skip if either tile doesn't have both writer and reader
      }
    case _ =>
      // Skip incomplete sliding windows at the end
  }
}

// ============================================================================
// Config fragments
// ============================================================================
// Configuration mixin to instantiate ISBRoCC on each core with appropriate
// writer/reader configuration for pipeline topology.

class WithRoCCISBPipeline(
  nCores: Int,
  width: Int = 64,
  depth: Int = 16,
  opcodes: OpcodeSet = OpcodeSet.custom0
) extends Config((site, here, up) => {
  case MultiRoCCKey => up(MultiRoCCKey, site) ++ (0 until nCores).map { i =>
    // Create linear pipeline: cores 0->1->2->...->nCores-1
    val hasWriter = i < nCores - 1  // Cores 0 to nCores-2 send downstream
    val hasReader = i > 0           // Cores 1 to nCores-1 receive from upstream
    val params = ISBRoCCParams(
      hasWriter = hasWriter,
      hasReader = hasReader,
      width = width,
      depth = depth,
      opcodes = opcodes
    )
    // Create factory function that instantiates ISBRoCC for this core
