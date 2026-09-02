/*
  ============================================================================
  Wireless Handbell — Feather ESP32 V2 Transmitter (BLE-only, low-latency)
  ============================================================================
  Reads an Adafruit LIS3DH accelerometer, detects a "ring" the way a real
  handbell rings (a forward swing followed by a sudden stop), and transmits
  the event over BLE (GATT notify) to a phone running the Handbell Receiver
  Android app.

  RING DETECTION — modeling a real handbell
    A real handbell doesn't ring from motion alone. The clapper swings only
    fore/aft, sprung so it won't strike on the backswing and needs real force
    to strike at all. What actually rings it: you swing the bell forward, you
    STOP the bell, and the clapper keeps going and hits the casting.

    So this is not "acceleration exceeded a threshold" (v0.2 and earlier —
    which is why picking the bell up or tapping the handle would false-trigger).
    Instead:
      1. Gravity is separated out with a slow low-pass filter, leaving only
         linear acceleration. (The old code compared a magnitude that had 1g
         of gravity baked into it, so orientation alone moved the number.)
      2. Forward-axis linear acceleration is integrated into a forward
         VELOCITY, using a leaky integrator so sensor bias can't drift it.
      3. The detector arms once the bell is genuinely moving (ARM_SPEED), not
         just jostled.
      4. While armed, a sharp deceleration (RING_STOP_DECEL_THRESHOLD, or the
         gentler DAMP_STOP_DECEL_THRESHOLD if this isn't shaping up to be a
         ring) counts as the bell being stopped. That's when a clapper strikes —
         and the moment the gesture gets classified: peak forward velocity at
         or above RING_FORWARD_VELOCITY means it was a ring, anything less
         means it was a damp. See DECIDE AT THE STOP below.

    Why this rejects the false triggers:
      - Tapping the handle: large acceleration spike, but it nets ~zero
        velocity, so the detector never arms.
      - Picking the bell up: slow, and mostly along gravity rather than the
        forward axis; doesn't reach arm velocity.
      - Backswing: arming requires POSITIVE forward velocity, so returning
        the bell can't ring it — the same asymmetry the clapper springs give
        a real bell, for free.
      - Multiple rings per swing: after a ring the detector must see velocity
        fall back below RELEASE_VELOCITY before it can re-arm, on top of
        REFRACTORY_MS.

    IMPORTANT — set FORWARD_AXIS/FORWARD_SIGN below to match how your LIS3DH
    is actually mounted, or none of this works. Set CALIBRATION_MODE 1 and
    follow the procedure there; it takes about 30 seconds.

  SUSTAIN AND DAMP — v0.4
    A real handbell keeps ringing after the strike until it naturally damps
    out, or until the ringer brings it to their body to stop it (touching the
    casting kills the vibration). The Android app plays a multi-second
    decaying tone per ring rather than a short fixed blip, so this firmware
    needs to tell it when to cut that tone off early. That's a "damp", the
    standard handbell term.

    RING AND DAMP ARE NOT SYMMETRIC — this is the key asymmetry to preserve:

      A ring is DIRECTIONAL. The clapper only travels fore/aft and is sprung
      against striking backward, so only a forward swing + stop rings the
      bell. That stays a single-axis test on forwardVelocity.

      A damp is OMNIDIRECTIONAL. Physically it's just "the casting contacted
      something" — any orientation works. And in practice the ringer's arm
      geometry means the bell rarely comes back along the ring axis at all:
      bringing it to the chest/shoulder typically contacts around 45° off the
      normal plane of motion, laterally. An earlier version tested only for
      backward motion along the forward axis, which forced the ringer to
      rotate the bell in-hand to damp it — exactly backwards from how the
      gesture actually wants to work.

    So damp detection works on the full 3D velocity vector, firing on a sharp
    deceleration ALONG THE DIRECTION OF TRAVEL (dot product of linear
    acceleration with the unit motion vector) rather than along any fixed
    axis. That's what makes it work at 45° lateral, straight down, or
    anywhere else.

    Damp is a separate BLE characteristic (BLE_CHAR_DAMP_UUID) rather than a
    field on RingEvent, since it's a genuinely different signal — "stop
    whatever is currently sounding," not "a new strike happened."

  DECIDE AT THE STOP, NOT AT ARMING — and why the obvious approach fails
    There is ONE armed state, not one per gesture. A motion arms on speed
    alone, and which gesture it was is decided when it stops, from the peaks
    accumulated over the whole gesture.

    The first attempt armed each gesture separately — ring when forward
    velocity passed its threshold, damp when speed passed its own while
    outside a forward cone. That cannot work, and the reason is worth
    recording so it doesn't get reintroduced:

      speed >= |forwardVelocity| ALWAYS, by definition of a vector magnitude.

    So on a forward swing, speed crosses the damp threshold strictly BEFORE
    forward velocity crosses the ring threshold. Every sample in that window
    is another chance for the damp arm to fire first, and a real swing is an
    arc with lateral and gravity-leakage components, so its direction wanders
    in and out of any forward cone you pick. The result: the damp state stole
    a large fraction of ring gestures, which then landed as silent damps
    instead of rings. Widening or narrowing the cone only trades ring misses
    against damp misses — the race is structural, not a tuning problem.

    Deciding at the stop removes the race entirely: by then the full gesture
    has happened, and peak forward velocity cleanly separates the two (a damp
    travels back toward the body, so its peak forward velocity is near zero or
    negative, nowhere near a ring's).

    The one thing this design must guard against is the strike recoil right
    after a ring reading as a damp and killing the tone it just started —
    hence POST_RING_DAMP_LOCKOUT_MS.

    Note "decided at the stop" does NOT mean the two gestures must share a
    stop threshold. The classification input — peak forward velocity — is a
    running peak available at every sample, so which gesture a motion would
    be, and therefore which threshold applies to it, is known continuously.
    They get separate thresholds precisely because they're physically
    different events: arresting a committed swing versus resting the casting
    against your shoulder.

  DON'T REQUIRE THE BELL TO STOP BETWEEN GESTURES
    A related early mistake: the post-event settle required speed to fall
    below RELEASE_VELOCITY before anything could re-arm. But ringing and then
    damping is ONE continuous motion — the bell never comes to rest in
    between — so the detector sat in settling right through the damp. Settling
    now waits only for the refractory window and for the deceleration spike
    itself to pass, never for the bell to be still.

  v0.2 NOTE — this variant drops ESP-NOW/WiFi entirely. It was built for a
  live demo where the receiver is an Android phone (Android has no ESP-NOW
  support), and removing the WiFi stack has the added benefit of eliminating
  WiFi/BLE radio coexistence arbitration, which was a source of timing
  jitter in v0.1. If you need the dual-transport (ESP-NOW + BLE) version
  again — e.g. to talk to devkit_receiver.ino/pc_serial_listener.py — it's
  preserved at the "v0.1" git tag.

  HARDWARE
    - Adafruit ESP32 Feather V2
    - Adafruit LIS3DH breakout, wired over I2C (STEMMA QT or SDA/SCL + 3V + GND)
    - 500mAh LiPo on the JST connector

  BATTERY TELEMETRY — how it works and its limits
    The Feather V2 has no fuel-gauge IC (unlike the ESP32-S2/S3 Feathers) —
    just a resistor divider from BAT to the analog pin A13, and a charge LED
    wired directly to the MCP73831 charger with no GPIO tap. So percentage,
    "charging", and "USB-powered with no battery" are all *inferred* from one
    noisy voltage reading over time, not measured directly:
      - Percentage comes from a standard LiPo voltage curve lookup (approximate).
      - A near-0V reading means no battery is physically present — since the
        board is alive to report anything at all, it must be running on USB.
      - Charging vs. discharging is judged by the *trend* over a rolling
        window (rising = charging, falling = discharging). A flat reading
        near max voltage is reported as "resting" rather than guessed at,
        since a fully-charged battery on USB and a fully-charged battery
        just sitting unplugged look identical from voltage alone.
    See BATT_* constants below to tune thresholds against your actual pack.

  LATENCY MEASUREMENT
    RingEvent.timestampMs is millis() on THIS board — meaningless compared
    directly against the phone's clock. The Android app reads
    BLE_CHAR_TIME_UUID once after connecting to estimate the offset between
    the two clocks (see TimeCharacteristicCallbacks below), then uses that
    to compute true ring-to-tone latency on its end.

  LIBRARIES (Arduino Library Manager)
    - Adafruit LIS3DH
    - Adafruit Unified Sensor
    - NimBLE-Arduino (by h2zero)   <-- NOT the stock ESP32 BLE library

  SETUP STEPS
    1. Flash this sketch to the Feather.
    2. It advertises as "WirelessHandbell" over BLE — no pairing needed.
    3. Run the Handbell Receiver Android app (see /android) to connect and
       play a tone on each ring.

  LATENCY TUNING — what changed from v0.1 and why
    - No WiFi/ESP-NOW: one fewer radio stack competing for airtime with BLE.
    - LIS3DH runs at 400Hz in HIGH RESOLUTION (12-bit) mode. v0.2 used
      1.6kHz low-power (8-bit) on the theory that raw sample rate was all
      that mattered, but the ring detector now integrates acceleration into
      velocity, and 8-bit data is too coarse to integrate without the bias
      error swamping the result. 400Hz still gives 2.5ms granularity, which
      is far below the ~10ms of BLE latency downstream, so this costs
      nothing perceptible and buys much cleaner detection.
    - Sampling is paced to the sensor's output rate rather than polled flat
      out — reading faster than the ODR just re-reads the same sample and
      wastes I2C bandwidth that BLE could be using.
    - The BLE connection interval is renegotiated down to 7.5–15ms with
      zero slave latency as soon as a phone connects, via
      NimBLEServer::updateConnParams(). Default negotiated intervals can
      land closer to 30–50ms, which adds directly to notify latency.
    - TX power raised to +9dBm to reduce the odds of a retry/retransmit
      (which would show up as a latency spike) at the cost of some battery
      life — acceptable for a short demo.
  ============================================================================
*/

