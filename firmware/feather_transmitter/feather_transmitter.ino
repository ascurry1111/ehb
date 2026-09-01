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
      3. The detector arms only once that velocity exceeds SWING_ARM_VELOCITY
         — i.e. the bell is genuinely travelling forward, not just jostled.
      4. While armed, a sharp deceleration (STOP_DECEL_THRESHOLD) counts as
         the bell being stopped, and that is what emits the ring.

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

  SUSTAIN AND MUTE — v0.4
    A real handbell keeps ringing after the strike until it naturally damps
    out, or until the ringer brings it to their body to stop it (touching the
    casting kills the vibration). The Android app now plays a multi-second
    decaying tone per ring rather than a short fixed blip, so this firmware
    needs to tell it when to cut that tone off early.

    Mute is detected as the mirror image of ring detection: a BACKWARD swing
    (negative forward velocity — the same direction the clapper springs
    forbid for ringing) followed by a sudden stop, the way bringing the bell
    to your chest/shoulder actually looks to the accelerometer. It's a
    separate BLE characteristic (BLE_CHAR_MUTE_UUID) rather than a field on
    RingEvent, since it's a genuinely different signal — "stop whatever is
    currently sounding," not "a new strike happened."

    The two gestures share one state machine (RING_IDLE / RING_ARMED_FORWARD
    / RING_ARMED_BACKWARD / RING_SETTLING) since forwardVelocity can't be
    both positive and negative at once — arming one direction is naturally
    exclusive with the other.

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
//   2 = swing trace (rings/mutes still fire) — streams the forward
//       acceleration and velocity profile whenever the bell is moving, so the
//       thresholds below can be set from real numbers. Ring a few times
//       normally, mute a few times (swing back to the body and stop), then
//       deliberately do the things that should NOT trigger either (pick it
//       up, tap the handle, tilt it slowly in each direction) and compare
//       the peakV/state trace across all of them.
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

// Forward speed (m/s) the bell must reach before a stop counts as a ring.
// RAISE THIS if gentle handling still rings it; LOWER it if genuine swings
// are being missed.
#define SWING_ARM_VELOCITY      0.70f

// Backward speed (m/s) the bell must reach before a stop counts as a mute —
// i.e. bringing the bell toward the body with real intent, not just easing
// off a swing. Same tuning knobs apply as SWING_ARM_VELOCITY, in reverse.
#define MUTE_ARM_VELOCITY       0.70f

// Velocity must fall back below this (m/s), in whichever direction was
// armed, before the detector releases and can re-arm.
#define RELEASE_VELOCITY        0.25f

// Deceleration (m/s^2, opposing the swing) that counts as "the bell stopped".
// ~15 m/s^2 is about 1.5g. RAISE THIS if soft stops ring; LOWER it if you
// have to stop the bell unnaturally hard to get a ring.
#define STOP_DECEL_THRESHOLD    15.0f

// Deceleration (m/s^2, opposing the backward motion) that counts as "the
// bell was pressed to a stop" for muting. Defaults to the same magnitude as
// STOP_DECEL_THRESHOLD as a starting point — contacting the body is a
// similarly abrupt motion — but tune independently with CALIBRATION_MODE 2
// if it turns out to feel different (e.g. a soft chest vs. a firm shoulder).
#define MUTE_STOP_DECEL_THRESHOLD 15.0f

// Below this linear acceleration (m/s^2) the bell is considered at rest, and
// after REST_SAMPLES_REQUIRED consecutive samples the velocity integrator is
// zeroed outright to kill any residual drift.
#define REST_ACCEL_THRESHOLD    0.60f
#define REST_SAMPLES_REQUIRED   40     // 100ms at 400Hz

#define REFRACTORY_MS           250    // minimum gap after a ring/mute before another can fire

