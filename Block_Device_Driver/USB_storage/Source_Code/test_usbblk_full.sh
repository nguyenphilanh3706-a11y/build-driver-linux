#!/bin/bash
# test_usbblk_full.sh - read-only test suite for usbblk
IF=1-1:1.0
OFFS="0 2048 60000000 121075712"      # sectors, all multiples of 128
BSS="512 4096 65536 1048576"
PASS=0; FAIL=0
declare -A REF

check() {   # name expected actual
	if [ "$2" = "$3" ]; then echo "PASS  $1"; PASS=$((PASS+1))
	else echo "FAIL  $1  (expected: $2, got: $3)"; FAIL=$((FAIL+1)); fi
}
md5at() {   # device start_sector block_size ; reads 1 MiB
	sudo dd if=$1 bs=$3 skip=$(( $2*512/$3 )) count=$(( 1048576/$3 )) \
		iflag=direct status=none | md5sum | cut -d' ' -f1
}

echo "=== Board ==="
cat /proc/device-tree/model; echo
uname -a; date

echo; echo "=== A. Reference: stock usb-storage ==="
if [ ! -e /sys/bus/usb/drivers/usb-storage/$IF ]; then
	sudo rmmod usbblk 2>/dev/null
	echo $IF | sudo tee /sys/bus/usb/drivers/usb-storage/bind >/dev/null
	sleep 3
fi
SECT=$(sudo blockdev --getsz /dev/sda)
for o in $OFFS; do REF[$o]=$(md5at /dev/sda $o 65536); done
echo "reference capacity: $SECT sectors"

echo; echo "=== B. Our driver ==="
echo $IF | sudo tee /sys/bus/usb/drivers/usb-storage/unbind >/dev/null
sleep 1
sudo rmmod usbblk 2>/dev/null
ERR0=$(sudo dmesg | grep -c "usbblk: transport error")
sudo insmod ./usbblk.ko
sleep 3
sudo dmesg | grep usbblk | tail -8
echo

check "T1 capacity equals stock driver" "$SECT" "$(sudo blockdev --getsz /dev/usbblk0)"
for o in $OFFS; do
	check "T2 md5 of 1 MiB at sector $o" "${REF[$o]}" "$(md5at /dev/usbblk0 $o 65536)"
done
for b in $BSS; do
	check "T3 same 1 MiB read with bs=$b" "${REF[2048]}" "$(md5at /dev/usbblk0 2048 $b)"
done
check "T4 last sector readable (512 bytes)" 512 \
	"$(sudo dd if=/dev/usbblk0 bs=512 skip=$((SECT-1)) count=1 iflag=direct status=none | wc -c)"
check "T5 read past end returns 0 bytes" 0 \
	"$(sudo dd if=/dev/usbblk0 bs=512 skip=$SECT count=1 iflag=direct status=none 2>/dev/null | wc -c)"
check "T6 device is read-only by default" 1 "$(sudo blockdev --getro /dev/usbblk0)"
check "T7 partition usbblk0p1 exists" yes "$([ -b /dev/usbblk0p1 ] && echo yes || echo no)"

sudo mount -o ro /dev/usbblk0p1 /mnt
OKN=$(cd /mnt && head -20 md5sum.txt | sudo md5sum -c - 2>/dev/null | grep -c ': OK$')
check "T8 20 files match md5sum.txt on the USB" 20 "$OKN"
sudo umount /mnt

ERR1=$(sudo dmesg | grep -c "usbblk: transport error")
check "T9 no new transport errors" 0 "$((ERR1-ERR0))"

echo; echo "=== T10 Throughput (64 MiB, 3 runs) ==="
for i in 1 2 3; do
	sudo dd if=/dev/usbblk0 of=/dev/null bs=1M count=64 iflag=direct 2>&1 | tail -1
done

echo; echo "TOTAL: PASS=$PASS FAIL=$FAIL"
