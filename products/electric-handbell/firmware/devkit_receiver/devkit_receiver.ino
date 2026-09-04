/*
  ============================================================================
  Wireless Handbell — ESP32-DEVKITC-V4 Receiver
  ============================================================================
  Listens for ESP-NOW ring events from the Feather transmitter and forwards
  each one to the PC over USB serial as a simple text line:

      RING,<ringId>,<peakMilliG>,<txTimestampMs>,<rxMillis>

  FIRST BOOT
    Flash this sketch, open Serial Monitor at 115200 baud. It prints this
    board's MAC address — copy that into RECEIVER_MAC in feather_transmitter.ino.
  ============================================================================
*/

#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>

typedef struct __attribute__((packed)) {
  uint32_t ringId;
  uint16_t peakMilliG;
  uint32_t timestampMs;
} RingEvent;

void onDataReceived(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  if (len != sizeof(RingEvent)) {
    Serial.printf("Got unexpected packet size %d (expected %d)\n", len, (int)sizeof(RingEvent));
    return;
  }
  RingEvent evt;
  memcpy(&evt, data, sizeof(evt));

  // Simple CSV line protocol — easy to parse from Python, Java/Kotlin, etc.
  Serial.printf("RING,%lu,%u,%lu,%lu\n",
                (unsigned long)evt.ringId,
                evt.peakMilliG,
                (unsigned long)evt.timestampMs,
                (unsigned long)millis());
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\nWireless Handbell ESP-NOW receiver starting...");

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);  // give the WiFi driver a moment to fully come up before reading MAC

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    while (1) delay(1000);
  }

  // Read the MAC directly from the driver rather than via WiFi.macAddress() —
  // on some core versions that call can return all zeros if invoked too early.
  uint8_t mac[6];
  esp_wifi_get_mac(WIFI_IF_STA, mac);
  Serial.printf("My MAC (put this in feather_transmitter.ino RECEIVER_MAC): "
                "%02X:%02X:%02X:%02X:%02X:%02X\n",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  esp_now_register_recv_cb(onDataReceived);

  Serial.println("Listening for ring events...");
}

void loop() {
  // Everything happens in the ESP-NOW receive callback.
  delay(10);
}
