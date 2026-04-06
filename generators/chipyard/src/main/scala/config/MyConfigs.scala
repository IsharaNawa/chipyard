package chipyard

import org.chipsalliance.cde.config.{Config}

// ------------------------------
// Start : Configs with Sequential ISB accelerators
// ------------------------------

class BasicSequentialRocketConfig extends Config(
  new chipyard.example.WithSequentialISB(isRegFile=true,address=0x4000) ++          // Use SequentialISB Chisel, connect Tilelink
  new freechips.rocketchip.rocket.WithNBigCores(1) ++
  new chipyard.config.AbstractConfig)

class DualCoreSequentialRocketConfig extends Config(
  new chipyard.example.WithSequentialISB(isRegFile=true,address=0x4000) ++          // Use SequentialISB Chisel, connect Tilelink
  new freechips.rocketchip.rocket.WithNBigCores(2) ++
  new chipyard.config.AbstractConfig)

class TriCoreTwoSequential64RocketConfig extends Config(
  new chipyard.example.WithSequentialISB(isRegFile=true,address=0x4000,depth=64) ++          // Use SequentialISB Chisel, connect Tilelink
  new chipyard.example.WithSequentialISB(isRegFile=true,address=0x5000,depth=64) ++ 
  new freechips.rocketchip.rocket.WithNBigCores(3) ++
  new chipyard.config.AbstractConfig)

class QuadRocketCoreWithThreeISB64FanOffConfig extends Config(
  new chipyard.example.WithSequentialISB(isRegFile=true,address=0x4000,depth=64) ++          // Use SequentialISB Chisel, connect Tilelink
  new chipyard.example.WithSequentialISB(isRegFile=true,address=0x5000,depth=64) ++ 
  new chipyard.example.WithSequentialISB(isRegFile=true,address=0x6000,depth=64) ++ 
  new freechips.rocketchip.rocket.WithNBigCores(4) ++
  new chipyard.config.AbstractConfig)

class FiveRocketCoreWithFiveISB64FanOffConfig extends Config(
  new chipyard.example.WithSequentialISB(isRegFile=true,address=0x4000,depth=64) ++          // Use SequentialISB Chisel, connect Tilelink
  new chipyard.example.WithSequentialISB(isRegFile=true,address=0x5000,depth=64) ++ 
  new chipyard.example.WithSequentialISB(isRegFile=true,address=0x6000,depth=64) ++
  new chipyard.example.WithSequentialISB(isRegFile=true,address=0x7000,depth=64) ++ 
  new chipyard.example.WithSequentialISB(isRegFile=true,address=0x8000,depth=1) ++ 
  new freechips.rocketchip.rocket.WithNBigCores(5) ++
  new chipyard.config.AbstractConfig)


class FiveRocketCoreWithFiveISB64FanOff128DepthConfig extends Config(
  new chipyard.example.WithSequentialISB(isRegFile=true,address=0x4000,depth=128) ++          // Use SequentialISB Chisel, connect Tilelink
  new chipyard.example.WithSequentialISB(isRegFile=true,address=0x5000,depth=128) ++ 
  new chipyard.example.WithSequentialISB(isRegFile=true,address=0x6000,depth=128) ++
  new chipyard.example.WithSequentialISB(isRegFile=true,address=0x7000,depth=128) ++ 
  new chipyard.example.WithSequentialISB(isRegFile=true,address=0x8000,depth=1) ++ 
  new freechips.rocketchip.rocket.WithNBigCores(5) ++
  new chipyard.config.AbstractConfig)

