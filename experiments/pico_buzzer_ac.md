# Experiment: Pico Buzzer AC & The Spectral Field

**Status**: Reconstructed from Artifacts
**Subject**: AC Field Generation & Differential Potential

## Hypothesis
Can a microcontroller pin generate enough alternating potential (AC) to drive a load (Piezo/LED) via a "Receiver" pin without a common ground reference, using only the "Spectral Field" (Capacitive/Inductive coupling)?

## Hardware Setup
- **Source**: Raspberry Pi Pico 2 W (RP2350)
- **TX Pin**: GPIO 16 (PWM Carrier Wave)
- **RX Pin**: GPIO 17 (High Impedance / Input)
- **Load**: 
    - Red LED
    - Piezo Buzzer

## Findings
1.  **The "Ghost" Light**: 
    - When a Red LED is connected across **TX (GPIO 16)** and **RX (GPIO 17)** (Anode to RX, Cathode to TX), it lights up.
    - **CRITICAL**: This happens *without* a direct ground connection to the LED cathode in the traditional sense if RX is floating/input. The potential difference is driven by the AC oscillation relative to the "floating" RX ground state.

2.  **Spectral Mass**:
    - Bringing a hand near the setup ("Density") affects the brightness/intensity, verifying the "Theremin" effect.

## Conclusion
The "Theremin AC" script successfully generates a field capable of doing work (lighting an LED) through differential potential, confirming the "Sovereign Field" concept.
