# SYSTEM REQUIREMENT & ARCHITECTURAL SPECIFICATION

**Project Title:** Custom Linux Out-of-Tree Kernel Drivers for USB Mass Storage (blk-mq BOT), ALSA Digital Audio (ASoC I2S), and Monochrome OLED Framebuffer (Deferred I/O)  
**Target Platform:** Raspberry Pi Zero W Rev 1.1 (Broadcom BCM2835 SoC, 1.0 GHz ARM1176JZF-S, 512 MB LPDDR2)  
**Target OS & Kernel:** Raspbian GNU/Linux, Linux Kernel Version 6.18.50+rpt-rpi-v6 (32-bit ARMv6Z)  
**Project Authors:** Nguyen Phi Lanh, Le Cong Quoc Anh, Phan Anh Khoa, Le Thanh Tai  
**Advisor:** M.Sc. Huynh Hoang Ha (Ho Chi Minh City University of Technology and Engineering)

---

## 1. System Architecture & Hardware Pin Allocation

This document specifies the technical requirements, hardware pinouts, kernel interface architectures, and operational principles for three custom out-of-tree Loadable Kernel Modules (LKMs) deployed on the Raspberry Pi Zero W platform.

### 1.1 System Architecture Overview

```mermaid
flowchart TB
  subgraph US[USER SPACE]
    direction LR
    U1[Storage utilities] ~~~ U2[Audio applications] ~~~ U3[Display applications]
  end
  subgraph KS[LINUX KERNEL SUBSYSTEMS]
    direction LR
    K1[Block layer / blk-mq] ~~~ K2[ALSA / ASoC] ~~~ K3[Framebuffer / fbdev]
  end
  subgraph DR[CUSTOM KERNEL DRIVERS]
    direction LR
    D1[usbblk.ko] ~~~ D2[i2sdemo.ko] ~~~ D3[htn_sh1106_fb.ko]
  end
  subgraph HW[SOC INTERFACES AND PERIPHERALS]
    direction LR
    H1[USB / dwc2 → Kingston Flash] ~~~ H2[I2S + DMA → MAX98357A] ~~~ H3[I2C1 → SH1106 OLED]
  end
  U1 --> K1 --> D1 --> H1
  U2 --> K2 --> D2 --> H2
  U3 --> K3 --> D3 --> H3
  classDef app fill:#fff7f1,stroke:#ba816c,color:#312a28,stroke-width:1.5px
  classDef kernel fill:#eaf0f5,stroke:#67859e,color:#243746,stroke-width:1.5px
  classDef driver fill:#ede6f5,stroke:#9074ab,color:#312640,stroke-width:2px
  classDef hardware fill:#e6efe8,stroke:#72987e,color:#25372b,stroke-width:1.5px
  class U1,U2,U3 app
  class K1,K2,K3 kernel
  class D1,D2,D3 driver
  class H1,H2,H3 hardware
```

### 1.2 GPIO & Hardware Interconnect

```mermaid
flowchart LR
  PI["Raspberry Pi Zero W<br/>BCM2835"]
  USB["Kingston DataTraveler<br/>USB OTG"]
  AMP["MAX98357A<br/>I2S amplifier"]
  SPK["4 Ω / 3 W speaker"]
  OLED["SH1106 OLED<br/>I2C1 address 0x3C"]
  PI -->|"Micro-USB OTG"| USB
  PI -->|"GPIO18 · pin 12 · ALT0 · BCLK"| AMP
  PI -->|"GPIO19 · pin 35 · ALT0 · LRCLK"| AMP
  PI -->|"GPIO21 · pin 40 · ALT0 · DATA"| AMP
  AMP --> SPK
  PI -->|"GPIO2 · pin 3 · ALT0 · SDA1"| OLED
  PI -->|"GPIO3 · pin 5 · ALT0 · SCL1"| OLED
  classDef host fill:#273643,stroke:#273643,color:#ffffff,stroke-width:2px
  classDef storage fill:#fff1e9,stroke:#b87357,color:#302722,stroke-width:1.6px
  classDef audio fill:#f0e9f7,stroke:#8a6da7,color:#33273f,stroke-width:1.6px
  classDef graphics fill:#e5f0e9,stroke:#6b9679,color:#253b2c,stroke-width:1.6px
  class PI host
  class USB storage
  class AMP,SPK audio
  class OLED graphics
```

**Power connections:** MAX98357A VIN/GND → physical pins **2 (5 V)** / **6 (GND)**; SH1106 VCC/GND → **1 (3.3 V)** / **9 (GND)**.

