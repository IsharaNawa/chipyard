package chipyard.example

import chisel3._
import chisel3.util._
import chisel3.experimental.{IntParam, BaseModule}
import freechips.rocketchip.amba.axi4._
import freechips.rocketchip.subsystem.BaseSubsystem
import org.chipsalliance.cde.config.{Parameters, Field, Config}
import freechips.rocketchip.diplomacy._
import freechips.rocketchip.regmapper.{HasRegMap, RegField}
import freechips.rocketchip.tilelink._
import freechips.rocketchip.util.UIntIsOneOf

// this is needed as part of using TLRegisterRouter or AXI4RegisterRouter
// DOC include start: GCD params
case class GCDParams(
  address: BigInt = 0x4000, // address of the peripheral
  width: Int = 32,          // width of the peripheral
  useAXI4: Boolean = false, // useAXI4 or tileLink
  useBlackBox: Boolean = true)  // use the blackbox, i.e. verilog code?
// DOC include end: GCD params

// DOC include start: GCD key
case object GCDKey extends Field[Option[GCDParams]](None)
// DOC include end: GCD key

// io of the actual hardware implementation module
class GCDIO(val w: Int) extends Bundle {
  val clock = Input(Clock())  // Clock signal – drives the internal state machine
  val reset = Input(Bool())   // 	Asynchronous or synchronous reset
  val input_ready = Output(Bool())  // GCD module says: "I'm ready to accept a new (x, y) input pair"
  val input_valid = Input(Bool())   // Producer (e.g., MMIO wrapper) says: "inputs x and y are valid and ready to be used"
  val x = Input(UInt(w.W))  // number 1 for GCD calculation
  val y = Input(UInt(w.W))  // number 2 for GCD calculation
  val output_ready = Input(Bool())  // Consumer (e.g., MMIO wrapper) says: "I’m ready to receive the GCD result"
  val output_valid = Output(Bool()) // GCD module says: "I’ve computed the result and it’s valid"
  val gcd = Output(UInt(w.W))  // output of the GCD calculation
  val busy = Output(Bool())    // Indicates the GCD unit is currently processing a request
}

// IO port of the TopModule
trait GCDTopIO extends Bundle {
  val gcd_busy = Output(Bool())
}

// IO ports of the inner module who actually has the hardware logic
trait HasGCDIO extends BaseModule {
  val w: Int
  val io = IO(new GCDIO(w))
}

// DOC include start: GCD blackbox
class GCDMMIOBlackBox(val w: Int) extends BlackBox(Map("WIDTH" -> IntParam(w))) with HasBlackBoxResource
  with HasGCDIO
{
  addResource("/vsrc/GCDMMIOBlackBox.v")
}
// DOC include end: GCD blackbox

// DOC include start: GCD chisel
// actual hardware implementation
// according to the eucledian algorithm
class GCDMMIOChiselModule(val w: Int) extends Module
  with HasGCDIO
{
  // calculates the GCD using euclidean algorithm

  val s_idle :: s_run :: s_done :: Nil = Enum(3)

  val state = RegInit(s_idle)
  val tmp   = Reg(UInt(w.W))
  val gcd   = Reg(UInt(w.W))

  io.input_ready := state === s_idle
  io.output_valid := state === s_done
  io.gcd := gcd

  when (state === s_idle && io.input_valid) {
    state := s_run
  } .elsewhen (state === s_run && tmp === 0.U) {
    state := s_done
  } .elsewhen (state === s_done && io.output_ready) {
    state := s_idle
  }

  when (state === s_idle && io.input_valid) {
    gcd := io.x
    tmp := io.y
  } .elsewhen (state === s_run) {
    when (gcd > tmp) {
      gcd := gcd - tmp
    } .otherwise {
      tmp := tmp - gcd
    }
  }

  io.busy := state =/= s_idle
}
// DOC include end: GCD chisel

// DOC include start: GCD instance regmap

// outer module of the gcd actual hardware
// makes necessary connections to the inner module and
// maps how the addresses should behave
trait GCDModule extends HasRegMap {
  val io: GCDTopIO

  implicit val p: Parameters
  def params: GCDParams
  val clock: Clock
  val reset: Reset


  // How many clock cycles in a PWM cycle?
  val x = Reg(UInt(params.width.W))
  val y = Wire(new DecoupledIO(UInt(params.width.W)))
  val gcd = Wire(new DecoupledIO(UInt(params.width.W)))
  val status = Wire(UInt(2.W))

  val impl = if (params.useBlackBox) {
    Module(new GCDMMIOBlackBox(params.width))
  } else {
    Module(new GCDMMIOChiselModule(params.width))
  }

  impl.io.clock := clock
  impl.io.reset := reset.asBool

  impl.io.x := x
  impl.io.y := y.bits
  impl.io.input_valid := y.valid
  y.ready := impl.io.input_ready

  gcd.bits := impl.io.gcd
  gcd.valid := impl.io.output_valid
  impl.io.output_ready := gcd.ready

  status := Cat(impl.io.input_ready, impl.io.output_valid)
  io.gcd_busy := impl.io.busy

  regmap(

    // here .r means read-only
    // we can aslo specify the width of the register
    0x00 -> Seq(
      RegField.r(2, status)), // a read-only register capturing current status

    // here .w means write-only
    0x04 -> Seq(
      RegField.w(params.width, x)), // a plain, write-only register

    // here .w means write-only
    // since y is decoupled io, y.valid is asserted when that register is written
    0x08 -> Seq(
      RegField.w(params.width, y)), // write-only, y.valid is set on write

    // this is a read-only register
    // important to notice is that when reading is done, the ready signal will automatically will be set
    // this happens because gcd is connected with decoupled handshake
    0x0C -> Seq(
      RegField.r(params.width, gcd))) // read-only, gcd.ready is set on read

