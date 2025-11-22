#!/bin/bash
# Bare-metal Spike compilation and execution script

# first source the chipyard environment
source /home/ishara/Research/repos/chipyard/env.sh

# compilation variables
CC=riscv64-unknown-elf-gcc
MARCH=rv64imafd_zicsr_zifencei
MABI=lp64d
CFLAGS="-mcmodel=medany -O2 -std=gnu11 -Wall -nostartfiles -nostdlib -fno-common -g -ffunction-sections -fdata-sections"
LINKER_SCRIPT=../src/main/resources/genesys2/sdboot/spike.lds
SOURCE_FILES=(
    ../src/main/resources/genesys2/sdboot/start_spike.S
    ../src/main/resources/genesys2/sdboot/hello_spike.c
)
OUTPUT_FILE=hello_spike.elf

# compile for bare-metal spike
${CC} \
  -march=${MARCH} -mabi=${MABI} ${CFLAGS} \
  -T ${LINKER_SCRIPT} \
  "${SOURCE_FILES[@]}" \
  -o ${OUTPUT_FILE}

# check if compilation was successful
if [ $? -eq 0 ]; then
  echo "Compilation successful!"
  echo "Running on bare-metal Spike..."
  # run it on spike bare-metal (no pk)
  spike ${OUTPUT_FILE}
else
  echo "Compilation failed!"
  exit 1
fi