### 1.3 Complete Hardware Pinout Mapping

| Interface / Bus | Physical Pin # | SoC GPIO / Signal | Peripheral Pin | Electrical & Protocol Mode |
| :--- | :--- | :--- | :--- | :--- |
| **USB OTG Port** | Micro-USB | dwc2 OTG Controller | Kingston DT3.0 USB | USB 2.0 High-Speed (480 Mbps), BOT Protocol, VID 0x0951 PID 0x1666 |
| **I2S Digital Audio** | Physical Pin 12 | GPIO 18 / ALT0 (PCM_CLK) | MAX98357A BCLK | 1.536 MHz Bit Clock (48 kHz 16-bit Stereo PCM) |
| **I2S Digital Audio** | Physical Pin 35 | GPIO 19 / ALT0 (PCM_FS) | MAX98357A LRC (WS) | 48 kHz Word Select / Frame Sync Clock |
| **I2S Digital Audio** | Physical Pin 40 | GPIO 21 / ALT0 (PCM_DOUT) | MAX98357A DIN | Serial PCM Audio Data Line from BCM2835 I2S |
| **Audio Power Rail** | Physical Pin 2 | 5.0V Power Rail | MAX98357A VIN | 5.0 V DC Power Supply for Class-D Amplifier |
| **Audio Ground** | Physical Pin 6 | GND Rail | MAX98357A GND | Common System Ground Reference |
| **I2C OLED Display** | Physical Pin 3 | GPIO 2 / ALT0 (SDA1) | SH1106 OLED SDA | I2C1 Serial Data Line (Slave Address 0x3C, 100 kHz) |
| **I2C OLED Display** | Physical Pin 5 | GPIO 3 / ALT0 (SCL1) | SH1106 OLED SCL | I2C1 Serial Clock Line driven by BCM2835 Master |
| **OLED Power Rail** | Physical Pin 1 | 3.3V Power Rail | SH1106 OLED VCC | 3.3 V DC Logic / Panel Power Supply |
| **OLED Ground** | Physical Pin 9 | GND Rail | SH1106 OLED GND | Common System Ground Reference |

---

## 2. Subsystem Functional Specifications & Working Principles

### 2.1 Monolithic USB Mass Storage Block Driver (`usbblk.ko`)

The `usbblk.ko` subsystem shall integrate USB Bulk-Only Transport (BOT), a minimal SCSI translation engine, and Linux Multi-Queue Block Layer (`blk-mq`) dispatch in a single custom block-driver path.

- **USB Device Binding:** Binds directly to Kingston DataTraveler 3.0 (VID `0x0951`, PID `0x1666`, Class `0x08`, Subclass `0x06`, Protocol `0x50`).
- **SCSI Initialization Sequence:** Issues synchronous SCSI `TEST UNIT READY` (`0x00`), `REQUEST SENSE` (`0x03`), `INQUIRY` (`0x12`), and `READ CAPACITY(10)` (`0x25`) commands to discover device size (121,077,760 sectors x 512 B = 57.7 GiB).
- **Block Layer Registration:** Allocates a `blk-mq` tag set with 1 hardware queue (depth 16, `BLK_MQ_F_BLOCKING`), 64 KiB max transfer limit (128 sectors), and registers `gendisk` `/dev/usbblk0` and partition `/dev/usbblk0p1`.
- **CDB Protocol Engine:** Translates `blk-mq` `REQ_OP_READ` / `REQ_OP_WRITE` requests into 10-byte SCSI CDBs `READ(10)` (`0x28`) and `WRITE(10)` (`0x2A`) with big-endian sector LBA and transfer length conversion.
- **Write Protection Mechanism:** Enforces read-only protection by default (`allow_write=0`). Writes are permitted only when explicitly loaded with `allow_write=1`.
- **Hot-Unplug & Fault Recovery:** Handles physical hot-unplug via `dev->gone` flag, mutex lock protection, `del_gendisk()`, and BOT Reset Recovery (`0xFF` Control Request + `usb_clear_halt()`).

#### Storage Data Flow Diagram

