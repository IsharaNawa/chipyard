package chipyard

import org.chipsalliance.cde.config.{Config}
import freechips.rocketchip.rocket.{RocketCoreConfig}

// Local fragment: strip the integer multiplier/divider (M extension).
// Resulting ISA on every Rocket tile present in the config becomes RV64I (+A if you keep atomics).
// WARNING: Linux, OpenSBI, U-Boot, and glibc all require the M extension.
// Use only for area-measurement experiments or for handwritten -march=rv64i bare-metal binaries.
class WithoutMulDiv extends RocketCoreConfig(_.copy(mulDiv = None))

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

class FiveRocketCoreWithFourISBUsingBRAMWith256Depth64WidthOneISBUsingBRAMWith1Depth32WidthFanOffConfig extends Config(
  new chipyard.example.WithSequentialISB(isRegFile=false,address=0x4000,depth=256,width=64) ++          // Use SequentialISB Chisel, connect Tilelink
  new chipyard.example.WithSequentialISB(isRegFile=false,address=0x5000,depth=256,width=64) ++ 
  new chipyard.example.WithSequentialISB(isRegFile=false,address=0x6000,depth=256,width=64) ++
  new chipyard.example.WithSequentialISB(isRegFile=false,address=0x7000,depth=256,width=64) ++ 
  new chipyard.example.WithSequentialISB(isRegFile=false,address=0x8000,depth=2) ++ 
  new freechips.rocketchip.rocket.WithNBigCores(5) ++
  new chipyard.config.AbstractConfig)

// Interrupt-capable counterpart of the above. Uses WithSequentialISBWithIRQ which
// routes two PLIC interrupt lines per ISB (NOT_EMPTY, NOT_FULL) so cores can sleep
// in WFI / blocking UIO read instead of busy-polling the status register.
// Thresholds default to depth/4 (auto when 0) to add hysteresis.
class FiveRocketCoreWithFourISBUsingBRAMWith256Depth64WidthOneISBUsingBRAMWith1Depth32WidthFanOffWithIRQConfig extends Config(
  new chipyard.example.WithSequentialISBWithIRQ(isRegFile=false,address=0x4000,depth=256,width=64) ++
  new chipyard.example.WithSequentialISBWithIRQ(isRegFile=false,address=0x5000,depth=256,width=64) ++
  new chipyard.example.WithSequentialISBWithIRQ(isRegFile=false,address=0x6000,depth=256,width=64) ++
  new chipyard.example.WithSequentialISBWithIRQ(isRegFile=false,address=0x7000,depth=256,width=64) ++
  new chipyard.example.WithSequentialISBWithIRQ(isRegFile=false,address=0x8000,depth=2) ++
  new freechips.rocketchip.rocket.WithNBigCores(5) ++
  new chipyard.config.AbstractConfig)

class FiveRocketCoreWithFourISBUsingBRAMWith512Depth64WidthOneISBUsingBRAMWith1Depth32WidthFanOffConfig extends Config(
  new chipyard.example.WithSequentialISB(isRegFile=false,address=0x4000,depth=512,width=64) ++          // Use SequentialISB Chisel, connect Tilelink
  new chipyard.example.WithSequentialISB(isRegFile=false,address=0x5000,depth=512,width=64) ++ 
  new chipyard.example.WithSequentialISB(isRegFile=false,address=0x6000,depth=512,width=64) ++
  new chipyard.example.WithSequentialISB(isRegFile=false,address=0x7000,depth=512,width=64) ++ 
  new chipyard.example.WithSequentialISB(isRegFile=false,address=0x8000,depth=2) ++ 
  new freechips.rocketchip.rocket.WithNBigCores(5) ++
  new chipyard.config.AbstractConfig)


class SixRocketCoreWithFiveISBUsingBRAMWith512Depth64WidthOneISBUsingBRAMWith2Depth32WidthFanOffConfig extends Config(
  new chipyard.example.WithSequentialISB(isRegFile=false,address=0x4000,depth=512,width=64) ++          // Use SequentialISB Chisel, connect Tilelink
  new chipyard.example.WithSequentialISB(isRegFile=false,address=0x5000,depth=512,width=64) ++ 
  new chipyard.example.WithSequentialISB(isRegFile=false,address=0x6000,depth=512,width=64) ++
  new chipyard.example.WithSequentialISB(isRegFile=false,address=0x7000,depth=512,width=64) ++ 
  new chipyard.example.WithSequentialISB(isRegFile=false,address=0x8000,depth=512,width=64) ++ 
  new chipyard.example.WithSequentialISB(isRegFile=false,address=0x9000,depth=2) ++ 
  new freechips.rocketchip.rocket.WithNBigCores(6) ++
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

// ------------------------------
// Start : A/B configs to measure FPU area cost on Genesys2
// Two single-core BigRocket configs, identical except for the FPU.
// Generate both bitstreams and compare Vivado utilization reports.
// ------------------------------

// Baseline: 1 BigRocket core with the default FPU (RV64GC).
class SingleBigRocketWithFPUConfig extends Config(
  new freechips.rocketchip.rocket.WithNBigCores(1) ++
  new chipyard.config.AbstractConfig)

// Stripped: 1 BigRocket core with FPU removed (RV64IMA only).
// `WithoutFPU` is a RocketCoreConfig that applies to all Rocket tiles
// already present in the config, so it must appear AFTER WithNBigCores
// in evaluation order, which in Chipyard's ++ chains means BEFORE it in source.
class SingleBigRocketWithoutFPUConfig extends Config(
  new freechips.rocketchip.rocket.WithoutFPU ++
  new freechips.rocketchip.rocket.WithNBigCores(1) ++
  new chipyard.config.AbstractConfig)

// ------------------------------
// End : A/B configs to measure FPU area cost on Genesys2
// ------------------------------

// ------------------------------
// Start : A/B configs to measure MulDiv (M extension) area cost on Genesys2
// Two single-core BigRocket configs, identical except for the integer mul/div unit.
// Both have FPU REMOVED so that any utilization delta is attributable purely to MulDiv.
// (Keeping FPU in would not work without MulDiv since the FPU's int<->FP paths reference the int pipeline,
//  and conceptually we want to isolate just the M-extension hardware.)
// ------------------------------

// Baseline: 1 BigRocket core, no FPU, default fast MulDiv (mulUnroll=8, early-out divider).
class SingleBigRocketNoFPUWithMulDivConfig extends Config(
  new freechips.rocketchip.rocket.WithoutFPU ++
  new freechips.rocketchip.rocket.WithNBigCores(1) ++
  new chipyard.config.AbstractConfig)

// Stripped: 1 BigRocket core, no FPU, MulDiv removed (ISA = RV64IA, M extension gone).
class SingleBigRocketNoFPUWithoutMulDivConfig extends Config(
  new chipyard.WithoutMulDiv ++
  new freechips.rocketchip.rocket.WithoutFPU ++
  new freechips.rocketchip.rocket.WithNBigCores(1) ++
  new chipyard.config.AbstractConfig)

// ------------------------------
// End : A/B configs to measure MulDiv area cost on Genesys2
// ------------------------------