#include <Wire.h>
#include <Adafruit_LIS3DH.h>
#include <Adafruit_Sensor.h>
#include <NimBLEDevice.h>

// ---------------------------------------------------------------------------
// CONFIG — tune these for your bell / mounting
// ---------------------------------------------------------------------------
// --- Mounting orientation --------------------------------------------------
// Which LIS3DH axis points in the direction the bell travels when swung
// forward, and which sign of that axis is "forward". THIS MUST MATCH YOUR
// PHYSICAL MOUNTING — see CALIBRATION_MODE below to determine it empirically.
// Set from the build photo: the LIS3DH is mounted flat on the rod's wide face,
// long axis along the rod, component side facing outward toward the ringer.
// The PCB normal (Z) therefore points out of the rod face at the ringer, and a
// handbell's fore/aft swing runs along that normal — so forward (away from the
// ringer) is -Z. VERIFY WITH CALIBRATION_MODE ANYWAY: the axis is confidently
// Z, but the sign depends on which way the board faces and is easy to get
// backwards. A flipped sign means the detector arms on the backswing instead.
#define AXIS_X 0
#define AXIS_Y 1
#define AXIS_Z 2
#define FORWARD_AXIS   AXIS_Z
#define FORWARD_SIGN   (-1.0f)

// Diagnostics. Set, reflash, open Serial Monitor at 115200, then set back to 0.
//   1 = axis identification (rings suppressed)
//       - Hold the bell still in the ready position. The DOMINANT g=[] value
//         should be the in-plane axis running along the rod (X or Y). Z should
//         be the minority component. If Z is dominant, the board isn't mounted
//         the way this config assumes — re-derive both settings below.
//       - Swing forward and watch lin=[]. Whichever axis swings hardest is
//         FORWARD_AXIS; if it swings NEGATIVE on the forward stroke, that's
//         FORWARD_SIGN -1.0 (expected here), positive means +1.0.
//       - Confirm: vFwd should go strongly POSITIVE on a forward swing. If it
//         goes negative, flip FORWARD_SIGN.
//   2 = swing trace (rings/damps still fire) — streams aFwd, vFwd, speed,
//       cosFwd, decel and peakV whenever the bell is moving, so the
//       thresholds below can be set from real numbers. Ring a few times
//       normally, damp a few times (including from the awkward angles you'd
//       actually use), then deliberately do the things that should NOT
//       trigger either (pick it up, tap the handle, tilt it slowly) and
//       compare the trace across all of them.
#define CALIBRATION_MODE 0

