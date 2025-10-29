#!/usr/bin/env bash
set -euo pipefail

# binary_write.sh
# Interactive helper to compile a user C payload and write it as a 3MiB payload image
# to sector 34 of an SD card (the sdboot loader copies from sector 34 and jumps to
# MEMORY_MEM_ADDR). The script mirrors the steps we've used in this session.

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"  # repo root (two levels up from fpga/scripts)
SDBOOT_DIR="$REPO_ROOT/fpga/src/main/resources/genesys2/sdboot"
BUILD_DIR="$SDBOOT_DIR/build"
PAYLOAD_SIZE_BYTES=$((3 * 1024 * 1024))
PAYLOAD_SECTOR=34
SECTOR_SIZE=512
PAYLOAD_SECTORS=$((PAYLOAD_SIZE_BYTES / SECTOR_SIZE))

echo "This script will build a riscv payload and write a ${PAYLOAD_SIZE_BYTES} byte image to sector ${PAYLOAD_SECTOR} of an SD device."

# Ask for device
read -rp "Enter SD device (example /dev/sdb): " DEV_INPUT
if [ -z "$DEV_INPUT" ]; then
  echo "No device provided. Aborting." >&2
  exit 1
fi

# Normalize common shorthand inputs:
# - "dev/sdb" -> "/dev/sdb"
# - "sdb" -> "/dev/sdb"
DEV_INPUT="$(echo "$DEV_INPUT" | sed -e 's:^\s*::' -e 's:\s*$::')"
if [[ "$DEV_INPUT" == dev/* ]]; then
  DEV_INPUT="/$DEV_INPUT"
fi
if [[ "$DEV_INPUT" != /* ]]; then
  DEV_INPUT="/dev/$DEV_INPUT"
fi

# Resolve any symlinks (will fail if non-existent)
if ! DEV_INPUT_RESOLVED=$(readlink -f "$DEV_INPUT" 2>/dev/null); then
  DEV_INPUT_RESOLVED="$DEV_INPUT"
fi
DEV_INPUT="$DEV_INPUT_RESOLVED"
# determine raw device (strip partition suffix)
base=$(basename "$DEV_INPUT")
devroot=$(echo "$base" | sed -E 's/p?[0-9]+$//')
DEV_ROOT="/dev/$devroot"

if [ ! -b "$DEV_ROOT" ]; then
  echo "Device $DEV_ROOT not found or not a block device." >&2
  exit 1
fi

echo "Using raw device: $DEV_ROOT"

# Show current partition layout
echo "Current partition table for $DEV_ROOT:"
sudo sfdisk -l "$DEV_ROOT" || true

# Quick check: is partition 1 starting after the payload area?
# Try to detect partition 1 start sector (if any) using lsblk
part1_entry=$(lsblk -ln -o NAME,START "$DEV_ROOT" 2>/dev/null | awk 'NR>1{print $1" "$2; exit}' || true)
if [ -n "$part1_entry" ]; then
  part1_name=$(echo "$part1_entry" | awk '{print $1}')
  part1_start=$(echo "$part1_entry" | awk '{print $2}')
  echo "Detected partition 1: /dev/$part1_name start sector: $part1_start"
  if [ -n "$part1_start" ] && [ "$part1_start" -le $((PAYLOAD_SECTOR + PAYLOAD_SECTORS - 1)) ]; then
    echo "WARNING: partition 1 starts at or before the payload end sector (it may be overwritten)."
    read -rp "Continue and overwrite partition table/format (yes/no)? " yn
    case "$yn" in
      [Yy]* ) echo "Continuing per user request." ;;
      * ) echo "Aborting." ; exit 1 ;;
    esac
  fi
else
  echo "No partition 1 detected (or parsing failed). The script will proceed but be careful.";
fi

# Ask for source file(s) to compile
read -rp "Enter source C file(s) (space-separated or comma-separated). Default: $SDBOOT_DIR/hello.c : " srcs
if [ -z "$srcs" ]; then
  srcs="$SDBOOT_DIR/hello.c"
fi

# Normalize comma-separated input into space-separated tokens
srcs="$(echo "$srcs" | tr ',' ' ')"

# Expand and verify
SRC_FILES=()
for s in $srcs; do
  # skip empty tokens
  [ -z "$s" ] && continue
  # if relative path given, treat relative to repo root
  if [[ "$s" != /* ]]; then
    s="$REPO_ROOT/$s"
  fi
  if [ ! -f "$s" ]; then
    echo "Source file not found: $s" >&2
    exit 1
  fi
  SRC_FILES+=("$s")
done

# Allow additional supporting files (kprintf.c and uart driver are required)
KPRINTF_SRC="$SDBOOT_DIR/kprintf.c"
UART_SRC="$SDBOOT_DIR/driver/uart.c"
if [ ! -f "$KPRINTF_SRC" ] || [ ! -f "$UART_SRC" ]; then
  echo "Required support sources not found in $SDBOOT_DIR (kprintf.c or driver/uart.c)." >&2
  exit 1
fi

# Toolchain detection
if [ -n "${RISCV:-}" ] && [ -x "${RISCV}/bin/riscv64-unknown-elf-gcc" ]; then
  CC="${RISCV}/bin/riscv64-unknown-elf-gcc"
  OBJCOPY="${RISCV}/bin/riscv64-unknown-elf-objcopy"
else
  # fallback to whatever is in PATH
  if command -v riscv64-unknown-elf-gcc >/dev/null 2>&1; then
    CC="$(command -v riscv64-unknown-elf-gcc)"
    OBJCOPY="$(command -v riscv64-unknown-elf-objcopy)"
  else
    echo "riscv cross toolchain not found. Please set RISCV environment variable or install riscv64-unknown-elf-gcc." >&2
    exit 1
  fi
fi

echo "Using compiler: $CC"

# Create build dir
mkdir -p "$BUILD_DIR"

PAY_ELF="$BUILD_DIR/payload.elf"
PAY_BIN="$BUILD_DIR/payload.bin"
PAY_PAD="$BUILD_DIR/payload_3MiB.bin"

# Compiler flags - align with sdboot build when reasonable
CFLAGS="-march=rv64ima_zicsr_zifencei -mcmodel=medany -O2 -std=gnu11 -Wall -fno-common -g -DENTROPY=0 -mabi=lp64 -DNONSMP_HART=0 -ffunction-sections -fdata-sections -I$SDBOOT_DIR -I$SDBOOT_DIR/include -I$SDBOOT_DIR/driver"
LDFLAGS="-static -nostdlib"

echo "Compiling sources: ${SRC_FILES[*]}"
# Build command
# Link at MEMORY_MEM_ADDR (from platform.h); default 0x80000000
MEMORY_ADDR=0x80000000

# compile
OBJ_LIST=()
for s in "${SRC_FILES[@]}"; do
  o="$BUILD_DIR/$(basename "$s" .c).o"
  echo "  gcc -> $o"
  "$CC" $CFLAGS -c "$s" -o "$o"
  OBJ_LIST+=("$o")
done
# compile kprintf and uart
"$CC" $CFLAGS -c "$KPRINTF_SRC" -o "$BUILD_DIR/kprintf.o"
"$CC" $CFLAGS -c "$UART_SRC" -o "$BUILD_DIR/uart.o"
OBJ_LIST+=("$BUILD_DIR/kprintf.o" "$BUILD_DIR/uart.o")

# link
echo "Linking to ELF at address $MEMORY_ADDR -> $PAY_ELF"
"$CC" ${LDFLAGS} -Wl,-Ttext=$MEMORY_ADDR -o "$PAY_ELF" "${OBJ_LIST[@]}"

# create raw binary
echo "Creating raw binary: $PAY_BIN"
"$OBJCOPY" -O binary "$PAY_ELF" "$PAY_BIN"

# pad to 3MiB
echo "Creating padded image $PAY_PAD (size $PAYLOAD_SIZE_BYTES bytes)"
truncate -s $PAYLOAD_SIZE_BYTES "$PAY_PAD"
# copy payload.bin into padded image
dd if="$PAY_BIN" of="$PAY_PAD" conv=notrunc status=none || true

echo "About to write $PAY_PAD to $DEV_ROOT at sector $PAYLOAD_SECTOR (requires sudo)"
read -rp "Proceed and overwrite sectors on $DEV_ROOT? (yes/no) " ok
if [[ "$ok" != "yes" ]]; then
  echo "Aborting write.";
  exit 1
fi

# flush buffers and write
sync
sudo dd if="$PAY_PAD" of="$DEV_ROOT" bs=$SECTOR_SIZE seek=$PAYLOAD_SECTOR conv=notrunc status=progress
sync

# try to safely eject/power-off the device
if command -v udisksctl >/dev/null 2>&1; then
  echo "Powering off device via udisksctl"
  sudo udisksctl power-off -b "$DEV_ROOT" || true
else
  echo "Attempting eject of $DEV_ROOT"
  sudo eject "$DEV_ROOT" || true
fi

echo "Write complete. Payload written to $DEV_ROOT (sector $PAYLOAD_SECTOR)."
# show verification (first readable sectors)
sudo dd if="$DEV_ROOT" bs=$SECTOR_SIZE skip=$PAYLOAD_SECTOR count=8 | hexdump -C | sed -n '1,120p'

echo "Done. Please reinsert the card into the FPGA and reset the board to run the payload."
