/* 
  This is the version 4
  - This one is the most optimized and latest. Removed sequential number behaviour.
  - Ready valid signals are handled by the hardware automatically. User doesnt have to do this manually.
  - This has additional counter for counting the number of items in the fifo.
  - The counter is of 30 bits. Therefore maximum capacity of the FIFO is 2^30.
  - This is the latest version.
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
import freechips.rocketchip.regmapper.{HasRegMap, RegField}
import freechips.rocketchip.tilelink._
import freechips.rocketchip.util.UIntIsOneOf

// this is needed as part of using TLRegisterRouter
// DOC include start: ISB params
case class SequentialISBParams(
    isRegFile : Boolean = true,     // type of the memory element
    address : BigInt = 0x4000,      // address of the peripheral
    width: Int = 32,                // width of the peripheral
    depth: Int = 10                 // size of the buffer
    // ,variation: Int = 0          // variation of the buffer, (RegFifo : 0, MemFifo : 1, ...)
)
// DOC include end: ISB params

// DOC include start: ISB key
case object SequentialISBKey extends Field[List[SequentialISBParams]](Nil)
// DOC include end: ISB key

class SequentialEnqueChannel[T <: Data](private val gen: T)  extends Bundle{
  val bits = Input(gen)
  val valid = Input(Bool())
  val fifo_ready = Output(Bool())
}

class SequentialDequeChannel[T <: Data](private val gen: T)  extends Bundle{
  val bits = Output(gen)
  val valid = Output(Bool())
  val consumer_ready = Input(Bool())
}

// io of the actual hardware implementation module
class SequentialFifoIO[T <: Data](private val gen: T) extends Bundle {
    val clock = Input(Clock())  // Clock signal – drives the internal state machine
    val reset = Input(Bool())   // 	Asynchronous or synchronous reset
    val enq = new SequentialEnqueChannel(gen)   // Decoupled handshaked producer side
    val deq = new SequentialDequeChannel(gen)   // Decoupled hanshaked consumer side
    val count = Output(UInt(30.W))      // to get the item count
}

// IO ports of the inner module who actually has the hardware logic
trait HasSequentialISBIO extends BaseModule {
  val w: Int
  val io = IO(new SequentialFifoIO(UInt(w.W)))
}

// circular buffer
// enqueue and dequeue operations are based on write and read pointers
// memeory is created using registers(hardware expensive)
class SequentialRegFifo(val w: Int, depth: Int) extends Module with HasSequentialISBIO {

  // counter element
  // this will count to the depth
  // when the count is reached the depth, it will be reset to 0
  def counter(depth: Int, incr: Bool): (UInt, UInt) = {

    // create the register for counting
    val cntReg = RegInit(0.U(log2Ceil(depth).W))

    // get the next value
    // if the count has reached (depth-1), reset it to 0 , otherwise add 1 to the current count value
    val nextVal = Mux(cntReg === (depth-1).U, 0.U, cntReg + 1.U)

    // if the incr value is true.B only, connect the next value to the counting register
    when (incr) {
      cntReg := nextVal
    }

    // return both the current counting value and the next value
    (cntReg, nextVal)
  }

  // create the memory using registers
  // the memory should have depth number of units with the type of gen
  val memReg = Reg(Vec(depth, UInt(w.W)))

  // for incrementing the read pointer, initially it is false
  val incrRead = WireInit(false.B)

  // for incrementing the write pointer, initially it is false
  val incrWrite = WireInit(false.B)

  // get the current counter value and next reading pointer position
  val (readPtr, nextRead) = counter(depth, incrRead)

  // get the current counter value and next writing pointer position
  val (writePtr, nextWrite) = counter(depth, incrWrite)

  // need to maintain two states, empty buffer and full buffer
  val emptyReg = RegInit(true.B)
  val fullReg = RegInit(false.B)

  // need to count the number of elements in the fifo
  val itemCounter = RegInit(0.U(log2Ceil(depth).W))

  // if the producer has valid data and buffer is not full
  when (!fullReg && io.enq.valid) {

    // save the data in the memory
    memReg(writePtr) := io.enq.bits

    // set the empty register to false , since we have new data
    emptyReg := false.B

    // if the next write pointer is save as the read pointer, that means the buffer is full
    // therefore set the fullReg
    fullReg := nextWrite === readPtr

    // set the incrWrite to true, since we need to increment the writing pointer
    incrWrite := true.B

    // increment the counter
    // No need of roll over to 0 because of fullReg flag
    itemCounter := itemCounter + 1.U 
  }

  //if the consumer is ready to accept data and buffer has data
  when (!emptyReg && io.deq.consumer_ready) {

    // since we are going to give data, set the fullReg to false
    fullReg := false.B

    // if the nextRead and the writePtr are same, that means the buffer is empty
    emptyReg := nextRead === writePtr

    // set the incrRead to true, since we need to increment the read pointer
    incrRead := true.B

    // decrement the counter
    // No need of roll over to 0 because of emptyReg flag
    itemCounter := itemCounter - 1.U
  }

  // connect the data as the output of the module
  io.deq.bits := memReg(readPtr)

  // buffer is ready if it is not full
  io.enq.fifo_ready := !fullReg

  // data is not valid if the buffer does not have data
  io.deq.valid := !emptyReg

  // connect the itemCounter to the counter io
  io.count := itemCounter
}

class SequentialISBTopIO extends Bundle{
    val sequential_busy = Output(Bool())
    // val deq_busy = Output(Bool())
}

trait HasSequentialISBTopIO {
  def io: SequentialISBTopIO
}

class SequentialISBTL(params:SequentialISBParams,beatBytes:Int)(implicit p: Parameters) extends ClockSinkDomain(ClockSinkParameters())(p){

    val device = new SimpleDevice("sequential_isb", Seq("ishara,sequential_isb"))
    val node = TLRegisterNode(Seq(AddressSet(params.address, 4096-1)), device, "reg/control", beatBytes=beatBytes)

    override lazy val module = new ISBImpl
    class ISBImpl extends Impl with HasSequentialISBTopIO {
        val io = IO(new SequentialISBTopIO)
        withClockAndReset(clock, reset) {
        // How many clock cycles in a PWM cycle?
        // val x = Reg(UInt(params.width.W))
        // val y = Wire(new DecoupledIO(UInt(params.width.W)))
        // val gcd = Wire(new DecoupledIO(UInt(params.width.W)))
        // val status = Wire(UInt(2.W))

        val enq = Wire(new DecoupledIO(UInt(params.width.W)))
        val deq = Wire(new DecoupledIO(UInt(params.width.W)))
        val status = Wire(UInt(32.W))   

        // val impl_io = if (params.useBlackBox) {
        //     val impl = Module(new GCDMMIOBlackBox(params.width))
        //     impl.io
        // } else {
        //     val impl = Module(new GCDMMIOChiselModule(params.width))
        //     impl.io
        // }

        val impl = if(params.isRegFile){
            Module(new SequentialRegFifo(params.width,params.depth))
        }else{
            // TODO : change this according to other variations
            Module(new SequentialRegFifo(params.width,params.depth))
        }

        // impl_io.clock := clock
        // impl_io.reset := reset.asBool

        // impl_io.x := x
        // impl_io.y := y.bits
        // impl_io.input_valid := y.valid
        // y.ready := impl_io.input_ready

        // gcd.bits := impl_io.gcd
        // gcd.valid := impl_io.output_valid
        // impl_io.output_ready := gcd.ready

        // status := Cat(impl_io.input_ready, impl_io.output_valid)
        // io.gcd_busy := impl_io.busy

        impl.io.clock := clock
        impl.io.reset := reset.asBool

        impl.io.enq.bits := enq.bits
        impl.io.enq.valid := enq.valid
        enq.ready := impl.io.enq.fifo_ready

        deq.bits := impl.io.deq.bits
        deq.valid := impl.io.deq.valid
        impl.io.deq.consumer_ready := deq.ready


        status := Cat(impl.io.count,impl.io.enq.fifo_ready,impl.io.deq.valid)
        io.sequential_busy := true.B

    // // DOC include start: GCD instance regmap
    //     node.regmap(
    //         0x00 -> Seq(
    //         RegField.r(2, status)), // a read-only register capturing current status
    //         0x04 -> Seq(
    //         RegField.w(params.width, x)), // a plain, write-only register
    //         0x08 -> Seq(
    //         RegField.w(params.width, y)), // write-only, y.valid is set on write
    //         0x0C -> Seq(
    //         RegField.r(params.width, gcd))) // read-only, gcd.ready is set on read
    // // DOC include end: GCD instance regmap

        node.regmap(
            0x00 -> Seq(
                RegField.r(32,status)
            ),
            0x04 -> Seq(
                RegField.w(params.width,enq)
            ),

            0x08 -> Seq(
                RegField.r(params.width,deq)
            )
        )
        }  
    }
}

// trait SequentialISBModule extends HasRegMap{

//     val io : SequentialISBTopIO

//     implicit val p : Parameters
//     def params : SequentialISBParams
//     val clock : Clock
//     val reset : Reset

//     val enq = Wire(new DecoupledIO(UInt(params.width.W)))
//     val deq = Wire(new DecoupledIO(UInt(params.width.W)))
//     val status = Wire(UInt(32.W))

//     val impl = if(params.isRegFile){
//         Module(new SequentialRegFifo(params.width,params.depth))
//     }else{
//         // TODO : change this according to other variations
//         Module(new SequentialRegFifo(params.width,params.depth))
//     }

//     impl.io.clock := clock
//     impl.io.reset := reset.asBool

//     impl.io.enq.bits := enq.bits
//     impl.io.enq.valid := enq.valid
//     enq.ready := impl.io.enq.fifo_ready

//     deq.bits := impl.io.deq.bits
//     deq.valid := impl.io.deq.valid
//     impl.io.deq.consumer_ready := deq.ready


//     status := Cat(impl.io.count,impl.io.enq.fifo_ready,impl.io.deq.valid)
//     io.sequential_busy := true.B
    
//     regmap(
//         0x00 -> Seq(
//             RegField.r(32,status)
//         ),
//         0x04 -> Seq(
//             RegField.w(params.width,enq)
//         ),

//         0x08 -> Seq(
//             RegField.r(params.width,deq)
//         )
//     )
// }

// class SequentialISBTL(params:SequentialISBParams,beatBytes:Int)(implicit p: Parameters)
// extends  TLRegisterRouter(

//     params.address,

//     "sequential_isb",

//     Seq("ishara,sequential_isb"),

//     beatBytes = beatBytes)(
//         new TLRegBundle(params, _) with SequentialISBTopIO)(
//         new TLRegModule(params,_,_) with SequentialISBModule)


trait CanHavePeripherySequentialISB { this: BaseSubsystem =>

  private val pbus = locateTLBusWrapper(PBUS)

  // Create multiple ISB instances based on the list
  val sequential_busy = p(SequentialISBKey).zipWithIndex.map { case (params, i) =>
    val portName = s"sequential_isb_$i"

    val sequential_isb = if (params.isRegFile) {
      val sequential_isb = LazyModule(new SequentialISBTL(params, pbus.beatBytes)(p))
      sequential_isb.clockNode := pbus.fixedClockNode
      pbus.coupleTo(portName) { sequential_isb.node := TLFragmenter(pbus.beatBytes, pbus.blockBytes) := _ }
      sequential_isb
    } else {
      val sequential_isb = LazyModule(new SequentialISBTL(params, pbus.beatBytes)(p))
      sequential_isb.clockNode := pbus.fixedClockNode
      pbus.coupleTo(portName) { sequential_isb.node := TLFragmenter(pbus.beatBytes, pbus.blockBytes) := _ }
      sequential_isb
    }

    val sequential_busy = InModuleBody {
      val busy = IO(Output(Bool())).suggestName(s"sequential_busy_$i")
      busy := sequential_isb.module.io.sequential_busy
      busy
    }

    sequential_busy
  }
}

class WithSequentialISB(isRegFile : Boolean = true, address : BigInt = 0x4000,width: Int = 32,depth: Int = 10) extends Config((site, here, up) => {
  case SequentialISBKey => up(SequentialISBKey) ++ List(SequentialISBParams(isRegFile=isRegFile,address=address,width=width,depth=depth))
})
