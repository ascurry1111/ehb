/*
  ============================================================================
  Wireless Handbell — Feather ESP32 V2 Transmitter (BLE-only, low-latency)
  ============================================================================
  Reads an Adafruit LIS3DH accelerometer, detects a "ring" (a sudden
  acceleration spike caused by swinging/striking the bell), and transmits
  the event over BLE (GATT notify) to a phone running the Handbell Receiver
  Android app.

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
    - LIS3DH data rate raised to 1.6kHz (low-power mode, 8-bit resolution).
      We only need to catch a threshold crossing quickly, not calibrate
      precisely, so the resolution tradeoff is worth the ~4x faster sampling.
    - The accelerometer is now polled every loop() iteration with no
      artificial delay — NimBLE's host/controller stack runs on the other
      core, so a tight polling loop here doesn't steal its CPU time.
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
#define RING_THRESHOLD_G     2.2f   // magnitude (in g) above which we call it a "ring"
#define REFRACTORY_MS        150    // minimum gap between two ring events (debounce)

// Random-but-fixed UUIDs for the BLE service/characteristic. Must match the
// UUIDs the Android app scans/subscribes for (see android/.../RingEvent.kt).
// Generate your own at https://www.uuidgenerator.net/ if you want unique ones.
#define BLE_SERVICE_UUID      "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define BLE_CHAR_RING_UUID    "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
#define BLE_CHAR_BATTERY_UUID "6e400003-b5a3-f393-e0a9-e50e24dcca9e"
#define BLE_CHAR_TIME_UUID    "6e400004-b5a3-f393-e0a9-e50e24dcca9e"
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
bool bleClientConnected = false;

uint32_t ringCounter = 0;
uint32_t lastRingMillis = 0;

// Wire-format packet sent as the BLE notify payload.
// Keep this tiny and fixed-size — must match RingEvent.kt on the Android side.
typedef struct __attribute__((packed)) {
  uint32_t ringId;        // monotonically increasing ring counter
  uint16_t peakMilliG;    // peak acceleration magnitude in milli-g (for velocity-sensitive tone)
  uint32_t timestampMs;   // millis() at time of detection, for latency diagnostics
} RingEvent;

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
  lis.setDataRate(LIS3DH_DATARATE_LOWPOWER_1K6HZ);  // fastest ODR available, for lowest detection latency
  Serial.println("LIS3DH ready.");

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
// LOOP — poll accelerometer as fast as possible, detect ring, transmit
// ---------------------------------------------------------------------------
void loop() {
  unsigned long now = millis();

  sensors_event_t event;
  lis.getEvent(&event);

  // Magnitude of the acceleration vector, in g. At rest this reads ~1.0g (gravity).
  float ax = event.acceleration.x / 9.80665f;
  float ay = event.acceleration.y / 9.80665f;
  float az = event.acceleration.z / 9.80665f;
  float magnitude = sqrtf(ax * ax + ay * ay + az * az);

  static float peakSinceLastRing = 0;
  if (magnitude > peakSinceLastRing) peakSinceLastRing = magnitude;

  bool pastThreshold = magnitude > RING_THRESHOLD_G;
  bool pastRefractory = (now - lastRingMillis) > REFRACTORY_MS;

  if (pastThreshold && pastRefractory) {
    lastRingMillis = now;
    ringCounter++;

    RingEvent evt;
    evt.ringId = ringCounter;
    evt.peakMilliG = (uint16_t)(peakSinceLastRing * 1000.0f);
    evt.timestampMs = now;
    peakSinceLastRing = 0;

    Serial.printf("RING #%lu  peak=%.2fg\n", (unsigned long)evt.ringId, evt.peakMilliG / 1000.0f);

    if (bleClientConnected && ringCharacteristic) {
      ringCharacteristic->setValue((uint8_t*)&evt, sizeof(evt));
      ringCharacteristic->notify();
    }
  }

  // Low priority — runs after ring detection, and is a no-op almost every
  // iteration (see the comment on the function itself).
  sampleBatteryIfDue(now);
}
