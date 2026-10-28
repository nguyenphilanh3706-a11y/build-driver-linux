# HTN SH1106 Linux framebuffer driver

Target: Raspberry Pi 5 + 1.3 inch 128x64 SH1106 OLED over I2C.
Default I2C address: 0x3c.

## Demo Video

[![Watch the demo](https://img.youtube.com/vi/ptf8MbYmHf4/0.jpg)](https://youtu.be/ptf8MbYmHf4)

## Wiring

OLED | Raspberry Pi 5
---|---
VCC | 3.3V
GND | GND
SDA | GPIO2 / SDA1 / pin 3
SCL | GPIO3 / SCL1 / pin 5

## Dependencies

```bash
sudo apt update
sudo apt install build-essential raspberrypi-kernel-headers device-tree-compiler i2c-tools
```

On Debian kernels where `raspberrypi-kernel-headers` is unavailable, install the matching kernel headers package for `uname -r`.

## 1. Check I2C

Enable I2C in `/boot/firmware/config.txt`:

```ini
dtparam=i2c_arm=on
```

Reboot, then:

```bash
sudo i2cdetect -y 1
```

You should normally see `3c`.

## 2. Build kernel module

```bash
make
```

## 3. Build overlay

```bash
make dtbo
sudo cp htn-sh1106.dtbo /boot/firmware/overlays/
```

Add to `/boot/firmware/config.txt`:

```ini
dtparam=i2c_arm=on
dtoverlay=htn-sh1106
```

Reboot.

## 4. Load driver

```bash
sudo insmod htn_sh1106_fb.ko
dmesg | tail -n 30
ls /dev/fb*
cat /proc/fb
```

Expected dmesg example:

```text
htn_sh1106_fb 1-003c: fb1 registered: SH1106 128x64, I2C addr=0x3c
```

`fb1` is common if HDMI already owns fb0. Always check `cat /proc/fb`.

## 5. Test graphics

```bash
make fb_test
sudo ./fb_test /dev/fb1
```

The OLED should show two rectangles and diagonal X lines.

## Debug

```bash
dmesg | grep -Ei 'htn|sh1106|i2c|fb'
ls -l /sys/bus/i2c/devices/1-003c/
cat /proc/fb
```

If the image is shifted horizontally, adjust `HTN_COL_OFFSET` in the driver. SH1106 128x64 modules commonly need offset 2.

If your module is SSD1306 rather than SH1106, do not use the SH1106 init/column code unchanged; SSD1306 supports a different memory addressing scheme and Linux already includes `ssd1307fb` support for SSD1306-class displays.
