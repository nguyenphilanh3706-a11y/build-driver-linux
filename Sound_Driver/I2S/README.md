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

File: `i2sdemo.c`

The codec component registers a custom Digital Audio Interface (DAI) named `i2sdemo-hifi`.

Its playback configuration declares:

- Channels: 1–2
- Sample rates: 8–96 kHz
- Sample formats: 16-bit, 24-bit, and 32-bit little-endian PCM


Rename or adjust the files as necessary to match the actual filenames in the repository.

## 6. Technical Notes

### Role of DMA and I2S

The custom driver configures the audio-card relationship and logs stream events. The existing platform driver handles the I2S controller and DMA operations that transfer PCM samples from memory to the hardware interface.

### Device Tree Overlay

The overlay describes the audio components and their relationships. It does not transmit audio samples or independently create the ALSA PCM interface.