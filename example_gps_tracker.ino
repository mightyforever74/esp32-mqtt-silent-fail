/**
 * example_gps_tracker.ino
 * 
 * Demonstrates the silent publish failure bug and the SafePublish fix.
 * This is a simplified GPS tracker that publishes location data via MQTT.
 * 
 * THE BUG: PubSubClient's default 256-byte buffer silently drops
 * messages that exceed the limit. publish() returns false, but
 * if you don't check it, you'll never know data is being lost.
 * 
 * THE FIX: Use SafePublish wrapper OR call setBufferSize() in setup().
 * 
 * Author: Cem Sahinkaya (@mightyforever74)
 * License: MIT
 */

#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <TinyGPSPlus.h>
#include "SafePublish.h"

// ── Configuration ──
const char* WIFI_SSID     = "your-wifi";
const char* WIFI_PASS     = "your-password";
const char* MQTT_BROKER   = "your-broker.com";
const int   MQTT_PORT     = 1883;
const char* MQTT_USER     = "your-username";
const char* MQTT_PASS     = "your-password";
const char* DEVICE_ID     = "ESP32-001";

// ── Objects ──
WiFiClient espClient;
PubSubClient mqttClient(espClient);
SafePublish safeMqtt(mqttClient);  // ✅ Safe publish wrapper
TinyGPSPlus gps;
HardwareSerial gpsSerial(2);

// ── Topics ──
String topicLocation;
String topicTelemetry;
String topicHeartbeat;

// ── Timers ──
unsigned long lastLocation  = 0;
unsigned long lastTelemetry = 0;
unsigned long lastHeartbeat = 0;
unsigned long lastStats     = 0;

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n================================");
    Serial.println("  ESP32 MQTT SafePublish Demo");
    Serial.println("================================\n");

    // ──────────────────────────────────────────────
    // METHOD 1: Manual buffer size (simple fix)
    // ──────────────────────────────────────────────
    // mqttClient.setBufferSize(512);

    // ──────────────────────────────────────────────
    // METHOD 2: Auto-configure based on your needs
    // ──────────────────────────────────────────────
    // Longest topic: ~40 chars, largest payload: ~300 bytes
    safeMqtt.autoConfigureBuffer(40, 300);

    // Setup topics
    topicLocation  = String("fleet/device/") + DEVICE_ID + "/location";
    topicTelemetry = String("fleet/device/") + DEVICE_ID + "/telemetry";
    topicHeartbeat = String("fleet/device/") + DEVICE_ID + "/heartbeat";

    // GPS Serial (RX=16, TX=17)
    gpsSerial.begin(9600, SERIAL_8N1, 16, 17);

    // WiFi
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.print("[WiFi] Connecting");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.printf("\n[WiFi] Connected: %s\n", WiFi.localIP().toString().c_str());

    // MQTT
    mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
    connectMQTT();
}

void connectMQTT() {
    while (!mqttClient.connected()) {
        Serial.print("[MQTT] Connecting...");
        if (mqttClient.connect(DEVICE_ID, MQTT_USER, MQTT_PASS)) {
            Serial.println(" ✅ Connected!");
            Serial.printf("[MQTT] Buffer size: %d bytes\n", mqttClient.getBufferSize());
        } else {
            Serial.printf(" ❌ Failed (rc=%d). Retrying in 5s...\n", mqttClient.state());
            delay(5000);
        }
    }
}

void loop() {
    // Feed GPS parser
    while (gpsSerial.available() > 0) {
        gps.encode(gpsSerial.read());
    }

    // Reconnect if needed
    if (!mqttClient.connected()) connectMQTT();
    mqttClient.loop();

    unsigned long now = millis();

    // ── LOCATION: every 30 seconds ──
    if (now - lastLocation >= 30000) {
        lastLocation = now;
        publishLocation();
    }

    // ── TELEMETRY: every 60 seconds ──
    if (now - lastTelemetry >= 60000) {
        lastTelemetry = now;
        publishTelemetry();
    }

    // ── HEARTBEAT: every 60 seconds ──
    if (now - lastHeartbeat >= 60000) {
        lastHeartbeat = now;
        publishHeartbeat();
    }

    // ── STATS: every 5 minutes ──
    if (now - lastStats >= 300000) {
        lastStats = now;
        safeMqtt.printStats();
    }
}

// ── PUBLISH FUNCTIONS ──

void publishLocation() {
    if (!gps.location.isValid()) {
        Serial.println("[LOC] Skipped: no GPS fix");
        return;
    }

    StaticJsonDocument<384> doc;
    doc["device_id"]  = DEVICE_ID;
    doc["latitude"]   = gps.location.lat();
    doc["longitude"]  = gps.location.lng();
    doc["speed"]      = gps.speed.kmph();
    doc["heading"]    = gps.course.isValid() ? gps.course.deg() : 0;
    doc["altitude"]   = gps.altitude.isValid() ? gps.altitude.meters() : 0;
    doc["satellites"] = gps.satellites.value();
    doc["hdop"]       = gps.hdop.isValid() ? gps.hdop.hdop() : 0;
    doc["wifi_rssi"]  = WiFi.RSSI();

    String json;
    serializeJson(doc, json);

    // ❌ THE BUG — this is what most people write:
    // mqtt_client.publish(topicLocation.c_str(), json.c_str());
    // Serial.println("[LOC] Published!");  // LIES! May not have been sent!

    // ✅ THE FIX — use SafePublish:
    safeMqtt.publish(topicLocation, json);
}

void publishTelemetry() {
    StaticJsonDocument<256> doc;
    doc["device_id"]  = DEVICE_ID;
    doc["uptime"]     = millis() / 1000;
    doc["wifi_rssi"]  = WiFi.RSSI();
    doc["free_heap"]  = ESP.getFreeHeap();
    doc["gps_valid"]  = gps.location.isValid();
    doc["satellites"] = gps.satellites.value();

    String json;
    serializeJson(doc, json);

    safeMqtt.publish(topicTelemetry, json);
}

void publishHeartbeat() {
    StaticJsonDocument<128> doc;
    doc["device_id"] = DEVICE_ID;
    doc["uptime"]    = millis() / 1000;

    String json;
    serializeJson(doc, json);

    safeMqtt.publish(topicHeartbeat, json);
}
