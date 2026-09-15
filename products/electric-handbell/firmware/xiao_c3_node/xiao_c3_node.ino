/*
  ============================================================================
  Electric Handbell v0.3 -- XIAO ESP32C3 Concurrency Node (base firmware)
  ============================================================================
  Base firmware for each of the nine XIAO ESP32C3 "bell" boards in the
  concurrency demo (docs/hardware-design.md §7, measurement 4). This started
  as a pure USB bring-up test; it grew OTA, then an external LED, and now a
  minimal BLE peripheral -- currently just enough of one to serve as a power-
  consumption test harness (see firmware/power-test.md), not the real
  ring/damp/song-program logic yet. It reuses the exact GATT convention
  `feather_transmitter.ino` already established (same service UUID, same
  Ring/Damp characteristics and wire structs) so this is real groundwork
  for the eventual demo firmware, not throwaway code.

  WHY OTA: nine boards sitting close together on a breadboard are impractical
  to keep re-cabling for USB flashing one at a time. Flash this once per
  board over USB (off the breadboard, while there's room to plug in), and
  from then on updates go out over the network.

  FIRST-TIME SETUP (per board, still over USB)
    1. Copy wifi_credentials.h.example to wifi_credentials.h (same folder)
       and fill in your real WiFi SSID/password. That file is gitignored --
       never commit real credentials.
       NOTE: the ESP32C3 is 2.4GHz-only. If your network is 5GHz-only, or a
       mesh system that hides its 2.4GHz band under a different SSID, connect
       to the 2.4GHz name explicitly.
    2. Flash over USB as before (see firmware/README.md).
    3. Open Serial Monitor at 115200. You should see it join WiFi, then print
       its hostname (e.g. "ehb-c3-a1b2c3.local") and IP address. Note the
       hostname somewhere -- it's how you'll address this specific board for
       every OTA update after this.
    4. From then on: flash over the network instead of USB.

  PER-BOARD IDENTITY
    All nine boards run the exact same compiled binary -- there is no
    per-board source edit. Each one's OTA hostname AND its BLE advertised
    name are both derived automatically from its own factory MAC address
    (ehb-c3-<last 3 MAC bytes>), so they don't collide and each is
    independently addressable/identifiable in a scan list.

  EXTERNAL LED
    This board's only onboard LED is a charge-status LED driven by the charge
    IC, not a GPIO. The heartbeat LED is an external one, wired to the
    breadboard:

      XIAO pin D10 (GPIO10) --> ~220-330 ohm resistor --> LED anode (long leg)
      LED cathode (short leg, flat edge of the case)   --> XIAO GND pin

    Standard "sourcing" wiring -- GPIO HIGH lights the LED, GPIO LOW turns it
    off. The LED is only driven during the PLAYING state (see below) -- lit
    between a simulated ring and the next damp, off otherwise.

  DEMO STATE MACHINE (for the power test -- see firmware/power-test.md)
    STATE_IDLE          WiFi connected + OTA listening, BLE advertising,
                         not connected. Default/boot state.
    STATE_APP_CONNECTED A BLE central has connected. WiFi is torn down here
                         (we don't push OTA updates while connected to the
                         demo app -- see docs/hardware-design.md decision #3a
                         on not running both radio stacks concurrently in the
                         final bell; STATE_IDLE deliberately does run them
                         concurrently anyway, since measuring that combined
                         cost is the whole point of the "Idle" power test).
                         Writing to BLE_CHAR_PROGRAM_UUID while here
                         (simulating the app pushing a song program) doesn't
                         change state -- the GATT write itself is the activity
                         being measured.
    STATE_PLAYING        Entered by writing 0x01 to BLE_CHAR_CONTROL_UUID.
                         Alternates simulated ring/damp events on a timer:
                         Ring notify + LED on, then Damp notify + LED off.
                         Writing 0x00 to BLE_CHAR_CONTROL_UUID (or
                         disconnecting) returns to APP_CONNECTED/IDLE.
    (OTA-in-progress isn't a separate enum state -- it's just STATE_IDLE
    with a real OTA push happening. See firmware/power-test.md.)

  START BEACON LISTENER (docs/concurrency-demo-design.md §6)
    The board scans continuously for the phone's start beacon: a countdown to
    T0, repeated ~20 times over two seconds. Each repeat carries the time
    REMAINING rather than a timestamp, so catching any single beacon is
    enough and a late catch is no worse than an early one.

    Across several beacons the board keeps the EARLIEST implied T0. Every
    error between the phone stamping the countdown and us reading the clock
    -- staging latency, air time, callback latency -- can only make T0 look
    later than it is, never earlier, so the minimum is the best estimate.

    At T0 the board pulses its LED. An esp_timer one-shot drives that, not a
    poll in loop(), because ArduinoOTA.handle() can block for milliseconds
    and that jitter would land squarely in the measurement.

    WiFi is torn down as soon as the first beacon of a run arrives, roughly
    two seconds ahead of T0, so the radio is quiet and settled when it
    matters. It comes back a few seconds after the pulse so the board stays
    OTA-reachable between runs.

    Right now the LED pulse IS the experiment -- wire D10 to a PPK2 digital
    channel and the spread between boards' rising edges is the thing being
    measured. See android/ConcurrencyDemo/README.md.
  ============================================================================
*/

