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

## Ring detection and damp

Detection models how a real handbell actually rings — a forward swing followed
by a **sudden stop** (which is when the clapper catches up and strikes) — rather
than "acceleration crossed a threshold," which is why earlier versions rang when
you merely picked the bell up or tapped the handle. See the `RING DETECTION`
comment at the top of `feather_transmitter.ino` for the full rationale.

A real handbell keeps ringing after the strike until it naturally damps out, or
until the ringer presses it to their body to stop it. The Android app plays a
multi-second decaying tone per ring rather than a short blip, and **damp** is
the gesture that cuts it short.

**Ring and damp are deliberately not symmetric.** A ring is directional — the
clapper only travels fore/aft and is sprung against striking backward — so it
stays a single-axis test on forward velocity. A damp is *omnidirectional*:
physically it's just "the casting contacted something," and in practice arm
geometry means the bell usually comes back to the body around 45° off the
ring plane, laterally. So damp detection works on the **full 3D velocity
vector**, arming on speed in any direction outside a forward exclusion cone
and firing on deceleration measured *along the direction of travel* rather
than any fixed axis. See `SUSTAIN AND DAMP` in the sketch's header comment.

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
swing=..m/s` / `DAMP speed=..m/s decel=..` lines while ringing and damping by
hand:

- `SWING_ARM_VELOCITY` (m/s) — how fast the bell must actually be travelling
  forward before a stop can ring it. **Raise it** if handling still rings the
  bell; **lower it** if genuine swings are missed. Compare against the
  `swing=` figure printed on each ring.
- `STOP_DECEL_THRESHOLD` (m/s²) — how abruptly the bell must stop to ring.
  **Raise it** if soft stops ring; **lower it** if you have to stop the bell
  unnaturally hard.
- `DAMP_ARM_SPEED` (m/s) — how fast the bell must be travelling **in any
  direction** before a stop can damp it. Compare against the `speed=` figure
  in the `CALIBRATION_MODE 2` trace.
- `DAMP_STOP_DECEL_THRESHOLD` (m/s²) — how abruptly the bell must stop to
  damp, measured along its direction of travel. Defaults slightly below
  `STOP_DECEL_THRESHOLD`, since contacting a soft body is less abrupt than
  the deliberate stop that rings the bell, and a missed damp is much less
  disruptive than a missed ring. Compare against the trace's `decel=`.
- `DAMP_EXCLUSION_COS` — **the only thing separating the two gestures.** It's
  the cosine of a cone half-angle around the forward axis; motion inside the
  cone can't damp. Default `0.707` = 45°. Raise toward 1.0 for a narrower
  cone (damps trigger more readily, but a slightly-off-axis ring swing risks
  damping its own tone); lower for a wider cone (ring better protected, but
  damps that come back near the ring axis get missed). The trace's `cosFwd`
  shows where each gesture actually falls: ~1.0 when ringing, well below the
  threshold when damping.
- `RELEASE_VELOCITY` / `REFRACTORY_MS` — shared by both gestures; raise if one
  motion produces a burst of events.

The defaults are reasoned starting points, not measured ones — expect to adjust
them against your actual bell.

**Set them from real data rather than guesswork:** `CALIBRATION_MODE 2` streams
`aFwd`, `vFwd`, `speed`, `cosFwd`, `decel` and `peakV` whenever the bell is
moving, with rings/damps still firing. Ring and damp normally a few times —
including damping from the awkward angles you'd actually use — then deliberately
do the things that *shouldn't* trigger either (pick the bell up, tap the handle,
tilt it slowly) and set each threshold in the gap between the groups.

**The failure mode to watch for** is a slow forward *tilt* arming the detector.
The sensor sits well above the wrist pivot and the bell rotates through a large
angle, so gravity rotates in the sensor's frame faster than the filter tracks
it, and the residue looks like forward acceleration. If tilting alone arms it,
lower `GRAVITY_LPF_ALPHA` so gravity is tracked faster — but not too far, since
it also starts absorbing genuine swing acceleration. With a single accelerometer
this can be traded off but not eliminated; a 6-DOF IMU with a gyro (LSM6DS3)
would remove the whole class of problem.

## Android

The full receiver app lives at [`../android/HandbellReceiver`](../android/HandbellReceiver),
not just a snippet — see its README for what it does and how to build it. The
GATT service exposes four characteristics, all under service UUID `6e400001-...`:

| Characteristic | UUID suffix | Purpose |
|---|---|---|
| Ring | `...0002` | Notify on every ring: `RingEvent` (uint32 ringId, uint16 peakMilliG, uint32 timestampMs) |
| Battery | `...0003` | Notify every 5s: `BatteryStatus` (uint8 percent, uint8 state, uint16 milliVolts, uint16 estimatedMinutesRemaining) |
| Time | `...0004` | Read-only, returns current `millis()` fresh each read — for clock-sync/latency measurement |
| Damp | `...0005` | Notify on the omnidirectional damp gesture: `DampEvent` (uint32 timestampMs) |

All multi-byte fields are little-endian.

## Known tuning issues (calibration TODO)

- **Dynamic level (peak-g) varies noticeably between rings that feel
  identical by hand.** Not yet root-caused — could be the accelerometer's ADC
  noise, the gravity-leakage effect described above, inconsistent physical
  technique, or some combination. Worth investigating with `CALIBRATION_MODE 2`
  traces of several "identical" rings side by side before retuning
  `DynamicLevel`'s g-value bands in the Android app.

## A note on BLE + ESP-NOW running together

Both use the ESP32's single 2.4GHz radio, just different protocol stacks (Bluetooth vs Wi-Fi-based ESP-NOW). Running both at once is supported and commonly done, but you may see occasional timing jitter or a dropped packet under heavy traffic since the radio is time-shared. For a bell that rings occasionally rather than continuously, this shouldn't be noticeable — but if you see issues, test each transport with the other's code commented out to isolate.
