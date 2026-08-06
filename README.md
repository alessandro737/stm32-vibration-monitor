# STM32 Vibration Monitor

Real-time vibration monitoring firmware for the STM32F411RE, built on
FreeRTOS. Acquires accelerometer data over SPI under a hard deadline
while sharing the bus with a flash logger whose erase operations can
block for hundreds of milliseconds.

> **Status: in development — Week 1 of 6.**
> Toolchain and repository setup complete. Hardware not yet in hand.
> Nothing here builds to a running target yet.

## Overview

A high-priority acquisition task reads batches of samples from an
ADXL345 FIFO, triggered by a watermark interrupt. Downstream tasks
handle signal processing, event logging to SPI flash, and telemetry
over UART — each at different rates and with different tolerance for
delay.

The design problem is that the accelerometer and the flash share one
SPI bus, and a flash sector erase takes 45–400 ms against a ~10 ms
acquisition deadline. That conflict is the reason this is an RTOS
project rather than a superloop, and resolving it is the core of the
work.

## Hardware

| Component | Part | Role |
|---|---|---|
| MCU board | NUCLEO-F411RE | Cortex-M4F, onboard ST-LINK/V2-1 |
| Accelerometer | ADXL345 (GY-291) | SPI, 32-entry FIFO, watermark interrupt |
| Flash | W25Q64 | SPI, shares the bus with the sensor |
| Capture | FX2LP 8-channel logic analyzer | sigrok / PulseView |

## Toolchain

Developed on WSL2 (Ubuntu 24.04) with the ST-LINK forwarded via
usbipd-win. PulseView runs natively on Windows.

- `arm-none-eabi-gcc` — Arm GNU Toolchain
- OpenOCD — flashing and SWD debug
- CMake + Ninja
- FreeRTOS-Kernel, CMSIS_5 (git submodules)

## Roadmap

- [ ] **Week 1** — Toolchain, SPI driver against RM0383, ADXL345 DEVID
      read verified on the logic analyzer
- [ ] **Week 2** — FIFO watermark interrupt, ISR-to-task handoff,
      latency measurement
- [ ] **Week 3** — Task pipeline: DSP, telemetry, command parsing,
      queue backpressure policies
- [ ] **Week 4** — W25Q64 driver, shared-bus mutex, priority inversion
      reproduced and fixed
- [ ] **Week 5** — Watchdog supervisor, stack sizing from measurement,
      host-side unit tests, 24-hour soak
- [ ] **Week 6** — Measurement writeup and documentation

## Repository

src/app Task definitions and scheduling
src/drivers SPI, ADXL345, W25Q64, UART
src/bsp Clock, GPIO, startup, linker script
test/ Host-native unit tests (no cross-compiler)
captures/ Raw sigrok .sr logic analyzer captures
docs/ Engineering log

Build output is named `vibemon` (vibemon.elf, vibemon.bin, vibemon.map).

## License

MIT