#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <NimBLEDevice.h>
#include <esp_timer.h>

#if __has_include("wifi_credentials.h")
  #include "wifi_credentials.h"
#else
  #error "Missing wifi_credentials.h -- copy wifi_credentials.h.example to wifi_credentials.h and fill in your WiFi details."
#endif

// --- BLE UUIDs -- must match feather_transmitter.ino / the Android app's
// service UUID and Ring/Damp characteristics. Program/Control are new here,
// numbered to continue that file's 0002-0005 sequence. ---
#define BLE_SERVICE_UUID      "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define BLE_CHAR_RING_UUID    "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
#define BLE_CHAR_DAMP_UUID    "6e400005-b5a3-f393-e0a9-e50e24dcca9e"
#define BLE_CHAR_PROGRAM_UUID "6e400006-b5a3-f393-e0a9-e50e24dcca9e"  // WRITE -- song program chunks (test harness: content ignored)
#define BLE_CHAR_CONTROL_UUID "6e400007-b5a3-f393-e0a9-e50e24dcca9e"  // WRITE -- 0x01 start playing, 0x00 stop

// --- Start beacon (see docs/concurrency-demo-design.md §6) ---
// The phone broadcasts a countdown to T0, repeated ~20 times over two seconds.
// Each repeat carries the time REMAINING, so a board catching repeat #48
// computes the same absolute instant as one catching #47 -- any single beacon
// is sufficient, and a late catch is no worse than an early one.
//
// This must match BeaconProtocol.kt in android/ConcurrencyDemo. The layout as
// it arrives over the air is:
//   byte 0..1  company ID, 0xFFFF little-endian (NimBLE includes it)
//   byte 2..3  magic "EH"
//   byte 4     message type
//   byte 5     run ID
//   byte 6..7  milliseconds remaining until T0, uint16 little-endian
#define BEACON_MAGIC_0     0x45  // 'E'
#define BEACON_MAGIC_1     0x48  // 'H'
#define BEACON_MSG_START   0x01
#define BEACON_FRAME_LEN   8
#define T0_PULSE_MS        50    // LED pulse width at T0 -- the RISING edge is the measurement
#define WIFI_RESTORE_MS    5000  // bring WiFi back this long after a run, so OTA works again

// Same fast connection-interval target as feather_transmitter.ino -- this
// test should reflect real demo latency requirements, not a lazy default.
#define CONN_INTERVAL_MIN     6     //  7.5ms
#define CONN_INTERVAL_MAX     12    // 15ms
#define CONN_LATENCY          0     // never skip a connection event
#define CONN_TIMEOUT          400   // 4000ms supervision timeout

// Wire-format structs -- must match RingEvent/DampEvent in
// feather_transmitter.ino (and RingEvent.kt on the Android side).
typedef struct __attribute__((packed)) {
  uint32_t ringId;
  uint16_t peakMilliG;
  uint32_t timestampMs;
} RingEvent;