```mermaid
flowchart TB
  A["User-space I/O / VFS"] --> B["blk-mq request queue"]
  B --> C["usbblk_queue_rq()"]
  C --> D["SCSI READ(10) / WRITE(10) CDB"]
  subgraph BOT[USB BULK-ONLY TRANSPORT — SEQUENTIAL PHASES]
    direction LR
    P1["01 · Command<br/>31-byte CBW<br/>Bulk OUT 0x02"] --> P2["02 · Data<br/>up to 64 KiB<br/>Bulk IN/OUT"] --> P3["03 · Status<br/>13-byte CSW<br/>Bulk IN 0x81"]
  end
  D --> P1
  P3 --> F["Kingston USB Mass Storage"]
  classDef core fill:#eaf0f5,stroke:#6a8499,color:#263644,stroke-width:1.5px
  classDef phase fill:#fff1e9,stroke:#b87357,color:#382b26,stroke-width:1.7px
  classDef target fill:#e7f0e9,stroke:#72987e,color:#263b2d,stroke-width:1.7px
  class A,B,C,D core
  class P1,P2,P3 phase
  class F target
```

---

### 2.2 Unified ALSA SoC Digital Audio Driver (`i2sdemo.ko` & `i2sdemo.dtbo`)

The `i2sdemo.ko` subsystem shall register the ASoC Codec Component (`student,i2sdemo-codec`) and Machine Driver (`student,i2sdemo-card`), linking BCM2835 I2S (`20203000.i2s`) and DMA Channel 5 to the MAX98357A Class-D amplifier.

- **Device Tree Overlay:** Device Tree overlay (`i2sdemo-overlay.dts`) enables `<&i2s_clk_producer>` and instantiates codec/card nodes.
- **ASoC Codec Component:** Registers Codec DAI `i2sdemo-hifi` supporting 8-96 kHz, 16/24/32-bit linear PCM (`S16_LE`, `S24_LE`, `S32_LE`), stereo/mono playback, and Speaker DAPM widget.
- **ASoC Machine Card:** Binds CPU DAI (`20203000.i2s`), Codec DAI (`i2sdemo-hifi`), and Platform DMA (`bcm2835-dma` Channel 5) in I2S Master Mode (`SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBC_CFC`).
- **Kernel Stream Diagnostics:** Hooks `.hw_params` and `.trigger` callbacks to log audio stream parameters (rate, channels, bit width) and playback state (`START`/`STOP`) into `dmesg`.

#### Audio Data Flow Diagram

```mermaid
flowchart LR
  APP["aplay / mpg123"] --> ALSA["ALSA PCM Core<br/>/dev/snd/pcmC*D*p"]
  ALSA --> ASoC["ASoC framework"]
  subgraph CUSTOM["i2sdemo.ko — topology and callbacks"]
    direction TB
    MACH["Machine driver<br/>DAI link"]
    CODEC["Codec DAI<br/>i2sdemo-hifi"]
    MACH --- CODEC
  end
  ASoC --> MACH
  MACH --> CPU["BCM2835 CPU DAI<br/>DMA channel 5"]
  CPU --> I2S["I2S bus<br/>BCLK · LRCLK · DATA"]
  I2S --> AMP["MAX98357A"] --> SPK["Speaker"]
  classDef app fill:#fff7f1,stroke:#ba816c,color:#332a25
  classDef kernel fill:#eaf0f5,stroke:#67859e,color:#243746
  classDef custom fill:#f0e9f7,stroke:#8a6da7,color:#33273f,stroke-width:2px
  classDef physical fill:#e7f0e9,stroke:#72987e,color:#253b2d
  class APP app
  class ALSA,ASoC,CPU kernel
  class MACH,CODEC custom
  class I2S,AMP,SPK physical
```

*The Codec DAI describes the audio endpoint; the BCM2835 I2S controller and DMA move the PCM samples.*

---

### 2.3 Monochrome OLED Framebuffer Driver (`htn_sh1106_fb.ko` & `htn-sh1106.dtbo`)

The `htn_sh1106_fb.ko` subsystem shall expose `/dev/fb1` through Linux fbdev for the 1.3-inch 128×64 SH1106 OLED connected over I2C1 at address `0x3C`.

- **Device Tree Binding:** `htn-sh1106-overlay.dts` enables `<&i2c1>` and binds `oled@3c` (`compatible = "htn,sh1106-fb"`) at 100 kHz I2C clock.
- **Panel Initialization:** Sends SH1106 initialization sequence over I2C: Display Off (`0xAE`), Clock Div (`0x80`), Multiplex 1/64 (`0x3F`), Segment Remap (`0xA1`), COM Scan (`0xC8`), Contrast (`0x80`), Charge Pump DC-DC (`0xAD`, `0x8B`), Display On (`0xAF`).
- **Framebuffer & Deferred I/O:** Allocates 1,024-byte virtual buffer (128x64, 1-bpp, `vzalloc`) and enables 20 Hz Deferred I/O (`FB_GEN_DEFAULT_DEFERRED_SYSMEM_OPS`) to trigger display updates on page fault.
- **Bit Transposition & Alignment:** Transposes row-major 1-bpp virtual buffer pixels into 8 vertical pages (Page Addressing `0xB0|page`), applies `HTN_COL_OFFSET = 2` to fix 2-pixel column shift on 132-column GDDRAM, and streams I2C payload in 16-byte chunks.