// --- Ring detection tuning -------------------------------------------------
#define SAMPLE_INTERVAL_US      2500   // 400Hz, matching the LIS3DH ODR set in setup()

// Gravity is tracked with a slow low-pass filter and subtracted off. Alpha is
// per-sample; 0.997 at 400Hz is a ~1s time constant — slow enough that a swing
// (a few hundred ms) doesn't get absorbed into the gravity estimate, fast
// enough to follow the bell being reoriented between rings.
//
// KNOWN LIMITATION — most likely thing to need tuning on real hardware:
// the bell rotates through its swing arc, so gravity rotates in the sensor's
// frame faster than this filter tracks it, and the residue leaks into the
// "linear" acceleration. This matters more here than it would on a
// translation-only rig: the sensor sits well above the wrist pivot, so the
// motion is largely rotational and the bell tips through a substantial angle.
// Tipping forward by θ leaks roughly g*sin(θ) into the forward axis, and only
// the fraction this filter has caught up on gets removed — enough that a slow
// deliberate tilt can accumulate phantom forward velocity.
//
// If tilting the bell forward (without a real swing) arms the detector,
// LOWER this alpha so gravity is tracked faster; the cost is that it also
// absorbs more genuine swing acceleration, so don't overshoot. Use
// CALIBRATION_MODE 2 to compare a real swing against a slow tilt.
//
// With only an accelerometer there's no clean fix — one sensor can't separate
// rotation from translation. A 6-DOF IMU with a gyro (e.g. LSM6DS3) would let
// a complementary filter track orientation properly and remove this whole
// class of problem.
#define GRAVITY_LPF_ALPHA       0.997f

// Per-sample decay on the velocity integrator (~0.5s time constant at 400Hz).
// This is what keeps accelerometer bias from integrating into phantom velocity.
#define VELOCITY_DECAY          0.995f

// Speed (m/s, any direction) at which the detector starts watching a gesture.
// This is NOT what distinguishes a ring from a damp — that's decided at the
// stop, see RING_FORWARD_VELOCITY below. Keep it comfortably below both, so
// every real gesture gets watched from early enough to capture its peaks.
// Set low because a damp can be a genuinely gentle motion — well gentler than
// any ring — and a gesture that never arms can't be detected no matter how the
// stop thresholds are set.
// RAISE if incidental handling triggers things; LOWER if gestures are missed.
#define ARM_SPEED               0.30f

// Peak FORWARD velocity (m/s) a gesture must have reached, at the moment it
// stops, to count as a ring rather than a damp. This is the ring/damp
// discriminator. A damp travels back toward the body, so its peak forward
// velocity sits near zero or negative and it lands on the damp side by a wide
// margin — which is why this can stay generous.
//   LOWER -> more gestures ring (risk: a damp gets read as a ring)
//   RAISE -> ring needs a more committed swing (risk: real rings read as damps
//            and land silently, which is the failure this replaced)
#define RING_FORWARD_VELOCITY   0.70f

// Deceleration (m/s^2) along the direction of travel that counts as "the bell
// stopped". Separate values per gesture, because they are physically different
// events: ringing means arresting a committed swing, while damping can be as
// gentle as resting the casting against your shoulder.
//
// These CAN differ even though the gesture is classified at the stop — the
// classification input (peakForwardVelocity) is a running peak available at
// every sample, so which threshold applies is known before the stop happens,
// not after. An earlier version collapsed these into one on the mistaken
// reasoning that detection had to precede classification; it doesn't.
//
// RAISE either if incidental handling triggers that gesture; LOWER it if you
// have to make the motion unnaturally hard to trigger.
#define RING_STOP_DECEL_THRESHOLD  12.0f
#define DAMP_STOP_DECEL_THRESHOLD   6.0f

// Speed must fall back below this (m/s) for a gesture that never produced a
// definite stop to release and start over.
#define RELEASE_VELOCITY        0.25f

// Below this linear acceleration (m/s^2) the bell is considered at rest, and
// after REST_SAMPLES_REQUIRED consecutive samples the velocity integrator and
// gesture peaks are zeroed outright to kill any residual drift.
#define REST_ACCEL_THRESHOLD    0.60f
#define REST_SAMPLES_REQUIRED   40     // 100ms at 400Hz

