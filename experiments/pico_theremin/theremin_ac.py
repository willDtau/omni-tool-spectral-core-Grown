
# Reconstructed based on walkthrough.md description
# "The Pico Theremin"
# Hardware: Pico 2 W (RP2350)
# Pins:
#   - GPIO 16 (TX): 1kHz - 2kHz Carrier Wave
#   - GPIO 17 (RX): Density Sensor (Hand Proximity)

import machine
import time
import math

# TX: Carrier Wave (PWM)
# Variable frequency based on density to create "Theremin" effect
tx_pin = machine.Pin(16)
tx_pwm = machine.PWM(tx_pin)

# RX: Density Sensor (Input)
# We read the voltage potential impacted by hand proximity
rx_pin = machine.Pin(17, machine.Pin.IN) 
rx_adc = machine.ADC(27) # ADC 1 is typically GPIO 27, but let's assume we might read analog if it was a true density sensor. 
# However, the walkthrough mentions GPIO 17 specifically as RX. 
# If it's pure digital RX for the LED effect, it might just remain an input.
# But for a "Theremin" sound, we usually need an ADC to read the "hand distance".
# Let's assume standard ADC usage on a nearby pin or just setup 17 as high impedance.
#
# Actually, the description says: "Red LED ... lights up purely from the differential potential".
# This implies the CODE is just generating the TX field.

def main():
    print("🛸 Pico Theremin: AC Field Generator Active (GPIO 16)")
    
    # Base Frequency
    freq = 1000
    tx_pwm.freq(freq)
    tx_pwm.duty_u16(32768) # 50% Duty Cycle

    while True:
        # Sweep frequency slightly to create "Living" field?
        # Or just hold steady for the "AC" experiment.
        # "1kHz - 2kHz Carrier Wave"
        
        for f in range(1000, 2001, 10):
            tx_pwm.freq(f)
            time.sleep(0.01)
            
        for f in range(2000, 999, -10):
            tx_pwm.freq(f)
            time.sleep(0.01)

if __name__ == "__main__":
    main()
