# Zephyr Harmonic Scanner (Pico W Super TFT)

This directory contains the "Daddy" version of the Pico W Harmonic Scanner, built on the Zephyr RTOS architecture. This C-version of the scanner operates entirely bare-metal, interacting directly with the ST7796 display and Wi-Fi logic to generate zero-latency geometric harmonographs.

## Thermodynamic Optimizations
To preserve system energy and reduce thermodynamic load on the RP2040 core, the `led_strip` (NeoPixel) dependency has been entirely removed from the codebase. The breadboard RGB LED is no longer addressed, significantly shrinking the compiled firmware footprint and eliminating unnecessary SPI/PIO overhead.

## Phase-Locked Deployment (4096-Byte Anchor)
Deploying the `.uf2` file to the RP2040 bootloader must be executed using perfectly phase-locked data structures to prevent `xhci-hcd` buffer overruns on the Pi 500+ Citadel host.
```bash
dd if=build/zephyr/zephyr.uf2 of=/media/user/RPI-RP2/zephyr.uf2 bs=4096 status=progress
```
Explicitly setting `bs=4096` perfectly aligns the transfer with the hardware block boundary, establishing a "crystalline memory state" and allowing the host USB controller to effortlessly shed the device once flashing is complete.

## Peachish Physics Anchor
The geometric physics engine relies on `current_rssi` to calculate `energy_target` and subsequent pendulum amplitudes. To accurately reflect the true "zero-energy" state at boot, the default `current_rssi` is anchored to `-90`. This mathematically starves the engine, drawing a perfectly centered, low-amplitude, low-energy "peachish" dot (`color565(255, 0, 50)`) before active network energy kicks in to expand the harmonograph.
