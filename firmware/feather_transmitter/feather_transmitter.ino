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
#define BLE_DEVICE_NAME       "WirelessHandbell"

// Preferred connection parameters, requested as soon as a central connects.
// Units: interval in 1.25ms steps, timeout in 10ms steps. See NimBLEServer::updateConnParams.
#define CONN_INTERVAL_MIN     6     //  7.5ms
#define CONN_INTERVAL_MAX     12    // 15ms
#define CONN_LATENCY          0     // never skip a connection event
#define CONN_TIMEOUT          400   // 4000ms supervision timeout

// ---------------------------------------------------------------------------
// GLOBALS
// ---------------------------------------------------------------------------
Adafruit_LIS3DH lis = Adafruit_LIS3DH();

NimBLEServer* bleServer = nullptr;
NimBLECharacteristic* ringCharacteristic = nullptr;
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
}
