sudo umount /media/ishara/BDD3-3E30
# if that fails with "busy", find and stop users first:
sudo lsof /dev/sda1
sudo fuser -v /dev/sda1
# to kill processes using it (use carefully):
sudo fuser -km /dev/sda1
# then retry unmount
sudo umount /media/ishara/BDD3-3E30

sudo rm /dev/sda


sudo udevadm trigger --type=subsystem --subsystem-match=block
sudo udevadm settle
# re-read partition tables just in case
sudo partprobe

ls -l /dev/sda /dev/sda1
lsblk -a

# create node with major:minor 8:0 (typical for first SCSI/SATA disk)
sudo mknod /dev/sda b 8 0
sudo chown root:disk /dev/sda
sudo chmod 660 /dev/sda

# re-run lsblk to verify
ls -l /dev/sda /dev/sda1
lsblk -a

sync
sudo blockdev --flushbufs /dev/sda

sudo udisksctl power-off -b /dev/sda