typedef struct __attribute__((packed)) {
  uint32_t timestampMs;
} DampEvent;

enum DemoState : uint8_t {
  STATE_IDLE = 0,
  STATE_APP_CONNECTED,
  STATE_PLAYING,
};

const uint8_t LED_PIN = 10;  // D10 -- external LED, active-HIGH (see wiring note above)
const unsigned long WIFI_CONNECT_TIMEOUT_MS = 15000;
const unsigned long WIFI_RETRY_INTERVAL_MS = 10000;
const unsigned long PLAY_STEP_INTERVAL_MS = 1500;  // ring/damp cadence while "playing"

char hostname[24];
volatile DemoState demoState = STATE_IDLE;
bool otaReady = false;
unsigned long lastWifiRetryMs = 0;
unsigned long lastHeartbeatMs = 0;

NimBLEServer* bleServer = nullptr;
NimBLECharacteristic* ringCharacteristic = nullptr;
NimBLECharacteristic* dampCharacteristic = nullptr;
uint32_t nextRingId = 1;
bool playLedOn = false;
unsigned long lastPlayStepMs = 0;

// --- Start beacon state. Touched from the NimBLE host task (scan callback)
// and the esp_timer task, so anything loop() reads is volatile. ---
NimBLEScan* bleScan = nullptr;
esp_timer_handle_t t0Timer = nullptr;
volatile bool t0Armed = false;
volatile uint8_t armedRunId = 0;
volatile int64_t bestT0Us = 0;       // best (earliest) estimate of T0, esp_timer clock
volatile uint16_t beaconsHeard = 0;
volatile bool t0Fired = false;
volatile unsigned long pulseStartMs = 0;
unsigned long wifiRestoreAtMs = 0;   // 0 = nothing scheduled

void buildHostname() {
  uint64_t mac = ESP.getEfuseMac();
  // Last 3 bytes of the MAC (the NIC-specific part) -- the first 3 are a
  // fixed Espressif OUI shared by every board, so they wouldn't distinguish
  // anything.
  uint8_t b3 = (mac >> 24) & 0xFF;
  uint8_t b4 = (mac >> 32) & 0xFF;
  uint8_t b5 = (mac >> 40) & 0xFF;
  snprintf(hostname, sizeof(hostname), "ehb-c3-%02x%02x%02x", b3, b4, b5);
}

void printBootBanner() {
  Serial.println();
  Serial.println("Electric Handbell v0.3 -- XIAO ESP32C3 node firmware");
  Serial.printf("Chip: %s rev%d, %d core(s) @ %dMHz\n",
                ESP.getChipModel(), ESP.getChipRevision(),
                ESP.getChipCores(), ESP.getCpuFreqMHz());
  Serial.printf("Flash: %luKB\n", (unsigned long)(ESP.getFlashChipSize() / 1024));
  Serial.printf("Hostname: %s\n", hostname);
}

// ---------------------------------------------------------------------------
// WiFi / OTA -- only active in STATE_IDLE. Torn down on BLE connect, brought
// back up on BLE disconnect. See the state-machine note at the top of file.
// ---------------------------------------------------------------------------

void connectWifi() {
  Serial.printf("Connecting to WiFi \"%s\"", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(hostname);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
    delay(300);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("WiFi connected. IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("WiFi connect timed out -- will keep retrying in the background.");
  }
}

void setupOta() {
  ArduinoOTA.setHostname(hostname);
  if (strlen(OTA_PASSWORD) > 0) {
    ArduinoOTA.setPassword(OTA_PASSWORD);
  }

  ArduinoOTA.onStart([]() {
    Serial.println("OTA: update starting...");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("OTA: update complete, rebooting...");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("OTA: %u%%\r", (progress * 100) / total);
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("OTA: error [%u]: ", error);
    switch (error) {
      case OTA_AUTH_ERROR:    Serial.println("auth failed"); break;
      case OTA_BEGIN_ERROR:   Serial.println("begin failed"); break;
      case OTA_CONNECT_ERROR: Serial.println("connect failed"); break;
      case OTA_RECEIVE_ERROR: Serial.println("receive failed"); break;
      case OTA_END_ERROR:     Serial.println("end failed"); break;
      default:                Serial.println("unknown error"); break;
    }
  });

  ArduinoOTA.begin();
  otaReady = true;
  Serial.printf("OTA ready -- upload target: %s.local\n", hostname);
}

// Brings WiFi + OTA up for STATE_IDLE. Safe to call repeatedly.
void enterIdleRadioState() {
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }
  // otaReady gets (re)set once WiFi actually reconnects, in loop().
}