// Minimum gap after any event before another can fire.
#define REFRACTORY_MS           200

// A damp is additionally suppressed for this long after a ring. The strike
// stop is followed by recoil/rebound, which is real motion ending in a real
// deceleration and would otherwise read as a damp — instantly killing the
// tone that was just started. Nobody legitimately damps a third of a second
// after ringing, so this costs nothing musically.
#define POST_RING_DAMP_LOCKOUT_MS 350

// Random-but-fixed UUIDs for the BLE service/characteristic. Must match the
// UUIDs the Android app scans/subscribes for (see android/.../RingEvent.kt).
// Generate your own at https://www.uuidgenerator.net/ if you want unique ones.
#define BLE_SERVICE_UUID      "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define BLE_CHAR_RING_UUID    "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
#define BLE_CHAR_BATTERY_UUID "6e400003-b5a3-f393-e0a9-e50e24dcca9e"
#define BLE_CHAR_TIME_UUID    "6e400004-b5a3-f393-e0a9-e50e24dcca9e"
#define BLE_CHAR_DAMP_UUID    "6e400005-b5a3-f393-e0a9-e50e24dcca9e"
#define BLE_DEVICE_NAME       "WirelessHandbell"

// Preferred connection parameters, requested as soon as a central connects.
// Units: interval in 1.25ms steps, timeout in 10ms steps. See NimBLEServer::updateConnParams.
#define CONN_INTERVAL_MIN     6     //  7.5ms
#define CONN_INTERVAL_MAX     12    // 15ms
#define CONN_LATENCY          0     // never skip a connection event
#define CONN_TIMEOUT          400   // 4000ms supervision timeout

// Battery telemetry — LOW PRIORITY. Sampled on a slow timer in loop(), never
// gating or slowing the ring-detection path above. See the header comment.
#define BATT_PIN                A13     // BATT_MONITOR: 200K/200K divider on the Feather V2
#define BATT_SAMPLE_INTERVAL_MS 5000    // how often we take a fresh reading + notify
#define BATT_TREND_SAMPLES      12      // 12 * 5s = 60s window used to judge charge/discharge trend
#define BATT_CAPACITY_MAH       500     // matches the 500mAh LiPo this was designed around
#define BATT_ASSUMED_DRAW_MA    60      // rough average draw for the time-remaining estimate —
                                         // measure your actual draw with a multimeter for a better number
#define BATT_ABSENT_VOLTAGE     2.0f    // below this, no LiPo is physically connected
#define BATT_VOLTAGE_CAL        1.0f    // fudge factor if your multimeter disagrees with the ADC reading
#define BATT_CHARGE_RISE_MV     15      // min rise over the trend window to call it "charging"
#define BATT_DISCHARGE_FALL_MV  5       // min fall over the trend window to call it "discharging"
// The two thresholds above are starting points — watch the raw millivolt
// readings Serial-printed below across a real charge/discharge cycle and
// retune them if the state flickers or lags.

// ---------------------------------------------------------------------------
// GLOBALS
// ---------------------------------------------------------------------------
Adafruit_LIS3DH lis = Adafruit_LIS3DH();

NimBLEServer* bleServer = nullptr;
NimBLECharacteristic* ringCharacteristic = nullptr;
NimBLECharacteristic* batteryCharacteristic = nullptr;
NimBLECharacteristic* timeCharacteristic = nullptr;
NimBLECharacteristic* dampCharacteristic = nullptr;
bool bleClientConnected = false;

uint32_t ringCounter = 0;

// --- Ring/damp detection state ----------------------------------------------
// ONE armed state for both gestures, deliberately. Which gesture a motion is
// gets decided at the STOP, from the peaks accumulated over the whole gesture
// — not at arming time. See "DECIDE AT THE STOP" in the header comment for
// why arming-time classification could not work.
enum RingState : uint8_t {
  RING_IDLE,        // at rest / drifting; watching for a gesture to start
  RING_ARMED,       // bell is moving with intent; waiting to see how it ends
  RING_SETTLING,    // just fired; refractory window
};

RingState ringState = RING_IDLE;
unsigned long settleUntilMs = 0;
unsigned long lastRingMs = 0;      // for POST_RING_DAMP_LOCKOUT_MS
unsigned long lastSampleUs = 0;

// Running gravity estimate (low-pass filtered raw acceleration).
bool gravityInitialized = false;
float gravityX = 0, gravityY = 0, gravityZ = 0;

// Leaky-integrated velocity as a full 3D vector. Ring detection only cares
// about its forward component, but a damp can come from any direction, so the
// whole vector has to be carried -- see SUSTAIN AND DAMP in the header.
float velX = 0, velY = 0, velZ = 0;

// Unit vector of travel, captured at the fastest point of the current gesture.
// Damp deceleration is measured along THIS rather than a fixed axis, which is
// what lets a damp register at 45° lateral, straight down, or anywhere else.
// Sampling it at peak speed (rather than continuously) keeps it stable: the
// direction of a slow velocity vector is mostly noise.
float travelDirX = 0, travelDirY = 0, travelDirZ = 0;

// Peaks accumulated over the current gesture. These are what the stop-time
// classification reads, so they must cover the WHOLE gesture (including the
// build-up before arming) and be reset only between gestures.
float peakLinearAccel = 0;        // for the ring payload's dynamic level
float peakSpeed = 0;              // peak |velocity|, any direction
float peakForwardVelocity = 0;    // peak SIGNED forward velocity — the ring/damp discriminator
int restSamples = 0;