// Random-but-fixed UUIDs for the BLE service/characteristic. Must match the
// UUIDs the Android app scans/subscribes for (see android/.../RingEvent.kt).
// Generate your own at https://www.uuidgenerator.net/ if you want unique ones.
#define BLE_SERVICE_UUID      "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define BLE_CHAR_RING_UUID    "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
#define BLE_CHAR_BATTERY_UUID "6e400003-b5a3-f393-e0a9-e50e24dcca9e"
#define BLE_CHAR_TIME_UUID    "6e400004-b5a3-f393-e0a9-e50e24dcca9e"
#define BLE_CHAR_MUTE_UUID    "6e400005-b5a3-f393-e0a9-e50e24dcca9e"
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
NimBLECharacteristic* muteCharacteristic = nullptr;
bool bleClientConnected = false;

uint32_t ringCounter = 0;

// --- Ring/mute detection state ----------------------------------------------
// One state machine for both gestures: forwardVelocity can't be positive and
// negative at once, so arming a swing in one direction is naturally exclusive
// with the other. See the SUSTAIN AND MUTE header comment.
enum RingState : uint8_t {
  RING_IDLE,             // watching for either direction to build up
  RING_ARMED_FORWARD,    // armed: moving forward fast enough to ring on a stop
  RING_ARMED_BACKWARD,   // armed: moving backward fast enough to mute on a stop
  RING_SETTLING,         // just fired (ring or mute); waiting for the swing to settle
};

RingState ringState = RING_IDLE;
unsigned long settleUntilMs = 0;
unsigned long lastSampleUs = 0;

// Running gravity estimate (low-pass filtered raw acceleration).
bool gravityInitialized = false;
float gravityX = 0, gravityY = 0, gravityZ = 0;

// Leaky-integrated forward velocity, and per-swing peaks for the ring payload.
// peakSwingVelocity tracks SPEED (magnitude), not signed velocity -- it needs
// to mean something for both a forward swing (positive) and a backward mute
// swing (negative).
float forwardVelocity = 0;
float peakLinearAccel = 0;
float peakSwingVelocity = 0;
int restSamples = 0;

// Wire-format packet sent as the BLE notify payload.
// Keep this tiny and fixed-size — must match RingEvent.kt on the Android side.
typedef struct __attribute__((packed)) {
  uint32_t ringId;        // monotonically increasing ring counter
  uint16_t peakMilliG;    // peak acceleration magnitude in milli-g (for velocity-sensitive tone)
  uint32_t timestampMs;   // millis() at time of detection, for latency diagnostics
} RingEvent;

// Wire-format packet sent as the mute notify payload. Deliberately minimal —
// a mute is a pure "stop sounding" signal, not a new strike, so it doesn't
// need peak/dynamic info. The timestamp is kept for future latency
// diagnostics symmetry with RingEvent, even though nothing consumes it yet.
typedef struct __attribute__((packed)) {
  uint32_t timestampMs;
} MuteEvent;

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
  Serial.println("\n*** CALIBRATION MODE 2 (swing trace) — rings/mutes still fire. ***");
  Serial.println("Traces while moving. Ring and mute normally a few times, then try");
  Serial.println("things that should NOT trigger either (pick up, tap handle, slow");
  Serial.println("tilt either way) and compare peakV against SWING_ARM_VELOCITY /");
  Serial.println("MUTE_ARM_VELOCITY.\n");
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
  muteCharacteristic = service->createCharacteristic(
      BLE_CHAR_MUTE_UUID,
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

  Serial.printf("RING #%lu  peak=%.2fg  swing=%.2fm/s\n",
                (unsigned long)evt.ringId, evt.peakMilliG / 1000.0f, peakSwingVelocity);

  if (bleClientConnected && ringCharacteristic) {
    ringCharacteristic->setValue((uint8_t*)&evt, sizeof(evt));
    ringCharacteristic->notify();
  }
}

