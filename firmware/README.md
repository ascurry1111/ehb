# Wireless Handbell Prototype

## What's in here

```
feather_transmitter/feather_transmitter.ino   Feather ESP32 V2 — ring detection, BLE tx (v0.2: BLE-only)
devkit_receiver/devkit_receiver.ino           ESP32-DEVKITC-V4 — ESP-NOW rx, forwards to USB serial (v0.1 path)
receiver_tests/pc_serial_listener.py          PC — plays a tone from the ESP-NOW/serial path (v0.1 path)
receiver_tests/pc_ble_listener.py             PC — plays a tone from the BLE path directly
```

**v0.2 note:** `feather_transmitter.ino` was changed to BLE-only for the
Android demo (Android has no ESP-NOW support), which also removes the
WiFi/BLE radio-coexistence jitter noted below. The ESP-NOW + BLE dual-transport
version, and `devkit_receiver.ino`/`pc_serial_listener.py`'s upstream
counterpart, are preserved at the `v0.1` git tag if you need that path again.
For the live demo, see [`../android/HandbellReceiver`](../android/HandbellReceiver)
instead of the PC listeners.

## Wiring

LIS3DH → Feather ESP32 V2 (I2C):
- VIN → 3V
- GND → GND
- SDA → SDA
- SCL → SCL

If you have the STEMMA QT versions of both boards, just use the STEMMA QT cable and skip the wiring above entirely.

## Bring-up order (v0.2, BLE-only demo path)

0. **Set the mounting orientation first** — see [Ring detection](#ring-detection) below. Nothing will trigger correctly until `FORWARD_AXIS`/`FORWARD_SIGN` match your physical mounting.
1. **Flash `feather_transmitter.ino`** to the Feather. Open its Serial Monitor at 115200 baud — you should see its BLE address printed, then `RING #n peak=...g swing=...m/s` lines whenever you swing the bell forward and stop it.
2. **Test the BLE → PC path** (quick sanity check without the phone app): run `pc_ble_listener.py`. Swing the bell — you should hear a tone on the PC.
3. **Test the BLE → phone path**: build and run the Android app in
   [`../android/HandbellReceiver`](../android/HandbellReceiver) on the demo
   phone. Swing the bell — you should see the ring counter increment and hear
   a tone from the phone.

## Bring-up order (v0.1, ESP-NOW + BLE dual-transport — see the `v0.1` git tag)

1. **Flash `devkit_receiver.ino`** to the ESP32-DEVKITC-V4 first and open its Serial Monitor at 115200 baud. It prints its own MAC address on boot — copy it.
2. **Paste that MAC** into `RECEIVER_MAC` near the top of `feather_transmitter.ino`.
3. **Flash `feather_transmitter.ino`** to the Feather. Open its Serial Monitor too — you should see `My MAC: ...` and then `RING #n peak=...g` lines whenever you swing/tap the accelerometer.
4. Watch the DEVKITC-V4's Serial Monitor — you should see matching `RING,...` lines arrive there over ESP-NOW.
5. **Test the ESP-NOW → PC path**: run `pc_serial_listener.py --list` to find the DEVKITC-V4's port, then `pc_serial_listener.py --port <that port>`. Swing the bell — you should hear a tone.
6. **Test the BLE → PC path**: run `pc_ble_listener.py` (the DEVKITC-V4 isn't involved in this path at all — the Feather talks straight to the PC's Bluetooth radio). Swing the bell — you should hear a tone.

## Ring detection

Detection models how a real handbell actually rings — a forward swing followed
by a **sudden stop** (which is when the clapper catches up and strikes) — rather
than "acceleration crossed a threshold," which is why earlier versions rang when
you merely picked the bell up or tapped the handle. See the `RING DETECTION`
comment at the top of `feather_transmitter.ino` for the full rationale.

### First: set the mounting orientation

**Detection will not work until `FORWARD_AXIS` / `FORWARD_SIGN` match how your
LIS3DH is physically mounted.** To find them, set `CALIBRATION_MODE 1`, reflash,
and open Serial Monitor at 115200:

1. Hold the bell still in the ready position. Whichever `g=[]` value sits near
   ±9.8 is the axis pointing along gravity — that is *not* your forward axis.
2. Swing the bell forward and watch `lin=[]`. The axis that swings strongly
   **positive** as the bell moves forward is `FORWARD_AXIS`, with
   `FORWARD_SIGN +1.0`. If it swings strongly negative, same axis but
   `FORWARD_SIGN -1.0`.
3. Set both, return `CALIBRATION_MODE` to `0`, reflash.

Sanity check: with the right settings, `vFwd` reads strongly positive during a
forward swing and near zero at rest.

### Then: tune the thresholds

These interact, so change one at a time and watch the `RING #n peak=..g
swing=..m/s` lines while ringing by hand:

- `SWING_ARM_VELOCITY` (m/s) — how fast the bell must actually be travelling
  forward before a stop can ring it. **Raise it** if handling still rings the
  bell; **lower it** if genuine swings are missed. Compare against the
  `swing=` figure printed on each ring.
- `STOP_DECEL_THRESHOLD` (m/s²) — how abruptly the bell must stop. **Raise it**
  if soft stops ring; **lower it** if you have to stop the bell unnaturally
  hard.
- `SWING_RELEASE_VELOCITY` / `REFRACTORY_MS` — raise if one motion produces
  multiple rings.

The defaults are reasoned starting points, not measured ones — expect to adjust
them against your actual bell. Some of that is unavoidable: with only an
accelerometer, bell rotation during the swing leaks a little gravity into the
forward axis (see the `KNOWN LIMITATION` note in the sketch), so the right
thresholds are empirical rather than derivable.

## Android

The BLE side ports directly to Android — same GATT service/characteristic UUIDs, same wire format (`RingEvent`: uint32 ringId, uint16 peakMilliG, uint32 timestampMs, little-endian). Kotlin outline:

```kotlin
// After connecting via BluetoothGatt and discovering services:
val ringChar = gatt.getService(UUID.fromString("6e400001-b5a3-f393-e0a9-e50e24dcca9e"))
    .getCharacteristic(UUID.fromString("6e400002-b5a3-f393-e0a9-e50e24dcca9e"))
gatt.setCharacteristicNotification(ringChar, true)
val cccd = ringChar.getDescriptor(UUID.fromString("00002902-0000-1000-8000-00805f9b34fb"))
cccd.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
gatt.writeDescriptor(cccd)

// In onCharacteristicChanged: parse the 10-byte payload with a little-endian
// ByteBuffer (ringId: Int, peakMilliG: Short as UShort, timestampMs: Int),
// then play a tone with SoundPool or a synthesized AudioTrack buffer, same
// idea as bell_tone() in pc_ble_listener.py.
```

I stopped short of a full Android Studio project since it's a much bigger scaffold (Gradle, permissions, activity lifecycle) than the prototype needs right now — happy to build that out once the BLE path is confirmed working on PC, if you want the full app.

## A note on BLE + ESP-NOW running together

Both use the ESP32's single 2.4GHz radio, just different protocol stacks (Bluetooth vs Wi-Fi-based ESP-NOW). Running both at once is supported and commonly done, but you may see occasional timing jitter or a dropped packet under heavy traffic since the radio is time-shared. For a bell that rings occasionally rather than continuously, this shouldn't be noticeable — but if you see issues, test each transport with the other's code commented out to isolate.