// Clear everything gesture-scoped. Called between gestures, so each one is
// classified purely on its own peaks.
void resetGesturePeaks() {
  peakLinearAccel = 0;
  peakSpeed = 0;
  peakForwardVelocity = 0;
}

// Wire-format packet sent as the BLE notify payload.
// Keep this tiny and fixed-size — must match RingEvent.kt on the Android side.
typedef struct __attribute__((packed)) {
  uint32_t ringId;        // monotonically increasing ring counter
  uint16_t peakMilliG;    // peak acceleration magnitude in milli-g (for velocity-sensitive tone)
  uint32_t timestampMs;   // millis() at time of detection, for latency diagnostics
} RingEvent;

// Wire-format packet sent as the damp notify payload. Deliberately minimal —
// a damp is a pure "stop sounding" signal, not a new strike, so it doesn't
// need peak/dynamic info. The timestamp is kept for future latency
// diagnostics symmetry with RingEvent, even though nothing consumes it yet.
typedef struct __attribute__((packed)) {
  uint32_t timestampMs;
} DampEvent;

// Must match BatteryState in BatteryStatus.kt on the Android side.
enum BatteryState : uint8_t {
  BATT_STATE_UNKNOWN     = 0,  // not enough trend history yet (first ~60s after boot)
  BATT_STATE_DISCHARGING = 1,  // on battery, voltage flat or falling
  BATT_STATE_CHARGING    = 2,  // voltage rising — actively being charged over USB
  BATT_STATE_NO_BATTERY  = 3,  // no LiPo connected; running on USB power alone
};

// Wire-format packet sent as the battery notify payload — see BatteryStatus.kt.
typedef struct __attribute__((packed)) {
  uint8_t percent;                    // 0-100, best-effort estimate from the voltage curve
  uint8_t state;                      // BatteryState
  uint16_t milliVolts;                // raw (smoothed) battery voltage, for diagnostics
  uint16_t estimatedMinutesRemaining; // only meaningful when state == BATT_STATE_DISCHARGING
} BatteryStatus;

// --- Battery sampling state (all touched only by sampleBatteryIfDue) ---
unsigned long lastBattSampleMillis = 0;
float battVoltageEma = -1.0f;                    // -1 = not yet initialized
float battTrendHistory[BATT_TREND_SAMPLES] = {0};
int battTrendIndex = 0;
int battTrendCount = 0;

// ---------------------------------------------------------------------------
// BLE server callbacks
// ---------------------------------------------------------------------------
// NOTE: NimBLE-Arduino 2.x changed these callback signatures to include a
// NimBLEConnInfo& parameter (and onDisconnect adds a reason code). If you're
// on NimBLE-Arduino 1.x instead, drop the NimBLEConnInfo&/reason parameters.
class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* server, NimBLEConnInfo& connInfo) override {
    bleClientConnected = true;
    Serial.println("[BLE] client connected — requesting fast connection params");
    server->updateConnParams(connInfo.getConnHandle(),
                              CONN_INTERVAL_MIN, CONN_INTERVAL_MAX,
                              CONN_LATENCY, CONN_TIMEOUT);
  }
  void onDisconnect(NimBLEServer* server, NimBLEConnInfo& connInfo, int reason) override {
    bleClientConnected = false;
    Serial.println("[BLE] client disconnected, restarting advertising");
    NimBLEDevice::startAdvertising();
  }
};