// ---------------------------------------------------------------------------
// MUTE EMISSION — see SUSTAIN AND MUTE in the header comment
// ---------------------------------------------------------------------------
void emitMute(unsigned long nowMs) {
  MuteEvent evt;
  evt.timestampMs = nowMs;

  Serial.printf("MUTE  swing=%.2fm/s\n", peakSwingVelocity);

  if (bleClientConnected && muteCharacteristic) {
    muteCharacteristic->setValue((uint8_t*)&evt, sizeof(evt));
    muteCharacteristic->notify();
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

  float aForward = FORWARD_SIGN * (FORWARD_AXIS == AXIS_X ? linX
                                 : FORWARD_AXIS == AXIS_Y ? linY
                                                          : linZ);

  // --- Integrate to forward velocity (leaky, so bias can't accumulate) -----
  forwardVelocity = forwardVelocity * VELOCITY_DECAY + aForward * dt;

  // When the bell is genuinely still, zero the integrator outright.
  if (linMag < REST_ACCEL_THRESHOLD) {
    if (restSamples < REST_SAMPLES_REQUIRED) {
      restSamples++;
    } else {
      forwardVelocity = 0.0f;
    }
  } else {
    restSamples = 0;
  }

  if (linMag > peakLinearAccel) peakLinearAccel = linMag;
  if (fabsf(forwardVelocity) > peakSwingVelocity) peakSwingVelocity = fabsf(forwardVelocity);

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
      Serial.printf("aFwd=%7.2f  vFwd=%6.2f  peakV=%6.2f  state=%s\n",
                    aForward, forwardVelocity, peakSwingVelocity,
                    ringState == RING_IDLE           ? "idle"
                    : ringState == RING_ARMED_FORWARD  ? "ARMED-fwd"
                    : ringState == RING_ARMED_BACKWARD ? "ARMED-bwd"
                                                       : "settling");
    }
  }
#endif
  // --- Swing / stop state machine ------------------------------------------
  switch (ringState) {
    case RING_IDLE:
      // Arm only once the bell is genuinely travelling with real intent, in
      // either direction. A tap on the handle spikes acceleration but nets
      // ~zero velocity, so it stops here regardless of direction.
      if (forwardVelocity >= SWING_ARM_VELOCITY) {
        ringState = RING_ARMED_FORWARD;
      } else if (forwardVelocity <= -MUTE_ARM_VELOCITY) {
        ringState = RING_ARMED_BACKWARD;
      }
      break;

    case RING_ARMED_FORWARD:
      if (aForward <= -STOP_DECEL_THRESHOLD) {
        // Sharp deceleration opposing the swing: the bell has been stopped,
        // which is the moment a real clapper would strike.
        emitRing(nowMs);
        ringState = RING_SETTLING;
        settleUntilMs = nowMs + REFRACTORY_MS;
      } else if (forwardVelocity < RELEASE_VELOCITY) {
        // Swing petered out without a definite stop — no ring.
        ringState = RING_IDLE;
        peakLinearAccel = 0;
        peakSwingVelocity = 0;
      }
      break;

    case RING_ARMED_BACKWARD:
      if (aForward >= MUTE_STOP_DECEL_THRESHOLD) {
        // Sharp deceleration opposing the backward motion: the bell has been
        // pressed to a stop, same as touching the casting on a real handbell.
        emitMute(nowMs);
        ringState = RING_SETTLING;
        settleUntilMs = nowMs + REFRACTORY_MS;
      } else if (forwardVelocity > -RELEASE_VELOCITY) {
        // Backward motion petered out without a definite stop — no mute.
        ringState = RING_IDLE;
        peakLinearAccel = 0;
        peakSwingVelocity = 0;
      }
      break;

    case RING_SETTLING:
      // Require BOTH the settle window to expire and the swing to actually
      // die down, so one vigorous motion can't produce a burst of events.
      if (nowMs >= settleUntilMs && fabsf(forwardVelocity) < RELEASE_VELOCITY) {
        ringState = RING_IDLE;
        peakLinearAccel = 0;
        peakSwingVelocity = 0;
      }
      break;
  }
#endif

  // Low priority — runs after ring detection, and is a no-op almost every
  // iteration (see the comment on the function itself).
  sampleBatteryIfDue(nowMs);
}
