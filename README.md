# Electric Handbell

A solid-state electronic handbell: a 3D-printed bell body with no moving
parts, instrumented with an accelerometer to detect ring gestures and
trigger sound electronically/wirelessly instead of with a physical clapper.

## Repo layout

```
3dprint/    OpenSCAD model for the bell body and printed test pieces
firmware/   ESP32 firmware (transmitter + receiver) and PC test tooling
```

## Hardware

- [Adafruit LIS3DH](https://www.adafruit.com/product/2809) accelerometer — senses the swing/ring motion
- [Adafruit ESP32 Feather V2](https://www.adafruit.com/product/5400) — reads the LIS3DH over I2C, transmits ring events wirelessly via ESP-NOW or BLE

See [firmware/README.md](firmware/README.md) for wiring, bring-up steps, and details on the wireless protocol.
