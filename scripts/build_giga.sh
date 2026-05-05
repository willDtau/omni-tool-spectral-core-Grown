#!/bin/bash

# Default to M7
CORE="m7"

# Check if first argument is m4 or m7
if [ "$1" == "m4" ]; then
    CORE="m4"
    shift
elif [ "$1" == "m7" ]; then
    CORE="m7"
    shift
fi

if [ "$CORE" == "m7" ]; then
    echo "Building for Giga R1 M7 Core..."
    # Target zephyr_d16_app for M7
    APP_DIR="zephyr_d16_app"
    BOARD="arduino_giga_r1/stm32h747xx/m7"
elif [ "$CORE" == "m4" ]; then
    echo "Building for Giga R1 M4 Core..."
    # Target zephyr_d16_m4 for M4
    APP_DIR="zephyr_d16_m4"
    BOARD="arduino_giga_r1/stm32h747xx/m4"
fi

west build -p auto -b "$BOARD" "$APP_DIR" -- "$@"
