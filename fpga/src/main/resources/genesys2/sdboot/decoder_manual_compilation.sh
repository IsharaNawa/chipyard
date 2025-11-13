source /home/ishara/Research/repos/chipyard_new/chipyard/env.sh

# riscv64-unknown-elf-gcc -march=rv64g -mabi=lp64d -mcmodel=medany -O2 -I. -I./include -I./driver decoder.c jpg.c embedded_cat.c kprintf.c driver/uart.c -static  -nostdlib -Wl,-Ttext=0x80000000 -o ishara/decoder.elf
# riscv64-unknown-elf-gcc -march=rv64imaf_zicsr_zifencei -mabi=lp64f -mcmodel=medany -O2 -I. -I./include -I./driver hello.c kprintf.c driver/uart.c -static -nostdlib -Wl,-Ttext=0x80000000 -o ishara/hello.elf
# riscv64-unknown-elf-gcc -march=rv64imaf_zicsr_zifencei -mabi=lp64f -mcmodel=medany -O2 -msmall-data-limit=0 -I. -I./include -I./driver decoder.c jpg.c embedded_cat.c kprintf.c driver/uart.c -static -nostdlib -Wl,-Ttext=0x80000000 -o ishara/decoder.elf
riscv64-unknown-elf-gcc -march=rv64imaf_zicsr_zifencei -mabi=lp64f -mcmodel=medany -O2 -msmall-data-limit=0 -I. -I./include -I./driver SequentialISBV4.c kprintf.c driver/uart.c -static -nostdlib -Wl,-Ttext=0x80000000 -o ishara/isb_test.elf


# riscv64-unknown-elf-gcc -march=rv64g -mabi=lp64d -mcmodel=medany -O2 -I. -I./include -I./driver decoder.c jpg.c embedded_cat.c kprintf.c driver/uart.c -static  -nostdlib -Wl,-Ttext=0x80000000 -o ishara/decoder.elf
  # -nostartfiles \            # keep bare-metal startfile behavior but allow libgcc link
  # -fno-common -g \
  # -DENTROPY=0 -DNONSMP_HART=0 \
  # -ffunction-sections -fdata-sections \
  # -I. -I./include -I./driver \
  # decoder.c jpg.c embedded_cat.c kprintf.c driver/uart.c \
  # -static -Wl,-gc-sections -Wl,-Ttext=0x80000000 \
  # -static -Wl,-Ttext=0x80000000 \
  # -o ishara/decoder.elf

# riscv64-unknown-elf-objcopy -O binary ishara/decoder.elf ishara/decoder.bin
riscv64-unknown-elf-objcopy -O binary ishara/isb_test.elf ishara/isb_test.bin
# riscv64-unknown-elf-objcopy -O binary ishara/hello.elf ishara/hello.bin


lsblk /dev/sda


for m in $(lsblk -ln -o NAME,MOUNTPOINT /dev/sda | awk '$2!="" {print "/dev/"$1}'); do
  echo "Unmounting $m"
  sudo umount "$m" || true
done


# sudo dd if=ishara/decoder.bin of=/dev/sda bs=512 seek=34 conv=notrunc status=progress
sudo dd if=ishara/isb_test.bin of=/dev/sda bs=512 seek=34 conv=notrunc status=progress
# sudo dd if=ishara/hello.bin of=/dev/sda bs=512 seek=34 conv=notrunc status=progress

sync
# sudo dd if=/dev/sda bs=512 skip=34 count=8 status=none | hexdump -C | sed -n '1,200p'

if command -v udisksctl >/dev/null 2>&1; then
  echo "Powering off"
  sudo udisksctl power-off -b /dev/sda || true
fi


