/*
  ============================================================================
  Wireless Handbell — Feather ESP32 V2 Transmitter
  ============================================================================
  Reads an Adafruit LIS3DH accelerometer, detects a "ring" (a sudden
  acceleration spike caused by swinging/striking the bell), and transmits
  the event over BOTH BLE (GATT notify) and ESP-NOW at the same time.

  HARDWARE
    - Adafruit ESP32 Feather V2
    - Adafruit LIS3DH breakout, wired over I2C (STEMMA QT or SDA/SCL + 3V + GND)
    - 500mAh LiPo on the JST connector

  LIBRARIES (Arduino Library Manager)
    - Adafruit LIS3DH
    - Adafruit Unified Sensor
    - NimBLE-Arduino (by h2zero)   <-- NOT the stock ESP32 BLE library

  SETUP STEPS
    1. Flash devkit_receiver.ino to the ESP32-DEVKITC-V4 first.
    2. Open its Serial Monitor — it prints its own MAC address on boot.
    3. Paste that MAC address into RECEIVER_MAC below.
    4. Flash this sketch to the Feather.
  ============================================================================
*/

#include <Wire.h>
#include <Adafruit_LIS3DH.h>
#include <Adafruit_Sensor.h>
#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_coexist.h>
#include <NimBLEDevice.h>

// ---------------------------------------------------------------------------
// CONFIG — tune these for your bell / mounting
// ---------------------------------------------------------------------------
#define RING_THRESHOLD_G     2.2f   // magnitude (in g) above which we call it a "ring"
#define REFRACTORY_MS        150    // minimum gap between two ring events (debounce)
#define SAMPLE_INTERVAL_MS   5      // ~200 Hz polling of the accelerometer

// TODO: fill this in with the MAC address printed by devkit_receiver.ino
static uint8_t RECEIVER_MAC[6] = { 0x8C, 0x94, 0xDF, 0x57, 0x46, 0x34 };

// Random-but-fixed UUIDs for the BLE service/characteristic.
// Generate your own at https://www.uuidgenerator.net/ if you want unique ones.
#define BLE_SERVICE_UUID      "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define BLE_CHAR_RING_UUID    "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
#define BLE_DEVICE_NAME       "WirelessHandbell"

// ---------------------------------------------------------------------------
// GLOBALS
// ---------------------------------------------------------------------------
Adafruit_LIS3DH lis = Adafruit_LIS3DH();

NimBLEServer* bleServer = nullptr;
NimBLECharacteristic* ringCharacteristic = nullptr;
bool bleClientConnected = false;

uint32_t ringCounter = 0;
uint32_t lastRingMillis = 0;
unsigned long lastSampleMillis = 0;

// Wire-format packet shared by BLE payload and ESP-NOW payload.
// Keep this tiny and fixed-size so both transports can send it as raw bytes.
typedef struct __attribute__((packed)) {
  uint32_t ringId;        // monotonically increasing ring counter
  uint16_t peakMilliG;    // peak acceleration magnitude in milli-g (for velocity-sensitive tone)
  uint32_t timestampMs;   // millis() at time of detection, for latency diagnostics
} RingEvent;

// ---------------------------------------------------------------------------
// BLE server callbacks (just for connect/disconnect bookkeeping + re-advertise)
// ---------------------------------------------------------------------------
// NOTE: NimBLE-Arduino 2.x changed these callback signatures to include a
// NimBLEConnInfo& parameter (and onDisconnect adds a reason code). If you're
// on NimBLE-Arduino 1.x instead, drop the NimBLEConnInfo&/reason parameters.
class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* server, NimBLEConnInfo& connInfo) override {
    bleClientConnected = true;
    Serial.println("[BLE] client connected");
  }
  void onDisconnect(NimBLEServer* server, NimBLEConnInfo& connInfo, int reason) override {
    bleClientConnected = false;
    Serial.println("[BLE] client disconnected, restarting advertising");
    NimBLEDevice::startAdvertising();
  }
};

// ---------------------------------------------------------------------------
// ESP-NOW send callback (just for debug logging)
// NOTE: arduino-esp32 core 3.x (ESP-IDF 5.x) changed this signature to pass
// a wifi_tx_info_t* instead of a raw MAC pointer. If you're on core 2.x,
// change the first parameter back to `const uint8_t* mac`.
// ---------------------------------------------------------------------------
void onEspNowSent(const wifi_tx_info_t* txInfo, esp_now_send_status_t status) {
  Serial.printf("[ESPNOW] send %s\n", status == ESP_NOW_SEND_SUCCESS ? "OK" : "FAILED");
}

// ---------------------------------------------------------------------------
// SETUP
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\nWireless Handbell transmitter starting...");

  // --- Accelerometer ---
  if (!lis.begin(0x18)) {          // Adafruit LIS3DH default addr; try 0x19 if this fails
    Serial.println("Could not find LIS3DH — check wiring/address!");
    while (1) delay(1000);
  }
  lis.setRange(LIS3DH_RANGE_4_G);
  lis.setDataRate(LIS3DH_DATARATE_400_HZ);
  Serial.println("LIS3DH ready.");

  // --- BLE (NimBLE) — initialized BEFORE WiFi/ESP-NOW ---
  // On the classic ESP32, WiFi and BLE share one radio via a software
  // coexistence arbiter. Bringing WiFi up first tends to let it dominate
  // that arbitration, starving BLE of airtime for advertising — the
  // advertising API calls succeed, but nothing actually goes out over the
  // air. Initializing BLE first, then WiFi, avoids that.
  NimBLEDevice::init(BLE_DEVICE_NAME);
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

  // --- WiFi radio up in station mode (required for ESP-NOW), but not joining a network ---
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);  // give the WiFi driver a moment to fully come up before reading MAC

  // --- ESP-NOW ---
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    while (1) delay(1000);
  }
  esp_now_register_send_cb(onEspNowSent);

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, RECEIVER_MAC, 6);
  peer.channel = 0;      // use current channel
  peer.encrypt = false;
  if (esp_now_add_peer(&peer) != ESP_OK) {
    Serial.println("Failed to add ESP-NOW peer");
  }

  // Tell the coexistence arbiter to share radio time fairly between WiFi
  // and BLE, instead of the default weighting (which tends to favor WiFi
  // and can leave BLE advertising unable to get any airtime). Must be
  // called after both stacks are up.
  esp_coex_preference_set(ESP_COEX_PREFER_BALANCE);

  // Read the MAC directly from the driver rather than via WiFi.macAddress() —
  // on some core versions that call can return all zeros if invoked too early.
  uint8_t mac[6];
  esp_wifi_get_mac(WIFI_IF_STA, mac);
  Serial.printf("My MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  Serial.println("Setup complete. Swing the bell!");
}

// ---------------------------------------------------------------------------
// LOOP — poll accelerometer, detect ring, transmit
// ---------------------------------------------------------------------------
void loop() {
  unsigned long now = millis();
  if (now - lastSampleMillis < SAMPLE_INTERVAL_MS) return;
  lastSampleMillis = now;

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

    // --- Transmit over ESP-NOW ---
    esp_now_send(RECEIVER_MAC, (uint8_t*)&evt, sizeof(evt));

    // --- Transmit over BLE ---
    if (bleClientConnected && ringCharacteristic) {
      ringCharacteristic->setValue((uint8_t*)&evt, sizeof(evt));
      ringCharacteristic->notify();
    }
  }
}
