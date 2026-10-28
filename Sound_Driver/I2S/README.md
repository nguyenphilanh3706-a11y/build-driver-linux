# I2S Sound Driver — Raspberry Pi Zero W

## Link Youtube Demo Video

[![Watch the I2S Sound Driver Demo](https://img.youtube.com/vi/K5VWboX7QfQ/0.jpg)](https://youtu.be/K5VWboX7QfQ)

## 1. Overview

This project demonstrates a custom Linux sound driver based on the **ALSA System on Chip (ASoC)** framework for audio playback on the Raspberry Pi Zero W.

The project connects the Raspberry Pi's I2S controller to a MAX98357A I2S digital audio amplifier using a custom codec component, a machine driver, and a Device Tree Overlay.

The primary goal is to understand how Linux registers an audio card, exposes a PCM playback interface, and configures the relationship between the CPU Digital Audio Interface (CPU DAI) and the codec Digital Audio Interface (Codec DAI).

## 2. Hardware

| Component | Description |
|---|---|
| Raspberry Pi Zero W | Linux host and I2S controller |
| MAX98357A | I2S digital audio amplifier |
| Speaker | Audio output |
| MicroSD card | Linux operating system and driver files |

**Audio interface:** I2S (Inter-IC Sound)

**Driver framework:** ALSA / ASoC

**Device configuration:** Device Tree Overlay

> Note: The MAX98357A receives digital audio over I2S and amplifies the signal to drive a speaker. It does not use I2C for audio data transmission.

## 3. Project Objectives

- Understand the Linux ASoC architecture.
- Implement a custom codec component and machine driver.
- Register an audio card through the ASoC framework.
- Configure the I2S controller and audio device using Device Tree.
- Inspect driver callbacks during playback.
- Verify audio-card registration and PCM playback through ALSA utilities.

## 4. Architecture

```text
          User Space
    +--------------------+
    |        aplay       |
    +--------------------+
              |
              v
    +--------------------+
    |     ALSA PCM       |
    |   /dev/snd/pcm...  |
    +--------------------+
              |
              v
    +--------------------+
    |    ALSA / ASoC     |
    +--------------------+
              |
       +------+------+
       |             |
       v             v
+-------------+ +----------------+
| Machine     | | Codec Component|
| Driver      | | i2sdemo-hifi   |
+-------------+ +----------------+
       |             |
       v             v
+-------------+ +----------------+
| Raspberry Pi| |    MAX98357A   |
| I2S + DMA   | | I2S Amplifier  |
+-------------+ +----------------+
                       |
                       v
                   +--------+
                   | Speaker|
                   +--------+
```

The diagram represents the functional relationships between the software and hardware components.

The actual audio data path is:

**PCM buffer in RAM → DMA → Raspberry Pi I2S controller → MAX98357A → Speaker**

The ASoC driver configures the audio components and registers the sound card. The existing Raspberry Pi I2S platform driver and DMA engine handle the hardware data transfer.

## 5. Driver Components

### 5.1. Codec Component

File: `i2sdemo.c`

The codec component registers a custom Digital Audio Interface (DAI) named `i2sdemo-hifi`.

Its playback configuration declares:

- Channels: 1–2
- Sample rates: 8–96 kHz
- Sample formats: 16-bit, 24-bit, and 32-bit little-endian PCM

The component is registered using:

```c
devm_snd_soc_register_component(
    &pdev->dev,
    &i2sdemo_component,
    &i2sdemo_dai,
    1
);
```

The `hw_params` callback logs the selected sample rate, number of channels, and sample width.

The `trigger` callback logs playback `START` and `STOP` events.

**Important:** This is a student demonstration component. It models the codec-side DAI in software; it does not implement a complete hardware-specific MAX98357A codec driver.

### 5.2. Machine Driver

File: `i2sdemo.c`

The machine driver connects the CPU DAI to the codec DAI and registers the sound card.

Its main responsibilities include:

- Reading the CPU I2S controller and codec references from Device Tree.
- Configuring the CPU, codec, and platform components.
- Defining the I2S audio format and clock relationship.
- Registering the sound card with ASoC.

The driver uses:

```c
devm_snd_soc_register_card(dev, &priv->card);
```

The sound card is named `i2sdemo` by default.

### 5.3. Device Tree Overlay

File: `i2sdemo-overlay.dts`

The overlay enables the Raspberry Pi I2S controller and declares the custom codec and sound-card nodes.

Key properties include:

```dts
compatible = "student,i2sdemo-codec";
compatible = "student,i2sdemo-card";

i2s-controller = <&i2s_clk_producer>;
audio-codec = <&i2sdemo_codec>;
```

These properties allow the kernel to match the Device Tree nodes with the corresponding platform drivers and establish the relationship between the CPU DAI and codec DAI.

### 5.4. Makefile

The Makefile builds the kernel module and compiles the Device Tree Overlay.

Expected build outputs:

- `i2sdemo.ko` — kernel module
- `i2sdemo.dtbo` — compiled Device Tree Overlay

## 6. Repository Structure

```text
Sound_Driver/
├── README.md
├── i2sdemo.c
├── i2sdemo-overlay.dts
├── Makefile
└── config.txt
```

Rename or adjust the files as necessary to match the actual filenames in the repository.

## 7. Build Instructions

Run the following commands on the Raspberry Pi with the appropriate kernel headers and Device Tree compiler installed.

### Step 1. Build the Kernel Module and Overlay

```bash
make
```

The build process compiles the kernel module and generates `i2sdemo.dtbo`.

### Step 2. Install the Kernel Module

```bash
sudo install -D -m 0644 i2sdemo.ko \
    /lib/modules/$(uname -r)/extra/i2sdemo.ko

sudo depmod -a
```

### Step 3. Install the Device Tree Overlay

```bash
sudo install -D -m 0644 i2sdemo.dtbo \
    /boot/overlays/i2sdemo.dtbo
```

The correct boot configuration path may differ between Raspberry Pi OS versions. Check the system's active boot configuration before installing the overlay.

### Step 4. Configure the Overlay

Add the following line to the active Raspberry Pi boot configuration file:

```ini
dtoverlay=i2sdemo
```

Ensure that the overlay is not duplicated and that any conflicting audio configuration is disabled as appropriate for the setup.

The supplied configuration example uses `dtoverlay=i2sdemo`.

### Step 5. Load the Driver and Reboot

```bash
sudo modprobe i2sdemo
sudo reboot
```

The module name and installation procedure must match the actual build and target system. If the module is not found, verify the installation path, kernel version, and `depmod` results.

## 8. Testing and Verification

### Step 1. Check Detected Sound Cards

```bash
aplay -l
```

Look for the sound card named `i2sdemo`.

### Step 2. Inspect the ALSA Device Interface

```bash
ls -l /dev/snd/
```

ALSA creates the PCM character-device interface when the sound card is registered successfully.

### Step 3. Monitor Kernel Logs

In one terminal:

```bash
sudo dmesg -w
```

In another terminal, play a supported WAV file:

```bash
aplay -D plughw:CARD=i2sdemo,DEV=0 test.wav
```

If the card identifier differs on the target system, use the card and device reported by `aplay -l`.

### Step 4. Verify Playback Callbacks

During playback, inspect the kernel logs for messages such as:

```text
i2sdemo: card 'i2sdemo' registered
i2sdemo: hw_params rate=48000 channels=2 width=16 bits
i2sdemo: playback START
i2sdemo: playback STOP
```

The actual sample rate, channel count, and sample width depend on the selected PCM stream.

**Note:** These messages confirm driver registration and callback execution. They do not, by themselves, prove that the speaker produces audible sound. Successful physical playback also depends on the I2S configuration, hardware connections, clocking, and amplifier power supply.

## 9. Technical Notes

### ASoC vs. Character Device Driver

This project uses the Linux ASoC framework instead of implementing a standalone character driver with `cdev` and `file_operations`.

The application accesses PCM playback through the ALSA interface, such as `/dev/snd/pcmC0D0p`. ALSA and ASoC manage the sound-card interface and coordinate the registered driver components.

### Role of DMA and I2S

The custom driver configures the audio-card relationship and logs stream events. The existing platform driver handles the I2S controller and DMA operations that transfer PCM samples from memory to the hardware interface.

### Device Tree Overlay

The overlay describes the audio components and their relationships. It does not transmit audio samples or independently create the ALSA PCM interface.

## 10. Limitations and Future Work

- Implement or integrate a hardware-appropriate codec component for the MAX98357A.
- Validate physical playback across supported sample rates and PCM formats.
- Measure playback reliability and investigate underruns.
- Evaluate CPU utilization and playback latency.
- Add automated tests for audio-card registration and playback.
- Document the exact Raspberry Pi OS version, kernel version, wiring, and power requirements.

## 11. References

- [Linux Kernel Documentation — ASoC Machine Driver](https://docs.kernel.org/sound/soc/machine.html)
- [Linux Kernel Documentation — ASoC Platform Driver](https://docs.kernel.org/sound/soc/platform.html)
- [Raspberry Pi Documentation](https://www.raspberrypi.com/documentation/)

