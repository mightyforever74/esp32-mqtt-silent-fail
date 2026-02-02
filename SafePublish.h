/**
 * SafePublish.h - Safe MQTT publish wrapper for PubSubClient
 * 
 * Prevents silent publish failures caused by buffer overflow.
 * PubSubClient's default buffer is 256 bytes — if your topic + payload
 * exceeds this, publish() silently returns false with no error message.
 * 
 * Usage:
 *   #include "SafePublish.h"
 *   SafePublish safeMqtt(mqttClient);
 *   safeMqtt.publish("my/topic", jsonPayload);
 * 
 * Author: Cem Sahinkaya (@mightyforever74)
 * License: MIT
 */

#ifndef SAFE_PUBLISH_H
#define SAFE_PUBLISH_H

#include <PubSubClient.h>

class SafePublish {
public:
    SafePublish(PubSubClient &client) : _client(client) {}

    /**
     * Publish with automatic buffer size validation.
     * Logs detailed error info if the message won't fit.
     * 
     * @param topic   MQTT topic string
     * @param payload JSON or text payload
     * @return true if published successfully
     */
    bool publish(const char* topic, const char* payload) {
        int topicLen = strlen(topic);
        int payloadLen = strlen(payload);
        int totalSize = topicLen + payloadLen + MQTT_OVERHEAD;
        int bufferSize = _client.getBufferSize();

        // Pre-flight check: will it fit?
        if (totalSize > bufferSize) {
            Serial.println();
            Serial.println("╔══════════════════════════════════════════╗");
            Serial.println("║  ⚠️  MQTT BUFFER OVERFLOW DETECTED      ║");
            Serial.println("╚══════════════════════════════════════════╝");
            Serial.printf("  Topic:      %s (%d bytes)\n", topic, topicLen);
            Serial.printf("  Payload:    %d bytes\n", payloadLen);
            Serial.printf("  Total:      %d bytes\n", totalSize);
            Serial.printf("  Buffer:     %d bytes\n", bufferSize);
            Serial.printf("  Overflow:   %d bytes over limit!\n", totalSize - bufferSize);
            Serial.println();
            Serial.printf("  FIX: Add this to setup():\n");
            Serial.printf("  mqtt_client.setBufferSize(%d);\n", nextPowerOf2(totalSize + 50));
            Serial.println();
            _failCount++;
            return false;
        }

        // Connection check
        if (!_client.connected()) {
            Serial.printf("[MQTT] Publish failed: not connected (topic=%s)\n", topic);
            _failCount++;
            return false;
        }

        // Actual publish
        bool sent = _client.publish(topic, payload);

        if (sent) {
            _successCount++;
            Serial.printf("[MQTT] ✅ Published: %s (%d bytes)\n", topic, payloadLen);
        } else {
            _failCount++;
            Serial.printf("[MQTT] ❌ Failed: %s (%d bytes, buffer=%d)\n", 
                         topic, totalSize, bufferSize);
        }

        return sent;
    }

    /**
     * Publish with String parameters (convenience overload)
     */
    bool publish(const String &topic, const String &payload) {
        return publish(topic.c_str(), payload.c_str());
    }

    /**
     * Auto-configure buffer size based on your largest expected message.
     * Call this in setup() BEFORE any publish calls.
     * 
     * @param maxTopicLen    Longest topic you'll use (bytes)
     * @param maxPayloadLen  Largest payload you'll send (bytes)
     * @return The buffer size that was set
     */
    int autoConfigureBuffer(int maxTopicLen, int maxPayloadLen) {
        int needed = maxTopicLen + maxPayloadLen + MQTT_OVERHEAD + 50; // 50 byte safety margin
        int bufSize = nextPowerOf2(needed);
        _client.setBufferSize(bufSize);
        Serial.printf("[MQTT] Buffer auto-configured: %d bytes (needed %d)\n", bufSize, needed);
        return bufSize;
    }

    /**
     * Print publish statistics
     */
    void printStats() {
        Serial.println("── MQTT Publish Stats ──");
        Serial.printf("  Success: %lu\n", _successCount);
        Serial.printf("  Failed:  %lu\n", _failCount);
        if (_failCount > 0) {
            Serial.printf("  ⚠️  %.1f%% failure rate\n", 
                         (float)_failCount / (_successCount + _failCount) * 100);
        }
    }

    unsigned long getSuccessCount() { return _successCount; }
    unsigned long getFailCount() { return _failCount; }
    void resetStats() { _successCount = 0; _failCount = 0; }

private:
    PubSubClient &_client;
    unsigned long _successCount = 0;
    unsigned long _failCount = 0;
    static const int MQTT_OVERHEAD = 10; // MQTT protocol overhead bytes

    int nextPowerOf2(int n) {
        int p = 128;
        while (p < n && p < 8192) p *= 2;
        return p;
    }
};

#endif // SAFE_PUBLISH_H
