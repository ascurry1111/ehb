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
- Battery telemetry, sampled at low priority (every 5s, never gating the
  ring path): percentage, estimated time remaining, and whether the bell is
  charging / running on USB with no battery — all inferred from a single
  voltage reading over time, since the Feather V2 has no fuel-gauge chip.
  Sent over a second BLE characteristic and shown in the Android app.
- End-to-end ring-to-tone latency, shown in the app on every ring. A new
  clock-sync characteristic lets the phone estimate the offset between its
  own clock and the Feather's `millis()`, so `RingEvent.timestampMs` can be
  compared against the phone's clock. Breaks the total down into
  ring→phone (BLE) and phone→sound (app) legs.
- Ring detection rewritten to model real handbell physics: a forward swing
  followed by a sudden stop, rather than a bare acceleration threshold.
  Gravity is now filtered out, forward acceleration is integrated into a
  velocity, and a ring fires only when the bell was genuinely travelling
  forward and then decelerated sharply. This rejects the old false triggers
  (picking the bell up, tapping the handle, backswing, and bursts of
  multiple rings per motion). LIS3DH moved to 400Hz/12-bit high-resolution,
  since the velocity integration needs resolution more than raw sample rate.
  Requires setting `FORWARD_AXIS`/`FORWARD_SIGN` for your mounting — there's
  a `CALIBRATION_MODE` to determine them.
- UI pass: battery moved to a small, dim top-left corner (out of the way);
  latency defaults to just the total, tap it to toggle the breakdown; added
  a scrolling ring log (newest first, same info as "last ring") with a
  Clear control.

## v0.1 — 2026-08-29

Initial prototype.

- 3D-printed handbell body ([3dprint/handbell.scad](3dprint/handbell.scad)), plus test prints for the battery cradle, cone collar, feather plate, LIS3DH plate, and rod collar mounts.
- Firmware for the Feather ESP32 V2 reading an LIS3DH over I2C and detecting ring gestures ([firmware/feather_transmitter](firmware/feather_transmitter)).
- Ring events transmitted wirelessly two ways:
  - ESP-NOW to an ESP32-DEVKITC-V4 receiver, forwarded over USB serial ([firmware/devkit_receiver](firmware/devkit_receiver)).
  - BLE directly to a PC.
- PC-side test listeners for both paths, playing a tone on ring detection ([firmware/receiver_tests](firmware/receiver_tests)).
