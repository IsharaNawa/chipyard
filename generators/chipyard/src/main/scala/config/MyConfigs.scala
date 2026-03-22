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

class FiveRocketCoreWithFourISBWith128Depth64WidthConfig extends Config(
  new chipyard.example.WithSequentialISB(isRegFile=true,address=0x4000,depth=128,width=64) ++
  new chipyard.example.WithSequentialISB(isRegFile=true,address=0x5000,depth=128,width=64) ++
  new chipyard.example.WithSequentialISB(isRegFile=true,address=0x6000,depth=128,width=64) ++
  new chipyard.example.WithSequentialISB(isRegFile=true,address=0x7000,depth=1) ++
  new freechips.rocketchip.rocket.WithNBigCores(5) ++
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