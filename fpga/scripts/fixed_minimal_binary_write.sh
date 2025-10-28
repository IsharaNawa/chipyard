#!/usr/bin/env bash
set -euo pipefail

# go to sdboot dir (script caller can run from anywhere)
SDBOOT_DIR="$(realpath "$(dirname "$0")/../src/main/resources/genesys2/sdboot")"
cd "$SDBOOT_DIR"

BUILD_DIR=build
mkdir -p "$BUILD_DIR"

# tools
CC="${RISCV:-/home/ishara/Research/repos/chipyard_new/chipyard/.conda-env/riscv-tools}/bin/riscv64-unknown-elf-gcc"
OBJCOPY="${RISCV:-/home/ishara/Research/repos/chipyard_new/chipyard/.conda-env/riscv-tools}/bin/riscv64-unknown-elf-objcopy"
command -v "$CC" >/dev/null 2>&1 || { echo "riscv gcc not found at $CC"; exit 1; }
command -v "$OBJCOPY" >/dev/null 2>&1 || { echo "riscv objcopy not found at $OBJCOPY"; exit 1; }

# sources (edit as needed)
# Default payload: decoder.c with its helpers embedded_cat.c and jpg.c plus kprintf and uart
SRCS=(decoder.c embedded_cat.c jpg.c kprintf.c driver/uart.c)
# SRCS=(hello.c kprintf.c driver/uart.c) # uncomment to use simple hello world
echo "Sources: ${SRCS[*]}"

# compile
echo "Compiling..."
"$CC" -march=rv64ima_zicsr_zifencei -mabi=lp64 -mcmodel=medany -O2 \
  -I. -I./include -I./driver \
  "${SRCS[@]}" -static -nostdlib -Wl,-Ttext=0x80000000 -o "$BUILD_DIR/my_payload.elf"

"$OBJCOPY" -O binary "$BUILD_DIR/my_payload.elf" "$BUILD_DIR/my_payload.bin"
payload="$BUILD_DIR/my_payload.bin"
echo "Built payload: $payload ($(stat -c '%s' "$payload") bytes)"

# prompt for device
read -rp "Enter SD raw device (example /dev/sdb, NOT /dev/sdb1): " DEV
DEV="$(realpath -s "$DEV")"
# strip partition number for mmcblk/pX style
DEV_ROOT="${DEV%p*}"
if [ ! -b "$DEV_ROOT" ]; then
  echo "Device $DEV_ROOT not found or not a block device"; exit 1
fi
echo "Using device: $DEV_ROOT"
lsblk "$DEV_ROOT"

read -rp "WARNING: this will overwrite sectors starting at 34 on $DEV_ROOT. Type 'yes' to continue: " ok
[ "$ok" = "yes" ] || { echo "Aborted"; exit 1; }

# unmount any mounted partitions on the device
for m in $(lsblk -ln -o NAME,MOUNTPOINT "$DEV_ROOT" | awk '$2!="" {print "/dev/"$1}'); do
  echo "Unmounting $m"
  sudo umount "$m" || true
done

# optional: pad to 3MiB if you prefer (uncomment to enable)
#PAYLOAD_SIZE=$((3*1024*1024))
#truncate -s $PAYLOAD_SIZE "${payload}.3MiB"
#dd if="$payload" of="${payload}.3MiB" conv=notrunc bs=1 status=none
#writefile="${payload}.3MiB"
writefile="$payload"

# write to sector 34
echo "Writing $writefile to $DEV_ROOT at sector 34..."
sudo dd if="$writefile" of="$DEV_ROOT" bs=512 seek=34 conv=notrunc status=progress
sync

# verify first 8 sectors at payload offset
echo "Verify (hexdump of first 8 sectors at sector 34):"
sudo dd if="$DEV_ROOT" bs=512 skip=34 count=8 status=none | hexdump -C | sed -n '1,200p'

# safe power-off / eject
if command -v udisksctl >/dev/null 2>&1; then
  echo "Powering off $DEV_ROOT"
  sudo udisksctl power-off -b "$DEV_ROOT" || true
else
  echo "Ejecting $DEV_ROOT"
  sudo eject "$DEV_ROOT" || true
fi

echo "Done."