#### Graphics Rendering Diagram

```mermaid
flowchart LR
  APP["fb_test / oled_terminal"] --> FB["/dev/fb1<br/>128 × 64 · 1 bpp"]
  FB --> DEF["Deferred I/O<br/>20 Hz"]
  DEF --> PACK["Pixel transposition<br/>row-major → 8 pages"]
  PACK --> OFFSET["Column offset +2<br/>16-byte chunks"]
  OFFSET --> I2C["BCM2835 I2C1<br/>GPIO2 SDA1 · GPIO3 SCL1"]
  I2C --> OLED["SH1106 OLED<br/>0x3C"]
  classDef app fill:#fff7f1,stroke:#ba816c,color:#332a25
  classDef kernel fill:#eaf0f5,stroke:#67859e,color:#243746
  classDef driver fill:#f0e9f7,stroke:#8a6da7,color:#33273f,stroke-width:2px
  classDef hardware fill:#e7f0e9,stroke:#72987e,color:#253b2d
  class APP app
  class FB,DEF kernel
  class PACK,OFFSET driver
  class I2C,OLED hardware
```

---

## 3. Non-Functional Requirements & Safety Engineering

- **NFR-01 (DMA-Safe Memory Allocation):** All USB (CBW, CSW, sense, 64 KiB xfer) and I2C buffers are dynamically allocated in DMA-safe kernel heap via `kmalloc(..., GFP_KERNEL)`. Stack-allocated buffers are strictly prohibited.
- **NFR-02 (Concurrency & Synchronization):** USB transactions are synchronized via `struct mutex`. The `blk-mq` tag set uses `BLK_MQ_F_BLOCKING` to permit thread sleeping during `usb_bulk_msg()` I/O execution.
- **NFR-03 (Defensive Resource Cleanup):** Reverse-order `goto` unwinding labels (`err_disk`, `err_tag`, `err_vfree`) clean up resources on probe failure. Managed `devm_*` helpers ensure leak-free driver unbinding.
- **NFR-04 (Protocol Fault Recovery):** Bulk endpoints (`0x81` IN, `0x02` OUT) automatically issue USB BOT Reset Recovery (`0xFF` Control Request + `usb_clear_halt()`) upon STALL (`-EPIPE`) or bus errors.
- **NFR-05 (Performance & Parity):** Sustained 25.3-25.5 MB/s sequential read throughput (86.8% of stock `usb-storage`), 100% MD5 hash verification parity, 0 audio underruns (XRUNs=0, 1.2% CPU), and 20 Hz tear-free OLED rendering.

---

## 4. Verification & Traceability Matrix

| Req ID / Subsystem | Verification Test Suite | Empirical Measured Result | Status |
| :--- | :--- | :--- | :--- |
| **FR-BLK-01..04** *(USB Storage)* | 15-Case Test Suite & 40 Random LBA Reads | 121,077,760 sectors detected; 100% MD5 hash match with stock `/dev/sda` across all test samples | **PASS (100%)** |
| **FR-BLK-05..06** *(Storage Safe)* | `mkfs.fat`, File Write (`allow_write=1`), `fsck.fat` | `/dev/usbblk0p1` mounted; 4 files (10 MiB) written; `fsck.fat` verified clean; hot-unplug handled cleanly | **PASS (100%)** |
| **FR-SND-01..04** *(ALSA Audio)* | 48 kHz MP3 Playback via `mpg123` on `hw:i2sdemo,0` | Zero audio underruns (XRUNs = 0); ~1.2% CPU utilization; `dmesg` logged `rate=48000 ch=2 16-bit` & START/STOP | **PASS (100%)** |
| **FR-GFX-01..04** *(OLED Display)* | Bresenham Geometric Test (`fb_test`) & Terminal | `/dev/fb1` registered; crisp rendering of rectangles, diagonal X, and 5x7 ASCII text with zero offset artifacts | **PASS (100%)** |
| **NFR-01..05** *(System Parity)* | Sustained Direct-I/O Benchmark (64 MiB x 3) | 25.3-25.5 MB/s sustained read throughput (86.8% stock `usb-storage`); zero kernel memory leaks | **PASS (100%)** |