class FiveRocketCoreWithFiveISB256FanOffConfig extends Config(
  new chipyard.example.WithSequentialISB(isRegFile=true,address=0x4000,depth=256) ++          // Use SequentialISB Chisel, connect Tilelink
  new chipyard.example.WithSequentialISB(isRegFile=true,address=0x5000,depth=256) ++ 
  new chipyard.example.WithSequentialISB(isRegFile=true,address=0x6000,depth=256) ++
  new chipyard.example.WithSequentialISB(isRegFile=true,address=0x7000,depth=256) ++ 
  new chipyard.example.WithSequentialISB(isRegFile=true,address=0x8000,depth=1) ++ 
  new freechips.rocketchip.rocket.WithNBigCores(5) ++
  new chipyard.config.AbstractConfig)

class FiveRocketCoreWithFiveISBWith256Depth64WidthFanOffConfig extends Config(
  new chipyard.example.WithSequentialISB(isRegFile=true,address=0x4000,depth=256,width=64) ++          // Use SequentialISB Chisel, connect Tilelink
  new chipyard.example.WithSequentialISB(isRegFile=true,address=0x5000,depth=256,width=64) ++ 
  new chipyard.example.WithSequentialISB(isRegFile=true,address=0x6000,depth=256,width=64) ++
  new chipyard.example.WithSequentialISB(isRegFile=true,address=0x7000,depth=256) ++ 
  new chipyard.example.WithSequentialISB(isRegFile=true,address=0x8000,depth=1) ++ 
  new freechips.rocketchip.rocket.WithNBigCores(5) ++
  new chipyard.config.AbstractConfig)

class FiveRocketCoreWithFourISBWith256Depth64WidthOneISBWith1Depth32WidthFanOffConfig extends Config(
  new chipyard.example.WithSequentialISB(isRegFile=true,address=0x4000,depth=256,width=64) ++          // Use SequentialISB Chisel, connect Tilelink
  new chipyard.example.WithSequentialISB(isRegFile=true,address=0x5000,depth=256,width=64) ++ 
  new chipyard.example.WithSequentialISB(isRegFile=true,address=0x6000,depth=256,width=64) ++
  new chipyard.example.WithSequentialISB(isRegFile=true,address=0x7000,depth=256,width=64) ++ 
  new chipyard.example.WithSequentialISB(isRegFile=true,address=0x8000,depth=1) ++ 
  new freechips.rocketchip.rocket.WithNBigCores(5) ++
  new chipyard.config.AbstractConfig)

class SixRocketCoreWithFiveISBWith256Depth64WidthFanOffConfig extends Config(
  new chipyard.example.WithSequentialISB(isRegFile=true,address=0x4000,depth=256,width=64) ++          // Use SequentialISB Chisel, connect Tilelink
  new chipyard.example.WithSequentialISB(isRegFile=true,address=0x5000,depth=256,width=64) ++ 
  new chipyard.example.WithSequentialISB(isRegFile=true,address=0x6000,depth=256,width=64) ++
  new chipyard.example.WithSequentialISB(isRegFile=true,address=0x7000,depth=256,width=64) ++ 
  new chipyard.example.WithSequentialISB(isRegFile=true,address=0x8000,depth=1) ++ 
  new freechips.rocketchip.rocket.WithNBigCores(6) ++
  new chipyard.config.AbstractConfig)

class FiveRocketCoreWithFourISBWith128Depth64WidthConfig extends Config(
  new chipyard.example.WithSequentialISB(isRegFile=true,address=0x4000,depth=128,width=64) ++
  new chipyard.example.WithSequentialISB(isRegFile=true,address=0x5000,depth=128,width=64) ++
  new chipyard.example.WithSequentialISB(isRegFile=true,address=0x6000,depth=128,width=64) ++
  new chipyard.example.WithSequentialISB(isRegFile=true,address=0x7000,depth=1) ++
  new freechips.rocketchip.rocket.WithNBigCores(5) ++
  new chipyard.config.AbstractConfig)

