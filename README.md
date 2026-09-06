# PPM24 to CRSF Converter

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

Convert legacy Graupner MX-22 PPM24 signals to CRSF (Crossfire Serial Protocol) for modern long-range systems like mLRS or ExpressLRS (ELRS).

This project uses an STM32F103C8T6 (Blue Pill) to decode the 12-channel PPM24 signal from older transmitters and output a CRSF stream, enabling compatibility with modern high-frequency modules without modifying the original radio hardware.

## ✨ Features

- Decodes 12-channel PPM24 signals (Graupner MX-22 PPM24 mode)
- Supports standard PPM input from other transmitters (8-12 channels)
- Outputs CRSF protocol at 420000 baud for ELRS/mLRS modules
- Optional dual PPM input: merge head-tracker PPM with transmitter PPM
- Channel mapping:
  - CRSF Ch1-4 = Transmitter Ch1-4
  - CRSF Ch5-6 = Head-tracker Ch5-6 (or default 1500µs)
  - CRSF Ch7-12 = Transmitter Ch7-12
  - CRSF Ch13-14 = Transmitter original Ch5-6
- Pure register-level code — no HAL dependencies, minimal latency

## 🧩 Hardware Requirements

| Component | Description |
|-----------|-------------|
| **MCU** | STM32F103C8T6 (Blue Pill) development board |
| **Input 1** | PPM24 signal from transmitter DSC port (e.g., Graupner MX-22) |
| **Input 2** | Optional head-tracker PPM signal (8-channel standard PPM) |
| **Output** | CRSF signal to ELRS/mLRS TX module RX pin |
| **Level Shifter** | Required for PPM inputs (7.6V → 3.3V) |

> **⚠️ Important:** The PPM signal from the DSC port can be as high as battery voltage (e.g., 11.3V). **Do not connect directly to the STM32 GPIO** — use a voltage divider or optocoupler.

## 🔌 Wiring Guide

### Basic Configuration (Transmitter only)

| STM32 Pin | Connection | Description |
|-----------|------------|-------------|
| **PB6** | Transmitter PPM signal (after level shifting) | TIM4_CH1 input capture |
| **GND** | Common ground | Connect to transmitter and module ground |
| **PA2** | ELRS/mLRS module RX pin | CRSF output at 420000 baud |

### Dual PPM Input (Transmitter + Head‑Tracker)

| STM32 Pin | Connection | Description |
|-----------|------------|-------------|
| **PB6** | Transmitter PPM signal (after level shifting) | TIM4_CH1 |
| **PA0** | Head-tracker PPM signal (after level shifting) | TIM2_CH1 |
| **GND** | Common ground | — |
| **PA2** | ELRS/mLRS module RX pin | CRSF output |

### Level Shifting (Voltage Divider)

For a 7.6V PPM signal:
- **R1 (series)**: 4.7kΩ
- **R2 (to GND)**: 3.3kΩ

Output voltage ≈ **3.13V** — safe for STM32 GPIO.
## ⚠️ Disclaimer

This project is provided as-is. Always test thoroughly on the ground before flight. The author assumes no responsibility for any damage or injury caused by the use of this software or hardware.

## 🤖 AI Disclosure

This code was developed with the assistance of artificial intelligence tools (including but not limited to code generation and debugging support).

The author has reviewed, tested, and verified all code to ensure it functions as intended. Users are encouraged to review the code themselves before use.
