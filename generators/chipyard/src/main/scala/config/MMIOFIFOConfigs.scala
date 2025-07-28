package chipyard

import org.chipsalliance.cde.config.{Config}
import freechips.rocketchip.diplomacy.{AsynchronousCrossing}

// class PlusOneTLRocketConfig extends Config(
//   new chisel_buffers.mmio_fifo.WithPlusOne(variation=true) ++          // Use Plus One Chisel, connect Tilelink
//   new freechips.rocketchip.subsystem.WithNBigCores(1) ++
//   new chipyard.config.AbstractConfig)

// class PlusOneTLMultiRocketConfig extends Config(
//   new chisel_buffers.mmio_fifo.WithPlusOne(variation=true) ++          // Use Plus One Chisel, connect Tilelink
//   new freechips.rocketchip.subsystem.WithNBigCores(3) ++
//   new chipyard.config.AbstractConfig)

// class PlusOneTL8BitRocketConfig extends Config(
//   new chisel_buffers.mmio_fifo.WithPlusOne(variation=true,width=8) ++          // Use Plus One Chisel, connect Tilelink
//   new freechips.rocketchip.subsystem.WithNBigCores(3) ++
//   new chipyard.config.AbstractConfig)

// // doesnt work yet! infinite looping in the while section in the c code
// class PlusOne8BitDiffAddrTLRocketConfig extends Config(
//   // new chipyard.example.WithGCD(useAXI4=false, useBlackBox=false) ++ 
//   new chisel_buffers.mmio_fifo.WithPlusOne(variation=true,width=8,address=0x4000) ++
//   new chisel_buffers.mmio_fifo.WithPlusOne(variation=true,width=8,address=0x4010) ++          // Use Plus One Chisel, connect Tilelink
//   new freechips.rocketchip.subsystem.WithNBigCores(1) ++
//   new chipyard.config.AbstractConfig)

// class BasicISBRocketConfig extends Config(
//   new chisel_buffers.mmio_fifo.WithISB(isRegFile=true) ++          // Use ISB Chisel, connect Tilelink
//   new freechips.rocketchip.subsystem.WithNBigCores(1) ++
//   new chipyard.config.AbstractConfig)

// class BasicSequentialISBV1RocketConfig extends Config(
//   new chisel_buffers.sequential_mmio_fifo_v1.WithSequentialISB(isRegFile=true) ++          // Use SequentialISB Chisel, connect Tilelink
//   new freechips.rocketchip.subsystem.WithNBigCores(1) ++
//   new chipyard.config.AbstractConfig)

// class BasicSequentialISBV2RocketConfig extends Config(
//   new chisel_buffers.sequential_mmio_fifo_v2.WithSequentialISB(isRegFile=true) ++          // Use SequentialISB Chisel, connect Tilelink
//   new freechips.rocketchip.subsystem.WithNBigCores(1) ++
//   new chipyard.config.AbstractConfig)

class BasicSequentialISBV3RocketConfig extends Config(
  new chisel_buffers.sequential_mmio_fifo_v3.WithSequentialISB(isRegFile=true) ++          // Use SequentialISB Chisel, connect Tilelink
  new freechips.rocketchip.subsystem.WithNBigCores(1) ++
  new chipyard.config.AbstractConfig)