// Tears WiFi + OTA down for STATE_APP_CONNECTED / STATE_PLAYING.
void exitIdleRadioState() {
  otaReady = false;
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  Serial.println("WiFi off -- BLE client connected.");
}

// ---------------------------------------------------------------------------
// BLE
// ---------------------------------------------------------------------------

// Set from BLE callbacks, acted on in loop() -- see the comment below.
volatile bool wifiTeardownPending = false;
volatile bool wifiRestorePending = false;

class ServerCallbacks : public NimBLEServerCallbacks {
  // IMPORTANT: keep these callbacks fast. They run on NimBLE's own host
  // task while a connection procedure is still being finalized -- calling
  // into the WiFi driver (WiFi.disconnect()/WiFi.mode()) directly from here
  // stalled that task long enough to blow the connection timeout (observed
  // as Android's GATT_CONN_TIMEOUT / error 147 during power testing). Heavy
  // radio work is deferred to loop() via these flags instead.
  void onConnect(NimBLEServer* server, NimBLEConnInfo& connInfo) override {
    Serial.println("[BLE] client connected -- requesting fast connection params");
    server->updateConnParams(connInfo.getConnHandle(),
                              CONN_INTERVAL_MIN, CONN_INTERVAL_MAX,
                              CONN_LATENCY, CONN_TIMEOUT);
    demoState = STATE_APP_CONNECTED;
    wifiTeardownPending = true;
  }
  void onDisconnect(NimBLEServer* server, NimBLEConnInfo& connInfo, int reason) override {
    Serial.println("[BLE] client disconnected, restarting advertising");
    digitalWrite(LED_PIN, LOW);
    demoState = STATE_IDLE;
    wifiRestorePending = true;
    NimBLEDevice::startAdvertising();
  }
};

// Test harness only: content is ignored, this just needs to be a real GATT
// write so the radio activity of "receiving a song program" is genuine.
class ProgramCharacteristicCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo& connInfo) override {
    Serial.printf("[BLE] program chunk received: %u bytes\n", (unsigned)c->getValue().length());
  }
};

class ControlCharacteristicCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo& connInfo) override {
    std::string value = c->getValue();
    if (value.empty()) return;
    uint8_t cmd = (uint8_t)value[0];
    if (cmd == 0x01) {
      Serial.println("[BLE] control: start playing");
      demoState = STATE_PLAYING;
      lastPlayStepMs = millis() - PLAY_STEP_INTERVAL_MS;  // fire the first step immediately
      playLedOn = false;
    } else {
      Serial.println("[BLE] control: stop playing");
      demoState = STATE_APP_CONNECTED;
      playLedOn = false;
      digitalWrite(LED_PIN, LOW);
    }
  }
};

// ---------------------------------------------------------------------------
// START BEACON LISTENER
// ---------------------------------------------------------------------------

// Fires at T0. Dispatched on the esp_timer task, NOT in an ISR, so ordinary
// calls are safe here. Driving the pin from a timer rather than polling in
// loop() keeps loop jitter -- ArduinoOTA.handle() in particular can block for
// milliseconds -- out of the measurement.
void onT0(void* arg) {
  digitalWrite(LED_PIN, HIGH);
  pulseStartMs = millis();
  t0Fired = true;
}

void armT0Timer(int64_t delayUs) {
  if (delayUs < 0) delayUs = 0;
  esp_timer_stop(t0Timer);  // harmless if not running
  esp_timer_start_once(t0Timer, delayUs);
}

