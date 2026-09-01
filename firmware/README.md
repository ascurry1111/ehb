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

### Mounting orientation

`FORWARD_AXIS` / `FORWARD_SIGN` must match the physical mounting or nothing
works. They're preset to `AXIS_Z` / `-1.0` for the current build: the LIS3DH is
mounted flat on the rod's wide face with the component side outward, so the PCB
normal (Z) points at the ringer and forward — away from the ringer — is `-Z`.

**Verify the sign before tuning anything else.** The axis is confidently Z, but
the sign depends on which way the board faces and is easy to get backwards; a
flipped sign arms the detector on the backswing. Set `CALIBRATION_MODE 1`,
reflash, open Serial Monitor at 115200:

1. Hold the bell still in the ready position. The **dominant** `g=[]` value
   should be the in-plane axis running along the rod, with Z a minority
   component. If Z dominates, the board isn't mounted the way this config
   assumes and both settings need re-deriving.
2. Swing forward and confirm `vFwd` goes strongly **positive**. If it goes
   negative, flip `FORWARD_SIGN`.
3. Return `CALIBRATION_MODE` to `0`, reflash.

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
them against your actual bell.

**Set them from real data rather than guesswork:** `CALIBRATION_MODE 2` streams
the forward acceleration and velocity profile whenever the bell is moving, with
rings still firing. Ring normally a few times, then deliberately do the things
that *shouldn't* ring — pick the bell up, tap the handle, tilt it slowly
forward — and compare the `peakV` values. Set `SWING_ARM_VELOCITY` in the gap
between the two groups.

**The failure mode to watch for** is a slow forward *tilt* arming the detector.
The sensor sits well above the wrist pivot and the bell rotates through a large
angle, so gravity rotates in the sensor's frame faster than the filter tracks
it, and the residue looks like forward acceleration. If tilting alone arms it,
lower `GRAVITY_LPF_ALPHA` so gravity is tracked faster — but not too far, since
it also starts absorbing genuine swing acceleration. With a single accelerometer
this can be traded off but not eliminated; a 6-DOF IMU with a gyro (LSM6DS3)
would remove the whole class of problem.

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
