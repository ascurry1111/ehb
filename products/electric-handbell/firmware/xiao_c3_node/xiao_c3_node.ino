/*
  ============================================================================
  Electric Handbell v0.3 -- XIAO ESP32C3 Concurrency Node (base firmware)
  ============================================================================
  Base firmware for each of the nine XIAO ESP32C3 "bell" boards in the
  concurrency demo (docs/hardware-design.md §7, measurement 4). This started
  as a pure USB bring-up test; it's now also the OTA on-ramp -- flash this
  once per board over USB, and every firmware iteration after that (this LED
  step, then ring/damp logic and BLE) can be pushed over WiFi instead.

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
    4. From then on: flash over the network instead of USB (see
       firmware/tools/ once the OTA path is validated end-to-end).

  PER-BOARD IDENTITY
    All nine boards run the exact same compiled binary -- there is no
    per-board source edit. Each one's OTA hostname is derived automatically
    from its own factory MAC address (ehb-c3-<last 3 MAC bytes>), so they
    don't collide on the network and each is independently addressable.

  EXTERNAL LED
    This board's only onboard LED is a charge-status LED driven by the charge
    IC, not a GPIO -- toggling GPIO10 produced no visible blink on the
    hardware in hand. So the heartbeat LED is an external one, wired to the
    breadboard:

      XIAO pin D10 (GPIO10) --> ~220-330 ohm resistor --> LED anode (long leg)
      LED cathode (short leg, flat edge of the case)   --> XIAO GND pin

    Standard "sourcing" wiring -- GPIO HIGH lights the LED, GPIO LOW turns it
    off -- so no active-LOW inversion is needed in code. The resistor can sit
    on either leg of the LED; anode side shown above is just convention.
    Any of the XIAO's GND pins works; it doesn't need to share the breadboard
    power rails for this single-board USB-powered test.
  ============================================================================
*/

#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>

#if __has_include("wifi_credentials.h")
  #include "wifi_credentials.h"
#else
  #error "Missing wifi_credentials.h -- copy wifi_credentials.h.example to wifi_credentials.h and fill in your WiFi details."
#endif

const uint8_t LED_PIN = 10;  // D10 -- external LED, active-HIGH (see wiring note above)
const unsigned long BLINK_INTERVAL_MS = 1000;
const unsigned long WIFI_CONNECT_TIMEOUT_MS = 15000;
const unsigned long WIFI_RETRY_INTERVAL_MS = 10000;

unsigned long lastToggleMs = 0;
bool ledOn = false;
uint32_t heartbeatCount = 0;
unsigned long lastWifiRetryMs = 0;
char hostname[24];
bool otaReady = false;

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
    Serial.println("WiFi connect timed out -- continuing without it. "
                    "Will keep retrying in the background; OTA won't be "
                    "available until it connects.");
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

  Serial.println("If you see this and the heartbeat lines below, the board is good.");
  Serial.println();
}

void loop() {
  if (otaReady) {
    ArduinoOTA.handle();
  }

  // If WiFi dropped (or never connected), retry periodically without
  // blocking the rest of loop().
  if (WiFi.status() != WL_CONNECTED) {
    unsigned long now = millis();
    if (now - lastWifiRetryMs >= WIFI_RETRY_INTERVAL_MS) {
      lastWifiRetryMs = now;
      Serial.println("WiFi not connected -- retrying...");
      WiFi.disconnect();
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    }
  } else if (!otaReady) {
    // Connected for the first time after an earlier failed attempt.
    setupOta();
  }

  unsigned long now = millis();
  if (now - lastToggleMs >= BLINK_INTERVAL_MS) {
    lastToggleMs = now;
    ledOn = !ledOn;
    digitalWrite(LED_PIN, ledOn ? HIGH : LOW);
    heartbeatCount++;
    Serial.printf("heartbeat #%lu  millis=%lu  wifi=%s\n",
                  (unsigned long)heartbeatCount, now,
                  WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString().c_str() : "disconnected");
  }
}
