# Uses the bash inside your Chipyard conda environment as the interpreter.
#!/home/ishara/Research/repos/chipyard/.conda-env/bin/bash

# exit immediately if any command fails
set -e

chipyard_root="$HOME/Research/repos/chipyard"

# the Scala/Chisel config class for the FPGA build
CONFIG="FiveRocketCoreFourInterruptAwareMMIOFIFOUsingBRAMWithDebugEnabledWithDepth256Size64Genesys2With80FreqConfig"

# frequency of the peripheral bus clock in Hz
# This value is passed into the bootloader build so that the UART and other peripherals are clocked correctly
PBUS_CLK=80000000

# Vivado output and the generated device-tree sources
generated_dir="$chipyard_root/fpga/generated-src/chipyard.fpga.genesys2.GENESYS2FPGATestHarness.$CONFIG"

# bitstream destination directory
dest_dir="$chipyard_root/fpga/bitstream_copy"

# device-tree source destination directory
dts_dir="$chipyard_root/fpga/dts_copy"

# zero-stage bootloader is simply the very first code that runs on your system right after reset
# After configuration, the CPU starts fetching instructions from a fixed reset address (e.g., 0x10000)
# Something must already be at that address to bring up clocks, set up memory, and decide where the “real” software will come from (SD card, SPI flash, network, etc.).
# That something is the zero-stage bootloader.
echo "Script to build bootrom of zero stage bootloader for Rocket Chip on the Genesys2 FPGA board"

# source the env file in the chipyard directory
cd $chipyard_root
source env.sh

# source vivado settings to get vivado tools in the path
source /tools/Xilinx/2025.1/Vivado/settings64.sh

# build sdboot
# Goes into the sdboot directory containing the small ROM program that the FPGA will execute at reset
cd $chipyard_root/fpga/src/main/resources/genesys2/sdboot
# runs makefile in sdboot to clean any previous builds
make clean
# compiles that bootloader, embedding the correct peripheral-bus frequency so UART baud rates and timers are right.
make PBUS_CLK=$PBUS_CLK

# build bitstream
cd $chipyard_root/fpga
# runs the Chipyard FPGA makefile , to clean
make clean
# runs the Chipyard FPGA makefile , -j18 builds with 18 parallel jobs to speed up elaboration
make CONFIG=$CONFIG bitstream -j18

# copy bitstream, dts
mkdir -p $dest_dir
mkdir -p $dts_dir
cp $generated_dir/obj/*.bit $dest_dir/$CONFIG.bit
cp $generated_dir/*.dts $dts_dir/$CONFIG.dts

# compile dts(device tree) -> dtb(device tree blob - binary format of dts) using dtc (device tree compiler)
# dtc is not installed by default in chipyard conda environment, so check if it exists first
if command -v dtc >/dev/null 2>&1; then
    dtc -I dts -O dtb -o $dts_dir/$CONFIG.dtb $dts_dir/$CONFIG.dts
    echo "DTB compiled: $dts_dir/$CONFIG.dtb"
else
    echo "WARNING: dtc not found in PATH, cannot compile DTS to DTB."
fi

# final messages to the user about where things are and what to do next
echo "==================================================="
echo "Done building bitstream."
echo "Bitstream copied to: $dest_dir/$CONFIG.bit"
echo "DTS/DTB saved to: $dts_dir/$CONFIG.dts / $dts_dir/$CONFIG.dtb"
echo "Now program the FPGA with:"
echo "  Please open hardware manager to program $dest_dir/$CONFIG.bit"
echo " Or Use program_fpga.sh script to program the FPGA"
echo " Use connect_serial.sh script to connect to the serial console"
echo "==================================================="