// Serves the current millis() on every read, for the Android app's
// clock-sync handshake (its clock and ours are otherwise unrelated — this
// is what lets RingEvent.timestampMs be compared against the phone's own
// clock to compute end-to-end ring-to-tone latency).
class TimeCharacteristicCallbacks : public NimBLECharacteristicCallbacks {
  void onRead(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override {
    uint32_t now = millis();
    pCharacteristic->setValue((uint8_t*)&now, sizeof(now));
  }
};

// ---------------------------------------------------------------------------
// BATTERY TELEMETRY — low priority, see header comment
// ---------------------------------------------------------------------------

// Rough 1S LiPo discharge curve, piecewise-linear interpolated. Voltage-based
// state-of-charge is inherently approximate — treat this as a rough gauge,
// not a precise one.
uint8_t voltageToPercent(float v) {
  static const float curveV[]   = {3.00, 3.45, 3.68, 3.74, 3.77, 3.79, 3.82, 3.87, 3.92, 3.98, 4.06, 4.20};
  static const float curvePct[] = {   0,    5,   10,   20,   30,   40,   50,   60,   70,   80,   90,  100};
  const int n = sizeof(curveV) / sizeof(curveV[0]);

  if (v <= curveV[0]) return 0;
  if (v >= curveV[n - 1]) return 100;
  for (int i = 1; i < n; i++) {
    if (v <= curveV[i]) {
      float span = curveV[i] - curveV[i - 1];
      float frac = (v - curveV[i - 1]) / span;
      return (uint8_t)(curvePct[i - 1] + frac * (curvePct[i] - curvePct[i - 1]));
    }
  }
  return 100;
}

// Called every loop() iteration but only does real work once every
// BATT_SAMPLE_INTERVAL_MS — a single analogRead() plus some cheap arithmetic,
// nowhere near the accelerometer polling rate, so it never meaningfully
// competes with ring detection for CPU time.
void sampleBatteryIfDue(unsigned long now) {
  if (now - lastBattSampleMillis < BATT_SAMPLE_INTERVAL_MS) return;
  lastBattSampleMillis = now;

  int raw = analogRead(BATT_PIN);
  // 12-bit ADC, 3.3V reference, x2 for the 200K/200K divider on BAT.
  float voltage = (raw / 4095.0f) * 3.3f * 2.0f * BATT_VOLTAGE_CAL;

  // Light exponential smoothing — the ESP32's ADC is fairly noisy on its own.
  battVoltageEma = (battVoltageEma < 0) ? voltage : (battVoltageEma * 0.7f + voltage * 0.3f);

  battTrendHistory[battTrendIndex] = battVoltageEma;
  battTrendIndex = (battTrendIndex + 1) % BATT_TREND_SAMPLES;
  if (battTrendCount < BATT_TREND_SAMPLES) battTrendCount++;

  BatteryStatus bs;
  bs.milliVolts = (uint16_t)(battVoltageEma * 1000.0f);

  if (battVoltageEma < BATT_ABSENT_VOLTAGE) {
    bs.percent = 0;
    bs.state = BATT_STATE_NO_BATTERY;
    bs.estimatedMinutesRemaining = 0;
  } else {
    bs.percent = voltageToPercent(battVoltageEma);

    if (battTrendCount < BATT_TREND_SAMPLES) {
      bs.state = BATT_STATE_UNKNOWN;  // haven't seen a full 60s window yet
    } else {
      // battTrendIndex currently points at the oldest sample (next one to be
      // overwritten), since the buffer just wrapped past it above.
      float oldest = battTrendHistory[battTrendIndex];
      float deltaMv = (battVoltageEma - oldest) * 1000.0f;
      if (deltaMv >= BATT_CHARGE_RISE_MV) {
        bs.state = BATT_STATE_CHARGING;
      } else if (deltaMv <= -BATT_DISCHARGE_FALL_MV) {
        bs.state = BATT_STATE_DISCHARGING;
      } else {
        // Ambiguous flat zone (e.g. resting at/near full). Default to
        // "discharging" rather than invent a "full" state we can't actually
        // distinguish from "resting, unplugged" — see header comment.
        bs.state = BATT_STATE_DISCHARGING;
      }
    }

    bs.estimatedMinutesRemaining = (bs.state == BATT_STATE_DISCHARGING)
        ? (uint16_t)((bs.percent / 100.0f) * BATT_CAPACITY_MAH / BATT_ASSUMED_DRAW_MA * 60.0f)
        : 0;
  }

  Serial.printf("[BATT] %umV  %u%%  state=%u  ~%umin remaining\n",
                bs.milliVolts, bs.percent, bs.state, bs.estimatedMinutesRemaining);

  if (bleClientConnected && batteryCharacteristic) {
    batteryCharacteristic->setValue((uint8_t*)&bs, sizeof(bs));
    batteryCharacteristic->notify();
  }
}

// ---------------------------------------------------------------------------
// SETUP
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\nWireless Handbell transmitter starting (BLE-only, v0.2)...");

  // --- Accelerometer ---
  if (!lis.begin(0x18)) {          // Adafruit LIS3DH default addr; try 0x19 if this fails
    Serial.println("Could not find LIS3DH — check wiring/address!");
    while (1) delay(1000);
  }
  lis.setRange(LIS3DH_RANGE_4_G);
  // 12-bit high resolution at 400Hz. The detector integrates acceleration into
  // velocity, so resolution matters more here than raw sample rate — see the
  // LATENCY TUNING note in the header.
  lis.setPerformanceMode(LIS3DH_MODE_HIGH_RESOLUTION);
  lis.setDataRate(LIS3DH_DATARATE_400_HZ);
  Serial.println("LIS3DH ready (400Hz, 12-bit).");
#if CALIBRATION_MODE == 1
  Serial.println("\n*** CALIBRATION MODE 1 (axis ID) — rings suppressed. ***");
  Serial.println("Hold still: the dominant g[] axis should be along the rod,");
  Serial.println("not Z. Then swing forward and confirm vFwd goes POSITIVE.");
  Serial.println("See the notes above CALIBRATION_MODE in this sketch.\n");
#elif CALIBRATION_MODE == 2
  Serial.println("\n*** CALIBRATION MODE 2 (swing trace) — rings/damps still fire. ***");
  Serial.println("Traces while moving. Ring and damp normally a few times, then try");
  Serial.println("things that should NOT trigger either (pick up, tap handle, slow");
  Serial.println("tilt). Compare peakFwd across rings vs damps -- they should");
  Serial.println("separate cleanly around RING_FORWARD_VELOCITY -- and decel at");
  Serial.println("each stop against RING_/DAMP_STOP_DECEL_THRESHOLD.\n");
#endif

  // --- BLE (NimBLE) ---
  NimBLEDevice::init(BLE_DEVICE_NAME);
  NimBLEDevice::setPower(9);  // +9dBm — reduce odds of a retry/retransmit adding latency

  bleServer = NimBLEDevice::createServer();
  bleServer->setCallbacks(new ServerCallbacks());

  NimBLEService* service = bleServer->createService(BLE_SERVICE_UUID);
  ringCharacteristic = service->createCharacteristic(
      BLE_CHAR_RING_UUID,
      NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ);
  batteryCharacteristic = service->createCharacteristic(
      BLE_CHAR_BATTERY_UUID,
      NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ);
  timeCharacteristic = service->createCharacteristic(
      BLE_CHAR_TIME_UUID,
      NIMBLE_PROPERTY::READ);
  timeCharacteristic->setCallbacks(new TimeCharacteristicCallbacks());
  dampCharacteristic = service->createCharacteristic(
      BLE_CHAR_DAMP_UUID,
      NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ);
  service->start();

  NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();

  // The primary advertising packet is capped at 31 bytes. A 128-bit
  // service UUID (16 bytes) plus flags leaves too little room for the
  // full name "WirelessHandbell" alongside it — NimBLE silently drops
  // whatever doesn't fit, which is why the name wasn't showing up in
  // scans even though the device itself was visible. Fix: keep the name
  // by itself in the primary packet, and put the UUID in a separate scan
  // response packet instead.
  advertising->setName(BLE_DEVICE_NAME);

  NimBLEAdvertisementData scanResponseData;
  scanResponseData.addServiceUUID(BLE_SERVICE_UUID);
  advertising->setScanResponseData(scanResponseData);

  advertising->start();

  Serial.println("BLE advertising as \"WirelessHandbell\".");
  Serial.printf("BLE address: %s\n", NimBLEDevice::getAddress().toString().c_str());
  Serial.println("Setup complete. Swing the bell!");
}

// ---------------------------------------------------------------------------
// RING EMISSION
// ---------------------------------------------------------------------------
void emitRing(unsigned long nowMs) {
  ringCounter++;

  RingEvent evt;
  evt.ringId = ringCounter;
  // Peak LINEAR acceleration during the swing, in milli-g. Note this no longer
  // includes gravity the way v0.2's value did, so the tone-strength buckets in
  // the Android app's RingPlayer.BUCKETS may want retuning.
  float peakMilliG = (peakLinearAccel / 9.80665f) * 1000.0f;
  evt.peakMilliG = (uint16_t)constrain(peakMilliG, 0.0f, 65535.0f);
  evt.timestampMs = nowMs;

  Serial.printf("RING #%lu  peak=%.2fg  peakFwd=%.2fm/s  peakSpd=%.2fm/s\n",
                (unsigned long)evt.ringId, evt.peakMilliG / 1000.0f,
                peakForwardVelocity, peakSpeed);

  if (bleClientConnected && ringCharacteristic) {
    ringCharacteristic->setValue((uint8_t*)&evt, sizeof(evt));
    ringCharacteristic->notify();
  }
}

// ---------------------------------------------------------------------------
// DAMP EMISSION — see SUSTAIN AND DAMP in the header comment
// ---------------------------------------------------------------------------
void emitDamp(unsigned long nowMs, float decelAlongTravel) {
  DampEvent evt;
  evt.timestampMs = nowMs;

  // peakFwd is printed because it's what ruled this a damp rather than a ring:
  // if it's sitting just under RING_FORWARD_VELOCITY on gestures you meant as
  // rings, that threshold is set too high.
  Serial.printf("DAMP  peakFwd=%.2fm/s  peakSpd=%.2fm/s  decel=%.1fm/s^2  dir=[%.2f %.2f %.2f]\n",
                peakForwardVelocity, peakSpeed, decelAlongTravel,
                travelDirX, travelDirY, travelDirZ);

  if (bleClientConnected && dampCharacteristic) {
    dampCharacteristic->setValue((uint8_t*)&evt, sizeof(evt));
    dampCharacteristic->notify();
  }
}

