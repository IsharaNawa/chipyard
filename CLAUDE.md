# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What is Chipyard

Chipyard is an open-source framework for agile RISC-V SoC design. It integrates hardware generators (Chisel/Scala), RTL simulators (Verilator, VCS), FPGA prototyping flows, and VLSI flows (Hammer) into a unified Make-based build system. The primary language for RTL generation is Chisel (Scala), compiled to Verilog via firtool (MLIR-based FIRRTL compiler).

## Repository Setup

```bash
# Full initialization (Conda env, submodules, RISC-V toolchain, precompile SBT)
./build-setup.sh [--skip-firesim] [--skip-circt] [--use-lean-conda]

# Source environment before any build commands (required every shell session)
source env.sh
```

## Key Build Commands

All simulation commands are run from `sims/verilator/`, `sims/vcs/`, or `sims/xcelium/`.

```bash
cd sims/verilator

# Compile simulator (default: RocketConfig)
make
make debug                     # includes waveform support

# Custom configuration
make CONFIG=BoomConfig
make SUB_PROJECT=testchipip CONFIG=MyConfig

# Generate Verilog only (no simulator binary)
make verilog

# Run a binary
make run-binary BINARY=/path/to/test.elf
make run-binary-fast BINARY=/path/to/test.elf   # no instruction trace
make run-binary-debug BINARY=/path/to/test.elf  # with waveforms

# Run predefined test suites
make run-asm-tests
make run-bmark-tests

# Launch interactive SBT
make launch-sbt
```

## FPGA Build

```bash
cd fpga
make SUB_PROJECT=genesys2 CONFIG=RocketGENESYS2Config bitstream
make SUB_PROJECT=vcu118 CONFIG=RocketVCU118Config bitstream
```

## Test Building

```bash
cd tests
cmake -S . -B ./build -D CMAKE_BUILD_TYPE=Debug
cmake --build ./build --target all
cmake --build ./build --target hello_dump  # generates disassembly
```

## Architecture Overview

### Key Variables (defined in `variables.mk`)

| Variable | Purpose | Default |
|---|---|---|
| `SUB_PROJECT` | Project preset (chipyard, testchipip, rocketchip) | chipyard |
| `CONFIG` | Chisel config class (e.g., `RocketConfig`) | RocketConfig |
| `MODEL` | Top-level Chisel class for simulation | TestHarness |
| `TOP` | True SoC top (synthesizable, used for FPGA/ASIC) | ChipTop |
| `BINARY` | RISC-V ELF to simulate | — |
| `TIMEOUT_CYCLES` | Simulation timeout | 10M |

`MODEL`/`TestHarness` is the simulation wrapper (adds debug ports, clocks). `TOP`/`ChipTop` is the actual SoC. This split lets the same design be simulated and taped out without changes.

### Directory Purposes

- **`generators/`** — Chisel hardware generators. Key ones: `rocket-chip/` (in-order core), `boom/` (out-of-order), `gemmini/` (matrix accelerator), `constellation/` (NoC), `chipyard/` (top-level SoC composer). Each generator is an SBT subproject defined in the root `build.sbt`.
- **`sims/`** — Simulator build systems. Each subdirectory (`verilator/`, `vcs/`, `xcelium/`) includes `common.mk` and adds simulator-specific rules.
- **`fpga/`** — FPGA prototyping. Board support for Arty35T, Arty100T, Genesys2, NexysVideo, VC707, VCU118.
- **`vlsi/`** — Hammer-based VLSI flow for physical design (ASAP7, Sky130, OpenROAD).
- **`tools/`** — Shared libraries: CDE (config framework), firrtl2, dsptools, tapeout macro compiler.
- **`software/`** — FireMarshal workload generation, coremark/embench benchmarks.
- **`scripts/`** — Setup scripts; `build-setup.sh` is the entry point for a fresh clone.

### Configuration System

Configurations compose via Scala mixin inheritance. Config classes live in `generators/chipyard/src/main/scala/config/`:

```scala
class MyConfig extends Config(
  new freechips.rocketchip.rocket.WithNHugeCores(2) ++
  new chipyard.config.AbstractConfig)
```

Named configs: `RocketConfig`, `BoomConfig`, `SmallRocketConfig`, `DualRocketConfig` (see `RocketConfigs.scala`, `BoomConfigs.scala`). Custom configs go in `MyConfigs.scala`.

### Build Pipeline (what happens when you run `make`)

1. SBT compiles all Scala sources into a fat JAR (`$(GENERATOR_CLASSPATH)`)
2. Chisel elaboration produces FIRRTL (`.fir`) and annotation files
3. `firtool` (MLIR FIRRTL compiler) compiles FIRRTL → Verilog
4. Simulator-specific driver (C++) is compiled and linked with generated Verilog
5. Output: `simulator-<MODEL_PACKAGE>-<CONFIG>` (or `simv-*` for VCS)

Build artifacts land in `sims/<simulator>/generated-src/`.

### RoCC Accelerator Integration

Custom accelerators attach to Rocket cores via the RoCC (Rocket Custom Coprocessor) interface. See `generators/gemmini/` for a complete example. Config mixin in `RoCCAcceleratorConfigs.scala`.

### TileLink / Diplomacy

System components connect via TileLink, negotiated at elaboration time by the Diplomacy framework (in `generators/diplomacy/`). Bus widths, addresses, and features are resolved before Verilog is emitted — no runtime negotiation.

## Memory and Performance Notes

- RTL elaboration requires **at least 8 GB RAM** (BOOM/complex configs may need 16 GB+).
- Override heap: `export JAVA_HEAP_SIZE=12G` before running `make`.
- Parallel compilation: `make -j$(nproc)`.
- Faster waveforms: `USE_FST=1` (FST format vs VCD).
- Multi-threaded simulation: `VERILATOR_THREADS=4`.

## Debugging Elaboration Failures

- Chisel elaboration log: `generated-src/<design>.chisel.log`
- firtool log: `generated-src/<design>.firtool.log`
- Use `BREAK_SIM_PREREQ=1` to run simulation without recompiling RTL (skip elaboration).

## Current Branch Context

This repo is on branch `rocc-isb-changes-2`, which contains modifications related to an interrupt-aware ISB (Instruction Stream Buffer) and debug counters. The `generators/rocket-chip` and `generators/constellation` submodules have local modifications.
