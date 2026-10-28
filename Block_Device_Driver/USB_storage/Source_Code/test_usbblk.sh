#!/bin/bash
# test_usbblk.sh - read-only proof that usbblk works on this board
IF=1-1:1.0
N=64                      # MiB to read and compare
say() { echo; echo "=== $* ==="; }

say "1. Board and kernel"
cat /proc/device-tree/model; echo
uname -a
date

say "2. Reference: stock usb-storage driver"
if [ ! -e /sys/bus/usb/drivers/usb-storage/$IF ]; then
	sudo rmmod usbblk 2>/dev/null
	echo $IF | sudo tee /sys/bus/usb/drivers/usb-storage/bind >/dev/null
	sleep 3
fi
SECT_REF=$(sudo blockdev --getsz /dev/sda)
MD5_REF=$(sudo dd if=/dev/sda bs=1M count=$N iflag=direct status=none | md5sum | cut -d' ' -f1)
echo "/dev/sda : $SECT_REF sectors, md5 of first $N MiB = $MD5_REF"
sudo dd if=/dev/sda of=/dev/null bs=1M count=$N iflag=direct 2>&1 | tail -1

say "3. Hand the device over to our driver (usbblk.ko)"
echo $IF | sudo tee /sys/bus/usb/drivers/usb-storage/unbind >/dev/null
sleep 1
sudo rmmod usbblk 2>/dev/null
sudo insmod ./usbblk.ko
sleep 3
sudo dmesg | grep usbblk | tail -8
lsblk -o NAME,SIZE,RO,TYPE | grep -E "NAME|usbblk"

say "4. Compare with the reference"
SECT_MINE=$(sudo blockdev --getsz /dev/usbblk0)
MD5_MINE=$(sudo dd if=/dev/usbblk0 bs=1M count=$N iflag=direct status=none | md5sum | cut -d' ' -f1)
echo "/dev/usbblk0 : $SECT_MINE sectors, md5 of first $N MiB = $MD5_MINE"
sudo dd if=/dev/usbblk0 of=/dev/null bs=1M count=$N iflag=direct 2>&1 | tail -1
if [ "$SECT_REF" = "$SECT_MINE" ] && [ "$MD5_REF" = "$MD5_MINE" ]; then
	echo "RESULT: PASS (capacity and data identical to stock driver)"
else
	echo "RESULT: FAIL"
fi

say "5. Mount read-only through our driver"
sudo mount -o ro /dev/usbblk0p1 /mnt && ls /mnt | head -10
sudo umount /mnt
echo "done"