// ---------------------------------------------------------------------------
// LOOP — sample accelerometer, run the swing/stop detector, transmit
// ---------------------------------------------------------------------------
void loop() {
  unsigned long nowMs = millis();
  unsigned long nowUs = micros();

  // Pace sampling to the sensor's ODR; polling faster just re-reads the same
  // sample. Battery telemetry still gets a chance to run on skipped iterations.
  if ((unsigned long)(nowUs - lastSampleUs) < SAMPLE_INTERVAL_US) {
    sampleBatteryIfDue(nowMs);
    return;
  }
  float dt = (nowUs - lastSampleUs) / 1000000.0f;
  lastSampleUs = nowUs;
  // Guard the first iteration (and any stall) from producing a huge dt that
  // would slam the integrator.
  if (dt > 0.05f) dt = 0.05f;

  sensors_event_t event;
  lis.getEvent(&event);
  float ax = event.acceleration.x;   // m/s^2, gravity included
  float ay = event.acceleration.y;
  float az = event.acceleration.z;

  // --- Separate gravity from linear acceleration ---------------------------
  if (!gravityInitialized) {
    gravityX = ax; gravityY = ay; gravityZ = az;
    gravityInitialized = true;
  } else {
    gravityX = GRAVITY_LPF_ALPHA * gravityX + (1.0f - GRAVITY_LPF_ALPHA) * ax;
    gravityY = GRAVITY_LPF_ALPHA * gravityY + (1.0f - GRAVITY_LPF_ALPHA) * ay;
    gravityZ = GRAVITY_LPF_ALPHA * gravityZ + (1.0f - GRAVITY_LPF_ALPHA) * az;
  }
  float linX = ax - gravityX;
  float linY = ay - gravityY;
  float linZ = az - gravityZ;
  float linMag = sqrtf(linX * linX + linY * linY + linZ * linZ);

  // (No forward-axis acceleration term any more: the stop is now detected
  // along the direction of travel — decelAlongTravel below — which reduces to
  // the same thing for a forward swing but also works for an off-axis damp.)

  // --- Integrate to a 3D velocity vector (leaky, so bias can't accumulate) --
  velX = velX * VELOCITY_DECAY + linX * dt;
  velY = velY * VELOCITY_DECAY + linY * dt;
  velZ = velZ * VELOCITY_DECAY + linZ * dt;

  // When the bell is genuinely still, zero the integrator outright — and the
  // gesture peaks with it, so a stale peak from an earlier motion can't leak
  // into how the next gesture gets classified.
  if (linMag < REST_ACCEL_THRESHOLD) {
    if (restSamples < REST_SAMPLES_REQUIRED) {
      restSamples++;
    } else {
      velX = velY = velZ = 0.0f;
      resetGesturePeaks();
    }
  } else {
    restSamples = 0;
  }

  // Forward component is the ring/damp discriminator; the full magnitude is
  // what arms a gesture and what the damp's stop is measured against.
  float forwardVelocity = FORWARD_SIGN * (FORWARD_AXIS == AXIS_X ? velX
                                        : FORWARD_AXIS == AXIS_Y ? velY
                                                                 : velZ);
  float speed = sqrtf(velX * velX + velY * velY + velZ * velZ);

  // Capture the direction of travel at the fastest point of the gesture, and
  // measure deceleration along it. This is what makes damp orientation-blind.
  if (speed > peakSpeed) {
    peakSpeed = speed;
    if (speed > 1e-6f) {
      travelDirX = velX / speed;
      travelDirY = velY / speed;
      travelDirZ = velZ / speed;
    }
  }
  float decelAlongTravel = -(linX * travelDirX + linY * travelDirY + linZ * travelDirZ);

  if (linMag > peakLinearAccel) peakLinearAccel = linMag;
  if (forwardVelocity > peakForwardVelocity) peakForwardVelocity = forwardVelocity;

#if CALIBRATION_MODE == 1
  static unsigned long lastCalPrintMs = 0;
  if (nowMs - lastCalPrintMs >= 50) {
    lastCalPrintMs = nowMs;
    Serial.printf("g=[%6.2f %6.2f %6.2f]  lin=[%6.2f %6.2f %6.2f]  vFwd=%6.2f\n",
                  gravityX, gravityY, gravityZ, linX, linY, linZ, forwardVelocity);
  }
#else
#if CALIBRATION_MODE == 2
  // Trace only while the bell is actually moving, so the log isn't buried in
  // idle noise. 50Hz is enough to see the shape of a swing without flooding
  // the serial link (which would itself add latency).
  {
    static unsigned long lastTracePrintMs = 0;
    if (linMag >= REST_ACCEL_THRESHOLD && nowMs - lastTracePrintMs >= 20) {
      lastTracePrintMs = nowMs;
      // peakFwd is the discriminator: at the stop, >= RING_FORWARD_VELOCITY
      // is a ring, anything less is a damp. Watch it across both gestures to
      // confirm they separate cleanly.
      Serial.printf("vFwd=%6.2f  speed=%5.2f  decel=%6.1f  peakFwd=%5.2f  peakSpd=%5.2f  state=%s\n",
                    forwardVelocity, speed, decelAlongTravel,
                    peakForwardVelocity, peakSpeed,
                    ringState == RING_IDLE    ? "idle"
                    : ringState == RING_ARMED ? "ARMED"
                                              : "settling");
    }
  }
#endif
  // --- Gesture state machine -----------------------------------------------
  // Arm on motion; classify at the stop. See "DECIDE AT THE STOP" in the
  // header for why this must not be split into per-gesture armed states.
  switch (ringState) {
    case RING_IDLE:
      // Direction-agnostic: any real motion arms. A tap on the handle spikes
      // acceleration but nets ~zero velocity, so it never gets here.
      if (speed >= ARM_SPEED) {
        ringState = RING_ARMED;
      }
      break;

    case RING_ARMED: {
      // What this gesture would be if it stopped right now. Depends only on a
      // running peak, so it's known at every sample — which is what lets the
      // two gestures use different stop thresholds.
      bool wouldRing = (peakForwardVelocity >= RING_FORWARD_VELOCITY);
      float stopThreshold = wouldRing ? RING_STOP_DECEL_THRESHOLD
                                      : DAMP_STOP_DECEL_THRESHOLD;

      if (decelAlongTravel >= stopThreshold) {
        if (wouldRing) {
          // Travelled forward with real commitment: this is a strike.
          emitRing(nowMs);
          lastRingMs = nowMs;
        } else if (nowMs - lastRingMs >= POST_RING_DAMP_LOCKOUT_MS) {
          // Moved and stopped, but not a forward swing: a damp.
          emitDamp(nowMs, decelAlongTravel);
        } else {
          // Within the post-ring lockout. This is almost certainly the
          // strike's own recoil, not a real damp — swallow it rather than
          // killing the tone that was just started.
          Serial.println("(recoil suppressed)");
        }
        ringState = RING_SETTLING;
        settleUntilMs = nowMs + REFRACTORY_MS;
      } else if (speed < RELEASE_VELOCITY) {
        // Motion petered out without a definite stop — nothing to report.
        ringState = RING_IDLE;
        resetGesturePeaks();
      }
      break;
    }

    case RING_SETTLING:
      // Wait out the refractory window and for the deceleration spike itself
      // to pass. Deliberately does NOT wait for the bell to come to rest:
      // ringing and then damping is one continuous motion, and requiring a
      // full stop in between left the detector sitting here right through the
      // damp. See "DON'T REQUIRE THE BELL TO STOP BETWEEN GESTURES".
      // Uses the lower of the two thresholds, so the spike has decayed past
      // the point where either gesture could immediately re-fire on it.
      if (nowMs >= settleUntilMs && decelAlongTravel < DAMP_STOP_DECEL_THRESHOLD) {
        ringState = RING_IDLE;
        resetGesturePeaks();
      }
      break;
  }
#endif

  // Low priority — runs after ring detection, and is a no-op almost every
  // iteration (see the comment on the function itself).
  sampleBatteryIfDue(nowMs);
}