class BeaconScanCallbacks : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice* dev) override {
    // Read the clock FIRST. Everything below this line adds delay that would
    // otherwise be attributed to the beacon.
    int64_t nowUs = esp_timer_get_time();

    if (!dev->haveManufacturerData()) return;
    std::string md = dev->getManufacturerData();
    if (md.length() < BEACON_FRAME_LEN) return;

    const uint8_t* b = (const uint8_t*)md.data();
    if (b[0] != 0xFF || b[1] != 0xFF) return;                      // company ID 0xFFFF
    if (b[2] != BEACON_MAGIC_0 || b[3] != BEACON_MAGIC_1) return;  // not ours
    if (b[4] != BEACON_MSG_START) return;

    uint8_t runId = b[5];
    uint16_t msRemaining = (uint16_t)b[6] | ((uint16_t)b[7] << 8);
    int64_t candidateUs = nowUs + (int64_t)msRemaining * 1000;

    // WHY THE MINIMUM: every error between the phone stamping "ms remaining"
    // and us reading the clock -- the phone's staging latency, time in the
    // air, our own callback latency -- can only make T0 look LATER than it
    // really is. None of them can make it look earlier. So across many
    // beacons, the smallest estimate is the closest to truth.
    if (!t0Armed || runId != armedRunId) {
      t0Armed = true;
      armedRunId = runId;
      bestT0Us = candidateUs;
      beaconsHeard = 1;
      t0Fired = false;
      // Quiet the radio well before T0. WiFi teardown is heavy, so it goes
      // through loop() rather than happening here -- and starting it ~2s out
      // leaves plenty of settling time.
      wifiTeardownPending = true;
      armT0Timer(candidateUs - nowUs);
    } else {
      beaconsHeard++;
      if (candidateUs < bestT0Us) {
        bestT0Us = candidateUs;
        armT0Timer(candidateUs - nowUs);
      }
    }
  }
};

void setupBeaconListener() {
  esp_timer_create_args_t args = {};
  args.callback = &onT0;
  args.dispatch_method = ESP_TIMER_TASK;
  args.name = "t0";
  esp_timer_create(&args, &t0Timer);

  bleScan = NimBLEDevice::getScan();
  // wantDuplicates AND setDuplicateFilter(0): without both, we would see only
  // the FIRST beacon of the countdown and never the updates -- which would
  // defeat the entire mechanism, since the countdown value is the payload.
  bleScan->setScanCallbacks(new BeaconScanCallbacks(), true);
  bleScan->setDuplicateFilter(0);
  bleScan->setActiveScan(false);  // passive: we need the advert, not a scan response
  bleScan->setInterval(100);
  bleScan->setWindow(100);        // window == interval: listen continuously
  bleScan->start(0, false, true); // 0 = scan forever
  Serial.println("Beacon listener scanning.");
}

void setupBle() {
  NimBLEDevice::init(hostname);
  NimBLEDevice::setPower(9);  // +9dBm, matches feather_transmitter.ino

  bleServer = NimBLEDevice::createServer();
  bleServer->setCallbacks(new ServerCallbacks());

  NimBLEService* service = bleServer->createService(BLE_SERVICE_UUID);
  ringCharacteristic = service->createCharacteristic(
      BLE_CHAR_RING_UUID,
      NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ);
  dampCharacteristic = service->createCharacteristic(
      BLE_CHAR_DAMP_UUID,
      NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ);
  NimBLECharacteristic* programCharacteristic = service->createCharacteristic(
      BLE_CHAR_PROGRAM_UUID,
      NIMBLE_PROPERTY::WRITE);
  programCharacteristic->setCallbacks(new ProgramCharacteristicCallbacks());
  NimBLECharacteristic* controlCharacteristic = service->createCharacteristic(
      BLE_CHAR_CONTROL_UUID,
      NIMBLE_PROPERTY::WRITE);
  controlCharacteristic->setCallbacks(new ControlCharacteristicCallbacks());
  service->start();

  NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
  // Same 31-byte primary-packet constraint as feather_transmitter.ino --
  // name in the primary packet, service UUID in the scan response.
  advertising->setName(hostname);
  NimBLEAdvertisementData scanResponseData;
  scanResponseData.addServiceUUID(BLE_SERVICE_UUID);
  advertising->setScanResponseData(scanResponseData);
  advertising->start();

  Serial.printf("BLE advertising as \"%s\".\n", hostname);
  Serial.printf("BLE address: %s\n", NimBLEDevice::getAddress().toString().c_str());
}

