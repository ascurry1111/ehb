#!/usr/bin/env python3
"""
Wireless Handbell — PC listener for the BLE path.

Connects directly to the Feather's BLE peripheral (no DEVKITC-V4 needed for
this path) and subscribes to ring-event notifications, then plays a tone.
Reuses the same synthesis approach as pc_serial_listener.py.

INSTALL
    pip install bleak numpy sounddevice

USAGE
    python pc_ble_listener.py

WINDOWS NOTE
    sounddevice (via PortAudio) initializes a COM apartment on import, which
    conflicts with bleak's WinRT backend if imported first — you'd see
    "Thread is configured for Windows GUI but callbacks are not working."
    To avoid that, numpy/sounddevice are imported lazily, AFTER the BLE
    connection is fully established, rather than at the top of this file.
"""

import asyncio
import struct

from bleak import BleakClient, BleakScanner

DEVICE_NAME = "WirelessHandbell"
CHAR_RING_UUID = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"  # must match the Feather firmware

SAMPLE_RATE = 44100
BASE_FREQ_HZ = 880.0
TONE_DURATION_S = 0.9

# RingEvent wire format from the firmware: uint32 ringId, uint16 peakMilliG, uint32 timestampMs
RING_EVENT_FMT = "<IHI"

# Populated by _load_audio_libs() after the BLE connection is up.
np = None
sd = None


def _load_audio_libs():
    """Import numpy/sounddevice on demand, after bleak has finished its
    WinRT setup, so PortAudio's COM initialization can't interfere with it."""
    global np, sd
    import numpy as _np
    import sounddevice as _sd
    np = _np
    sd = _sd


def bell_tone(peak_g: float):
    t = np.linspace(0, TONE_DURATION_S, int(SAMPLE_RATE * TONE_DURATION_S), endpoint=False)
    strength = np.clip((peak_g - 1.0) / 5.0, 0.15, 1.0)
    envelope = np.exp(-t * (2.5 + 1.5 * (1 - strength)))
    signal = (
        1.00 * np.sin(2 * np.pi * BASE_FREQ_HZ * t)
        + 0.35 * strength * np.sin(2 * np.pi * BASE_FREQ_HZ * 2.4 * t)
        + 0.15 * strength * np.sin(2 * np.pi * BASE_FREQ_HZ * 4.1 * t)
    ) * envelope
    signal *= strength
    signal /= np.max(np.abs(signal)) + 1e-9
    return (signal * 0.8).astype(np.float32)


def handle_notification(_, data: bytearray):
    ring_id, peak_milli_g, timestamp_ms = struct.unpack(RING_EVENT_FMT, data)
    peak_g = peak_milli_g / 1000.0
    print(f"Ring #{ring_id}  peak={peak_g:.2f}g")
    sd.play(bell_tone(peak_g), SAMPLE_RATE)


async def main():
    print(f"Scanning for '{DEVICE_NAME}'...")
    device = await BleakScanner.find_device_by_name(DEVICE_NAME, timeout=15.0)
    if device is None:
        print("Device not found. Is the Feather powered on and advertising?")
        return

    print(f"Found {device.address}, connecting...")
    async with BleakClient(device) as client:
        print("Connected. Subscribing to ring notifications...")
        await client.start_notify(CHAR_RING_UUID, handle_notification)

        # Safe to bring in the audio stack now — all of bleak's WinRT
        # setup (scan + connect + start_notify) is already done.
        _load_audio_libs()

        print("Listening for rings. Ctrl+C to quit.")
        while True:
            await asyncio.sleep(1)


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\nStopping.")