# Changelog

## Unreleased (v0.2 in progress)

Live-demo path: Android phone as receiver, BLE-only, tuned for low latency.

- `feather_transmitter.ino`: dropped ESP-NOW/WiFi entirely (Android has no
  ESP-NOW support; also removes WiFi/BLE coexistence jitter). Raised LIS3DH
  output data rate to 1.6kHz and removed the polling throttle. Firmware now
  requests a 7.5–15ms BLE connection interval with zero slave latency as
  soon as a phone connects, and raises BLE TX power to +9dBm. The old
  dual-transport version is preserved at the `v0.1` git tag.
- New [`android/HandbellReceiver`](android/HandbellReceiver) app: scans for
  `WirelessHandbell`, subscribes to ring notifications, requests
  `CONNECTION_PRIORITY_HIGH`, and plays a pre-synthesized bell tone via
  `SoundPool` (velocity-sensitive across three peak-g buckets) with no
  synthesis work on the ring-event hot path. Auto-reconnects on drop.

## v0.1 — 2026-08-29

Initial prototype.

- 3D-printed handbell body ([3dprint/handbell.scad](3dprint/handbell.scad)), plus test prints for the battery cradle, cone collar, feather plate, LIS3DH plate, and rod collar mounts.
- Firmware for the Feather ESP32 V2 reading an LIS3DH over I2C and detecting ring gestures ([firmware/feather_transmitter](firmware/feather_transmitter)).
- Ring events transmitted wirelessly two ways:
  - ESP-NOW to an ESP32-DEVKITC-V4 receiver, forwarded over USB serial ([firmware/devkit_receiver](firmware/devkit_receiver)).
  - BLE directly to a PC.
- PC-side test listeners for both paths, playing a tone on ring detection ([firmware/receiver_tests](firmware/receiver_tests)).