// Optimized heterogeneous: 3 MedCores (tiles 0-2) + 2 BigCores (tiles 3-4)
// Tile 0 (Med): Linux OS — no FPU needed
// Tile 1 (Med): Huffman decode — bit-serial, branch-heavy, integer only
// Tile 2 (Med): Dequantize — integer multiply only (component[i] *= quant[i])
// Tile 3 (Big): Inverse DCT — float IDCT intermediates, needs FPU
// Tile 4 (Big): Color conversion — float YCbCr→RGB (1.402f*cr, etc.), needs FPU
// 4 ISBs matching app: 0x4000-0x6000 data (64-bit, depth 256), 0x7000 timing (32-bit, depth 1)
class HeteroFiveCoreWithFourISBWith256Depth64WidthConfig extends Config(
  new chipyard.example.WithSequentialISB(isRegFile=true,address=0x4000,depth=256,width=64) ++
  new chipyard.example.WithSequentialISB(isRegFile=true,address=0x5000,depth=256,width=64) ++
  new chipyard.example.WithSequentialISB(isRegFile=true,address=0x6000,depth=256,width=64) ++
  new chipyard.example.WithSequentialISB(isRegFile=true,address=0x7000,depth=1) ++
  new freechips.rocketchip.rocket.WithNBigCores(2) ++                                        // tiles 3,4 (FPU for IDCT + ColorConv)
  new freechips.rocketchip.rocket.WithNMedCores(3) ++                                        // tiles 0,1,2 (no FPU: OS, Huffman, Dequant)
  new chipyard.config.AbstractConfig)

// ------------------------------
// End : Configs with Sequential ISB accelerators
// ------------------------------

// ------------------------------
// Start : Configs with RoCC ISB accelerators
// ------------------------------

// 2-core pipeline: Core 0 → ISB → Core 1
class DualCoreRoCCISBRocketConfig extends Config(
  new chipyard.example.WithRoCCISB(writerTileId=0, readerTileId=1, width=64, depth=256) ++
  new chipyard.config.WithMultiRoCC ++
  new freechips.rocketchip.rocket.WithNBigCores(2) ++
  new chipyard.config.AbstractConfig)

// 3-core pipeline: Core 0 → ISB → Core 1 → ISB → Core 2
class TriCoreRoCCISBRocketConfig extends Config(
  new chipyard.example.WithRoCCISB(writerTileId=0, readerTileId=1, width=64, depth=64) ++
  new chipyard.example.WithRoCCISB(writerTileId=1, readerTileId=2, width=64, depth=64) ++
  new chipyard.config.WithMultiRoCC ++
  new freechips.rocketchip.rocket.WithNBigCores(3) ++
  new chipyard.config.AbstractConfig)

// 5-core pipeline: Core 0 → ISB → Core 1 → ... → Core 4
class FiveCoreRoCCISBPipelineRocketConfig extends Config(
  new chipyard.example.WithRoCCISB(writerTileId=0, readerTileId=1, width=64, depth=64) ++
  new chipyard.example.WithRoCCISB(writerTileId=1, readerTileId=2, width=64, depth=64) ++
  new chipyard.example.WithRoCCISB(writerTileId=2, readerTileId=3, width=64, depth=64) ++
  new chipyard.example.WithRoCCISB(writerTileId=3, readerTileId=4, width=64, depth=64) ++
  new chipyard.config.WithMultiRoCC ++
  new freechips.rocketchip.rocket.WithNBigCores(5) ++
  new chipyard.config.AbstractConfig)

// ------------------------------
// End : Configs with RoCC ISB accelerators
// ------------------------------

// ------------------------------
// Start : Configs with AccumulatorExample RoCC
// ------------------------------

// Single core with AccumulatorExample on custom0 opcode
class RocketAccumulatorConfig extends Config(
  new chipyard.config.WithAccumulatorRoCC(freechips.rocketchip.tile.OpcodeSet.custom0) ++
  new freechips.rocketchip.rocket.WithNBigCores(1) ++
  new chipyard.config.AbstractConfig)

// ------------------------------
// End : Configs with AccumulatorExample RoCC
// ------------------------------