  // Since the ready/valid signals of y are connected to the input_ready and 
  // input_valid signals of the GCD module, respectively, this register map 
  // and glue logic has the effect of triggering the GCD algorithm when y is written. 
  // Therefore, the algorithm is set up by first writing x and then performing 
  // a triggering write to y. Polling can be used for status checks.
}
// DOC include end: GCD instance regmap

// DOC include start: GCD router
// creating the tile
// params holds the configs such as base address and width
// beatBytes : typically 4,8 which is the size of TileLink bus beats
class GCDTL(params: GCDParams, beatBytes: Int)(implicit p: Parameters)
  extends TLRegisterRouter(

    // GCDTL is itself a LazyModule
    // Because GCDTL extends TLRegisterRouter, and TLRegisterRouter is a LazyModule,
    // 👉 GCDTL is also a LazyModule.
    
    // MMIO base address
    params.address, 
    
    // label for the peripheral
    "gcd",
    
    // Device compatibility string, goes into device tree(DTS)
    // A Device Tree is a data structure used by many operating systems (especially Linux) to describe the hardware layout of a system.
    // It tells the OS what peripherals exist, where they are mapped, and how to talk to them.
    Seq("ucbbar,gcd"),
    
    beatBytes = beatBytes)(
      // plugin the io bundle
      new TLRegBundle(params, _) with GCDTopIO)(

        // plug in the module
      new TLRegModule(params, _, _) with GCDModule)

class GCDAXI4(params: GCDParams, beatBytes: Int)(implicit p: Parameters)
  extends AXI4RegisterRouter(
    params.address,
    beatBytes=beatBytes)(
      new AXI4RegBundle(params, _) with GCDTopIO)(
      new AXI4RegModule(params, _, _) with GCDModule)
// DOC include end: GCD router

// DOC include start: GCD lazy trait

// connecting the Tile(based on TileLink or AXI4)
// to the MIMO crossbar
// this trait should be enabled in the config
// This is a cake pattern trait that can only be mixed into something that extends BaseSubsystem.
// BaseSubsystem represents the base SoC without peripherals
trait CanHavePeripheryGCD { this: BaseSubsystem =>

  // A string label for the port connection. It will tag this connection in the Diplomacy graph and device tree.
  private val portName = "gcd"

  // Look at the config parameters (p(GCDKey))
  val gcd_busy = p(GCDKey) match {

    // If GCDKey is Some(params), it means we want to include a GCD peripheral
    case Some(params) => {

      // select the gcd module based on the parameterized value
      // defined in params
      val gcd = if (params.useAXI4) {

        // Instantiate GCD AXI4 peripheral as a LazyModule and place it on pbus
        val gcd = pbus { LazyModule(new GCDAXI4(params, pbus.beatBytes)(p)) }

        // hook it up to pbus
        pbus.coupleTo(portName) {
          // Buffer them for AXI4 protocol
          // Convert TL to AXI4
          // toVariableWidthSlave doesn't use holdFirstDeny, which TLToAXI4() needsx
          // Requests → fragment into correct sizes
          // we cant seperate below lines
          gcd.node :=
          AXI4Buffer () :=
          TLToAXI4 () :=
          TLFragmenter(pbus.beatBytes, pbus.blockBytes, holdFirstDeny = true) := _
        }

        // return gcd as a AXI4 connected tile
        gcd
      } else {

        // if the params is selected for TileLink
        val gcd = pbus { 
          // Instantiate GCDTL peripheral as LazyModule.
          LazyModule(new GCDTL(params, pbus.beatBytes)(p)) 
        }

        // Hook it up through a TLFragmenter to handle request splitting
        pbus.coupleTo(portName) { 
          gcd.node := TLFragmenter(pbus.beatBytes, pbus.blockBytes) := _ 
        }

        // return gcd as a TileLink Tile
        gcd
      }

      // Add gcd_busy signal
      val pbus_io = pbus { InModuleBody {

        // Create a busy signal output
        val busy = IO(Output(Bool()))

        // Drive it from gcd.module.io.gcd_busy
        busy := gcd.module.io.gcd_busy

        // return the busy signal
        busy
      }}

      // Create a final gcd_busy output (outside of pbus block)
      val gcd_busy = InModuleBody {
        val busy = IO(Output(Bool())).suggestName("gcd_busy")
        busy := pbus_io
        busy
      }

      // Return this signal if GCD was included
      Some(gcd_busy)
    }

    // If GCD not enabled, no gcd_busy signal
    case None => None
  }
}
// DOC include end: GCD lazy trait

// DOC include start: GCD config fragment

// This defines a Config fragment that:
// When the Rocket-Chip generator asks for GCDKey, it will return:
// Some(GCDParams(useAXI4 = ..., useBlackBox = ...))
// This GCDParams tells the system:
// Whether to use TileLink or AXI4 (useAXI4)
// Whether to instantiate a blackbox or Chisel version of the GCD hardware (useBlackBox)
// ✅ This fragment plugs into Rocket Chip's parameter system.
class WithGCD(useAXI4: Boolean = false, useBlackBox: Boolean = false) extends Config((site, here, up) => {
  case GCDKey => Some(GCDParams(useAXI4 = useAXI4, useBlackBox = useBlackBox))
})
// DOC include end: GCD config fragment
