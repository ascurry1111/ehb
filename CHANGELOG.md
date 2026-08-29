# Changelog

## v0.1 — 2026-08-29

Initial prototype.

- 3D-printed handbell body ([3dprint/handbell.scad](3dprint/handbell.scad)), plus test prints for the battery cradle, cone collar, feather plate, LIS3DH plate, and rod collar mounts.
- Firmware for the Feather ESP32 V2 reading an LIS3DH over I2C and detecting ring gestures ([firmware/feather_transmitter](firmware/feather_transmitter)).
- Ring events transmitted wirelessly two ways:
  - ESP-NOW to an ESP32-DEVKITC-V4 receiver, forwarded over USB serial ([firmware/devkit_receiver](firmware/devkit_receiver)).
  - BLE directly to a PC.
- PC-side test listeners for both paths, playing a tone on ring detection ([firmware/receiver_tests](firmware/receiver_tests)).