// One ring+damp step of the fake "song" -- fires on a timer while PLAYING.
void playStep(unsigned long now) {
  if (now - lastPlayStepMs < PLAY_STEP_INTERVAL_MS) return;
  lastPlayStepMs = now;
  playLedOn = !playLedOn;
  digitalWrite(LED_PIN, playLedOn ? HIGH : LOW);

  if (playLedOn) {
    RingEvent evt = { nextRingId++, 2500, (uint32_t)now };  // fake mid-dynamic peak
    ringCharacteristic->setValue((uint8_t*)&evt, sizeof(evt));
    ringCharacteristic->notify();
    Serial.printf("[BLE] RING #%lu\n", (unsigned long)evt.ringId);
  } else {
    DampEvent evt = { (uint32_t)now };
    dampCharacteristic->setValue((uint8_t*)&evt, sizeof(evt));
    dampCharacteristic->notify();
    Serial.println("[BLE] DAMP");
  }
}

void setup() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);  // start OFF (active-HIGH)

  Serial.begin(115200);
  delay(300);  // give USB CDC a moment to enumerate before the first print

  buildHostname();
  printBootBanner();
  connectWifi();
  if (WiFi.status() == WL_CONNECTED) {
    setupOta();
  }
  setupBle();
  setupBeaconListener();

  Serial.println("Setup complete -- STATE_IDLE, advertising, WiFi/OTA up.");
  Serial.println();
}

void loop() {
  unsigned long now = millis();

  // Heavy radio work deferred out of the BLE callbacks -- see the comment
  // on ServerCallbacks. Handled first, before anything else this iteration.
  if (wifiTeardownPending) {
    wifiTeardownPending = false;
    exitIdleRadioState();
  }
  if (wifiRestorePending) {
    wifiRestorePending = false;
    enterIdleRadioState();
  }

  // --- Start beacon: end the T0 pulse and report the run ---
  if (t0Fired && (long)(now - pulseStartMs) >= T0_PULSE_MS) {
    digitalWrite(LED_PIN, LOW);
    t0Fired = false;
    t0Armed = false;
    Serial.printf("[BEACON] run %u fired at T0, %u beacons heard\n",
                  (unsigned)armedRunId, (unsigned)beaconsHeard);
    // Bring WiFi back shortly so the board is OTA-reachable between runs.
    wifiRestoreAtMs = now + WIFI_RESTORE_MS;
  }
  if (wifiRestoreAtMs != 0 && (long)(now - wifiRestoreAtMs) >= 0) {
    wifiRestoreAtMs = 0;
    wifiRestorePending = true;
  }

  switch (demoState) {
    case STATE_IDLE:
      if (otaReady) {
        ArduinoOTA.handle();
      }
      if (WiFi.status() != WL_CONNECTED) {
        if (now - lastWifiRetryMs >= WIFI_RETRY_INTERVAL_MS) {
          lastWifiRetryMs = now;
          Serial.println("WiFi not connected -- retrying...");
          WiFi.disconnect();
          WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        }
      } else if (!otaReady) {
        setupOta();  // connected for the first time after an earlier failed attempt
      }
      break;

    case STATE_APP_CONNECTED:
      // Waiting for a Control or Program write; nothing to do here.
      break;

    case STATE_PLAYING:
      playStep(now);
      break;
  }

  if (now - lastHeartbeatMs >= 1000) {
    lastHeartbeatMs = now;
    Serial.printf("heartbeat  millis=%lu  state=%d  wifi=%s\n",
                  now, (int)demoState,
                  WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString().c_str() : "off");
  }
}
