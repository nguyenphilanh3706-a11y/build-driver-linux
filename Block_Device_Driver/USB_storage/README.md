# USB Block Device Driver

## Overview

A custom Linux USB block device driver for a Kingston DataTraveler USB flash drive. The driver uses USB Bulk-Only Transport (BOT), SCSI commands, and the Linux block layer (blk-mq) to expose storage as `/dev/usbblk0`.

## Features

- USB device detection and disconnection handling.
- SCSI `INQUIRY`, `READ CAPACITY`, `READ(10)`, and `WRITE(10)` commands.
- Block-device registration using blk-mq.
- Read-only mode by default.
- Data integrity and read-throughput testing against the standard `usb-storage` driver.

## Build and Test

```bash
make
sudo insmod usbblk.ko
lsblk
```

Run the test scripts:

```bash
sudo ./test_usbblk.sh
sudo ./test_usbblk_full.sh
```

**Note:** The driver targets the configured USB device. Unmount its partitions before testing. Enabling write access may modify or destroy data.

