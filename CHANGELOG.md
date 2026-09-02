# Changelog

## v0.2 — 2026-09-01

Live-demo path: Android phone as receiver, BLE-only, tuned for low latency.
Confirmed working on hardware end to end, at ~22ms average ring-to-sound.

Known items deliberately left for v0.3, all documented in the relevant
README rather than only here:
- Dynamic level varies noticeably between rings that feel identical by hand
  (root cause not yet identified — see the tuning-TODO list in
  `firmware/README.md`).
- Ring/damp thresholds are reasoned starting points refined by feel, not
  measured against trace data across a range of ringers.
- Karplus-Strong tuning drifts at the very top of the pitch range
  (integer-sample delay length; fixable with fractional-delay interpolation).
- Raising the accelerometer ODR would cut a few ms of latency but requires
  re-deriving three sample-count-based tuning constants first.

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
- Expanded from three ad-hoc tone "buckets" to the six standard musical
  dynamic levels (pp–ff), evenly spread across the bell's observed realistic
  peak range (1.6g–5.0g). New `DynamicLevel.kt` is the single source of
  truth for both tone/volume (`RingPlayer`) and the detected-dynamic display
  (shown next to peak-g and in the ring log). Also fixed a real bug where
  playback volume never actually varied between levels — a per-buffer
  normalization step was canceling out the loudness scaling, leaving only a
  timbre difference — and widened the volume spread to ~22dB pp-to-ff.
  (Known open issue: dynamic level still varies noticeably between rings
  that feel identical by hand — see `firmware/README.md`'s tuning-TODO list.)
- Sustain and damp: tones now ring out for several seconds with a natural
  decay (longer for louder dynamics) instead of a ~1s fixed blip, and a
  **damp** gesture cuts the tone short, modeling how a real handbell is
  stopped by pressing it to the chest/shoulder. New `BLE_CHAR_DAMP_UUID`
  characteristic; ring and damp share one state machine (`RING_IDLE`/
  `RING_ARMED_FORWARD`/`RING_ARMED_DAMP`/`RING_SETTLING`). The bell is
  treated as physically monophonic on the Android side — a new ring replaces
  whatever's currently sounding rather than layering. Also fixed a latent
  bug found while extending this: the old peak-velocity tracker only ever
  recorded positive (forward) values, so it read ~0 during a backward swing.
- Damp detection made omnidirectional. It was first implemented as the
  mirror image of ring detection (backward motion along the forward axis),
  which forced the ringer to rotate the bell in-hand to damp it — arm
  geometry means the bell actually comes back to the body around 45° off the
  ring plane. Damp now integrates the **full 3D velocity vector**, arms on
  speed in any direction outside a forward exclusion cone
  (`DAMP_EXCLUSION_COS`, default 45°), and fires on deceleration measured
  along the direction of travel rather than any fixed axis. Ring detection
  stays single-axis and directional, as the clapper physics require — the
  exclusion cone is what keeps a forward swing from damping its own tone.
- Fixed ring/damp detection dropping ~60% of gestures (regression from the
  omnidirectional damp change above). Two structural bugs:
  1. **Arming race.** Ring and damp each had their own armed state, entered
     on their own threshold. But `speed >= |forwardVelocity|` by definition,
     so on a forward swing the damp threshold was always crossed first, and
     a real swing's arc wanders in and out of any forward cone — so the damp
     state routinely stole ring gestures, which then landed as silent damps.
     Tuning the cone only traded ring misses for damp misses. Now there is
     ONE armed state and the gesture is classified **at the stop**, from the
     peak forward velocity over the whole motion, which separates the two
     cleanly.
  2. **Settle deadlock.** Settling waited for the bell to come to rest, but
     ringing and then damping is one continuous motion — the bell never
     stops in between — so the detector sat in settling right through the
     damp. It now waits only for the refractory window and the deceleration
     spike to pass.
  Also added `POST_RING_DAMP_LOCKOUT_MS`, needed by the new design: the
  strike's recoil is real motion ending in a real deceleration, and would
  otherwise read as a damp and kill the tone it just started.
- Damps now fire on *contact* rather than on deceleration magnitude alone.
  Lowering `DAMP_STOP_DECEL_THRESHOLD` far enough to catch gentle damps also
  caught the arm's own slowdown while approaching the shoulder — the two are
  similar in magnitude, so no threshold separates them. What does separate
  them is suddenness: contact changes acceleration within milliseconds, an
  arm slowdown ramps over 100ms+. A damp now additionally requires a jerk
  spike (`DAMP_CONTACT_JERK`), which lets the deceleration threshold stay low
  enough for soft damps without firing mid-approach. Ring keeps a
  magnitude-only test — its threshold is already well above any voluntary arm
  motion.
- Soft damps now register. A damp can be a far gentler motion than a ring —
  resting the casting against a shoulder rather than arresting a committed
  swing — so it gets its own, much lower stop threshold
  (`DAMP_STOP_DECEL_THRESHOLD`, half the ring's), and `ARM_SPEED` was lowered
  so gentle gestures arm at all. The fix above had merged the two thresholds
  on the reasoning that the stop must be detected before the gesture can be
  classified; that was wrong — the classification input is a running peak
  available at every sample, so the applicable threshold is known
  continuously.
- Manual **Damp** button in the app, for stopping a tone by hand. Acts on
  the local audio stream, so it works with or without the bell connected.
- Raised the dynamic volume floor from 0.08 to 0.20 (now ~14dB pp-to-ff
  instead of ~22dB) — pp was too quiet to hear comfortably. A phone
  speaker's quiet end has to stay above the room, not just above silence.
- Pitch selection. New `HandbellPitch.kt` covers the standard chromatic range
  of a 5-octave handbell set (C3–C8, 61 pitches, flat spellings per handbell
  convention). A dropdown near the top of the app picks the pitch, defaulting
  to `A5` (880Hz, what the tone was already tuned to) and persisting the
  choice across restarts. `RingPlayer` re-synthesizes all six dynamic levels
  at the new frequency on a background thread; the previous pitch's tones
  stay playable until the new ones are ready (generation-counter guarded
  against rapid changes racing each other), so a ring mid-change never drops.
- Instant +/- half-step nudge buttons flanking the pitch dropdown. These
  don't re-synthesize (the dropdown's ~1s re-synthesis was too slow for a
  one-button nudge) — a semitone is a SoundPool playback-rate change of
  2^(1/12), so a nudge just replays the already-loaded tones at a shifted
  rate. `RingPlayer` lets that drift up to 9 semitones (safely inside
  SoundPool's 0.5–2.0 rate range) before quietly re-synthesizing in the
  background to rebase, invisibly to the caller.
- Fixed harmonic aliasing at high pitches. `synthesizeBellTone()`'s upper
  harmonics (2.4x/4.1x the fundamental) were inaudibly above Nyquist at the
  original fixed A5 tuning, but pitch selection's range now reaches C8
  (4186Hz), where the same harmonics land at ~10–17kHz — above this app's
  22050Hz sample rate's Nyquist, where they'd fold back down as audible
  noise rather than simply disappear. Each harmonic now fades out as it
  approaches Nyquist instead.
- Instrument selection: a **Sound** dropdown picks between four procedural
  synthesis voices (Bell, Piano, Guitar, Electric Guitar) — new
  `Instrument.kt`, and per-instrument raw-waveform generators in
  `RingPlayer.kt` (`synthesizeBellRaw()` is the renamed original tone).
  Guitar/Electric Guitar use Karplus-Strong (a noise-filled delay line fed
  back through a lowpass + decay) rather than summed sine waves — a
  genuinely different, and cheaper, algorithm, chosen because a plucked
  string's character comes from resonating noise rather than summing pure
  tones. Piano uses a true harmonic series (unlike the bell's deliberately
  inharmonic partials) with higher harmonics damping faster than the
  fundamental. Switching instruments re-synthesizes on a background thread,
  same safety properties as the pitch dropdown. Organ/trumpet/clarinet were
  considered and dropped — sustained/blown voices don't fit the
  strike-and-decay model everything else here is built around. A soundfont +
  native synth engine (e.g. FluidSynth) would give authentic sampled
  instrument timbres across a much wider set, but was set aside as a
  separate, much larger undertaking requiring a native/JNI dependency and a
  large bundled asset that couldn't be built and verified without a real
  device to test against throughout — see the "Instruments" section of the
  Android README for the full reasoning.
- Instrument changes now block rather than fall back. Hearing the old pitch
  for one more ring mid-transition is harmless; hearing the wrong instrument
  entirely reads as a bug, not a brief delay. `RingPlayer.setInstrument()`
  now suppresses playback for the ~1s re-synthesis window (a ring landing
  mid-change is still counted/logged, just silently — only its sound is
  dropped) and a new `setOnBusyChangedListener` drives a full-screen
  overlay with a status message that also blocks touches to the rest of
  the UI, so the controls can't be poked again mid-change to start a
  second overlapping request. Pitch changes are unaffected and keep their
  existing fall-back-to-old-tone behavior. Synthesis failures (e.g. an
  unexpected exception mid-render) now always clear the busy state rather
  than risking a permanently grey-locked UI, given the new stakes of
  leaving it stuck.

- Latency reduction pass, targeting the measured ~30ms ring-to-sound time.
  Three fixes, all on the path itself:
  - **App: play the tone on the BLE callback thread**, before hopping to the
    main thread. `onRing` previously went through `runOnUiThread` and then did
    a `String.format` and three view updates *before* calling `play()` — despite
    a comment claiming it played first with "no extra work before it." The
    sound was waiting on the main looper (behind any in-progress frame) and
    then on the app's own UI work. Split into `onRingUi` for everything
    non-urgent; likely the largest single win.
  - **Firmware: notify before logging.** `emitRing()` ran a `Serial.printf`
    with three `%f` conversions *before* `notify()`, putting float formatting
    and a 115200-baud write directly in the latency path. Pure reordering.
  - **Firmware: I2C at 400kHz** (`Wire.setClock`) instead of Arduino's 100kHz
    default. Cuts ~0.6ms of blocking bus time per sample read down to ~0.15ms,
    and that time is on the path since detection can't run until the read
    finishes.
  Also added `audioLock` in `RingPlayer` to serialize `play()`/`damp()` and the
  damp fade steps — they used to get that for free by both running on the main
  thread, so it's required now that `play()` comes in off-thread.
  Documented the full remaining latency budget in the Android README,
  including why the BLE connection interval (~5–11ms) is a hard floor and why
  raising the accelerometer ODR is *not* a safe latency tweak (three tuned
  constants are expressed in samples, not time, and would silently change
  meaning).

- App launcher icon, replacing the stock Android placeholder: the gold
  casting tipped 45° (mouth up-left, handle down-right, matching the build
  photo) over a radial champagne-to-slate-grey gradient, with a small
  Bluetooth badge upper right. Pure vector adaptive icon — two
  `VectorDrawable`s, no raster assets at any density, and no legacy PNG
  fallback needed since `minSdk` is 31. The bell is authored upright and
  tipped by a single group rotation, so the geometry stays editable; every
  point is inside the radius-36 adaptive-icon safe zone (values checked and
  recorded in the file header).

## v0.1 — 2026-08-29

Initial prototype.

- 3D-printed handbell body ([3dprint/handbell.scad](3dprint/handbell.scad)), plus test prints for the battery cradle, cone collar, feather plate, LIS3DH plate, and rod collar mounts.
- Firmware for the Feather ESP32 V2 reading an LIS3DH over I2C and detecting ring gestures ([firmware/feather_transmitter](firmware/feather_transmitter)).
- Ring events transmitted wirelessly two ways:
  - ESP-NOW to an ESP32-DEVKITC-V4 receiver, forwarded over USB serial ([firmware/devkit_receiver](firmware/devkit_receiver)).
  - BLE directly to a PC.
- PC-side test listeners for both paths, playing a tone on ring detection ([firmware/receiver_tests](firmware/receiver_tests)).
