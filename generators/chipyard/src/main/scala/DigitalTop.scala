package chipyard

import chisel3._

import freechips.rocketchip.subsystem._
import freechips.rocketchip.system._
import org.chipsalliance.cde.config.Parameters
import freechips.rocketchip.devices.tilelink._

// ------------------------------------
// BOOM and/or Rocket Top Level Systems
// ------------------------------------

// DOC include start: DigitalTop
class DigitalTop(implicit p: Parameters) extends ChipyardSystem
  with testchipip.tsi.CanHavePeripheryUARTTSI // Enables optional UART-based TSI transport
  with testchipip.boot.CanHavePeripheryCustomBootPin // Enables optional custom boot pin
  with testchipip.boot.CanHavePeripheryBootAddrReg // Use programmable boot address register
  with testchipip.cosim.CanHaveTraceIO // Enables optionally adding trace IO
  with testchipip.soc.CanHaveBankedScratchpad // Enables optionally adding a banked scratchpad
  with testchipip.iceblk.CanHavePeripheryBlockDevice // Enables optionally adding the block device
  with testchipip.serdes.CanHavePeripheryTLSerial // Enables optionally adding the backing memory and serial adapter
  with testchipip.soc.CanHavePeripheryChipIdPin // Enables optional pin to set chip id for multi-chip configs
  with sifive.blocks.devices.i2c.HasPeripheryI2C // Enables optionally adding the sifive I2C
  with sifive.blocks.devices.pwm.HasPeripheryPWM // Enables optionally adding the sifive PWM
  with sifive.blocks.devices.uart.HasPeripheryUART // Enables optionally adding the sifive UART
  with sifive.blocks.devices.gpio.HasPeripheryGPIO // Enables optionally adding the sifive GPIOs
  with sifive.blocks.devices.spi.HasPeripherySPIFlash // Enables optionally adding the sifive SPI flash controller
  with sifive.blocks.devices.spi.HasPeripherySPI // Enables optionally adding the sifive SPI port
  with icenet.CanHavePeripheryIceNIC // Enables optionally adding the IceNIC for FireSim
  with chipyard.example.CanHavePeripheryInitZero // Enables optionally adding the initzero example widget
  
  //  Because DigitalTop is a LazyModule, this is where Diplomacy connections are created before Chisel hardware generation.
  // DigitalTop class contains the set of traits which parameterize and define the DigitalTop
  // The DigitalTop class includes the pre-elaboration code and also a lazy val to produce the module implementation
  
  // with this, we can optionally add the GCD module to the circuit
  // we can add the GCD module by adding it to a config
  // with chipyard.example.CanHavePeripheryGCD // Enables optionally adding the GCD example widget

  // with chisel_buffers.mmio_fifo.CanHavePeripheryPlusOne

  // with chisel_buffers.mmio_fifo.CanHavePeripheryISB

  // with chisel_buffers.sequential_mmio_fifo_v1.CanHavePeripherySequentialISB

  // with chisel_buffers.sequential_mmio_fifo_v2.CanHavePeripherySequentialISB

  with chisel_buffers.sequential_mmio_fifo_v3.CanHavePeripherySequentialISB

  
  with chipyard.example.CanHavePeripheryStreamingFIR // Enables optionally adding the DSPTools FIR example widget
  with chipyard.example.CanHavePeripheryStreamingPassthrough // Enables optionally adding the DSPTools streaming-passthrough example widget
  with nvidia.blocks.dla.CanHavePeripheryNVDLA // Enables optionally having an NVDLA
  with chipyard.clocking.HasChipyardPRCI // Use Chipyard reset/clock distribution
  with chipyard.clocking.CanHaveClockTap // Enables optionally adding a clock tap output port
  with fftgenerator.CanHavePeripheryFFT // Enables optionally having an MMIO-based FFT block
  with constellation.soc.CanHaveGlobalNoC // Support instantiating a global NoC interconnect
{
  override lazy val module = new DigitalTopModule(this)
}

// The DigitalTopModule class is the actual RTL that gets synthesized.
class DigitalTopModule[+L <: DigitalTop](l: L) extends ChipyardSystemModule(l)
  with sifive.blocks.devices.i2c.HasPeripheryI2CModuleImp
  with sifive.blocks.devices.pwm.HasPeripheryPWMModuleImp
  with sifive.blocks.devices.uart.HasPeripheryUARTModuleImp
  with sifive.blocks.devices.gpio.HasPeripheryGPIOModuleImp
  with sifive.blocks.devices.spi.HasPeripherySPIFlashModuleImp
  with sifive.blocks.devices.spi.HasPeripherySPIModuleImp
  with freechips.rocketchip.util.DontTouch
// DOC include end: DigitalTop


// Each peripheral (like GCD, UART, SPI) has two parts:

// 1. A LazyModule trait (e.g. CanHavePeripheryGCD)
// ➡ The LazyModule traits do the Diplomacy connection work:
//        Attach to buses
//        Set up parameters, address maps
//        Set up device tree entries

// 2. A Module implementation trait (e.g. HasPeripheryUARTModuleImp)
// ➡ The Module traits provide RTL / hardware-level connections:
//        Define ports
//        Define registers/wires that will appear in the final Verilog
//        Hook up actual I/O

// Think of DigitalTop as the blueprint architect, 
// designing the system and planning where things go (addresses, buses).
// And DigitalTopModule as the construction crew, 
// actually building the hardware based on that plan.

