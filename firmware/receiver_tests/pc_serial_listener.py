#!/usr/bin/env python3
"""
Wireless Handbell — PC listener for the ESP-NOW path.

Reads "RING,<id>,<peakMilliG>,<txMs>,<rxMs>" lines from the ESP32-DEVKITC-V4
over USB serial (it's just relaying what it hears over ESP-NOW) and plays a
bell tone. Louder swings (higher peakMilliG) play a brighter/louder tone.

INSTALL
    pip install pyserial numpy sounddevice

USAGE
    python pc_serial_listener.py --port COM5          (Windows)
    python pc_serial_listener.py --port /dev/tty.usbserial-XXXX   (macOS)
    python pc_serial_listener.py --port /dev/ttyUSB0  (Linux)

    Run with --list to see available serial ports.
"""

import argparse
import sys
import time

import numpy as np
import serial
import serial.tools.list_ports
import sounddevice as sd

SAMPLE_RATE = 44100
BASE_FREQ_HZ = 880.0      # base bell pitch; tweak to taste
TONE_DURATION_S = 0.9


def list_ports():
    ports = serial.tools.list_ports.comports()
    if not ports:
        print("No serial ports found.")
        return
    print("Available serial ports:")
    for p in ports:
        print(f"  {p.device}  —  {p.description}")


def bell_tone(peak_g: float) -> np.ndarray:
    """Synthesize a simple bell-like tone: a fundamental plus a couple of
    inharmonic overtones (rough approximation of bell timbre), with an
    exponential decay envelope. Volume/brightness scale with peak_g."""
    t = np.linspace(0, TONE_DURATION_S, int(SAMPLE_RATE * TONE_DURATION_S), endpoint=False)

    # Map accelerometer peak (roughly 1.0g resting .. ~6g hard swing) to a 0..1 "strike strength"
    strength = np.clip((peak_g - 1.0) / 5.0, 0.15, 1.0)

    fundamental = 1.0
    overtone1 = 2.4     # bells have inharmonic (non-integer) overtones
    overtone2 = 4.1

    envelope = np.exp(-t * (2.5 + 1.5 * (1 - strength)))  # harder strikes ring a touch longer
    signal = (
        1.00 * np.sin(2 * np.pi * BASE_FREQ_HZ * fundamental * t)
        + 0.35 * strength * np.sin(2 * np.pi * BASE_FREQ_HZ * overtone1 * t)
        + 0.15 * strength * np.sin(2 * np.pi * BASE_FREQ_HZ * overtone2 * t)
    ) * envelope

    signal *= strength           # overall loudness follows strike strength
    signal /= np.max(np.abs(signal)) + 1e-9
    return (signal * 0.8).astype(np.float32)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", help="Serial port, e.g. COM5 or /dev/ttyUSB0")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--list", action="store_true", help="List serial ports and exit")
    args = parser.parse_args()

    if args.list or not args.port:
        list_ports()
        if not args.port:
            sys.exit(0)

    print(f"Opening {args.port} @ {args.baud}...")
    ser = serial.Serial(args.port, args.baud, timeout=1)
    time.sleep(2)  # let the board finish resetting after the port opens
    print("Listening for rings. Ctrl+C to quit.")

    try:
        while True:
            line = ser.readline().decode(errors="ignore").strip()
            if not line:
                continue
            if not line.startswith("RING,"):
                print(f"[board] {line}")
                continue

            try:
                _, ring_id, peak_milli_g, tx_ms, rx_ms = line.split(",")
                peak_g = int(peak_milli_g) / 1000.0
            except ValueError:
                print(f"[unparsed] {line}")
                continue

            print(f"Ring #{ring_id}  peak={peak_g:.2f}g")
            sd.play(bell_tone(peak_g), SAMPLE_RATE)

    except KeyboardInterrupt:
        print("\nStopping.")
    finally:
        ser.close()


if __name__ == "__main__":
    main()
