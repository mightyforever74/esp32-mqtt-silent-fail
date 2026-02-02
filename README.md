# 🔇 ESP32 MQTT Publish Silently Fails — And Your Serial Monitor Won't Tell You

> **TL;DR:** PubSubClient's default buffer is 256 bytes. If your MQTT payload + topic exceeds this, `publish()` silently returns `false` — but if you don't check the return value, you'll never know. Your Serial Monitor happily prints "Published!" while zero bytes reach the broker.

## The Symptom

You're building an ESP32 IoT project. Your Serial Monitor shows everything working perfectly:

```
[GPS] sats=8 lat=41.013045 lon=28.909387 spd=0.1
[LOC] Published: 41.013045,28.909387 spd=0.1 sats=8    ← looks great!
[TEL] Published: system telemetry                        ← also great!
```

But when you check your MQTT broker — **location messages never arrive.** Telemetry works. Heartbeat works. Location? Gone. Vanished into thin air.

## The Investigation

Here's the EMQX broker trace from a real production system. Two minutes of monitoring:

```
fleet/device/E8198871FE68/telemetry ✅ (every 60s — arrives fine)
fleet/device/E8198871FE68/heartbeat ✅ (every 60s — arrives fine)
fleet/device/E8198871FE68/location  ❌ (every 30s — NEVER arrives)
```

All three use the same `mqtt_client`. Same connection. Same broker. Two work, one doesn't.

## The Root Cause

**PubSubClient's default buffer size is 256 bytes.** The total MQTT packet (topic + payload + protocol overhead) must fit within this buffer.

Let's do the math:

| Message | Topic Length | Payload Size | Total | Status |
|---------|-------------|-------------|-------|--------|
| Telemetry | 40 bytes | ~165 bytes | **~215 bytes** | ✅ Fits |
| Heartbeat | 40 bytes | ~80 bytes | **~130 bytes** | ✅ Fits |
| Location | 38 bytes | ~220 bytes | **~268 bytes** | ❌ **Exceeds 256!** |

The location payload carries more fields (latitude, longitude, speed, heading, altitude, satellites, hdop, wifi_rssi, fw_version), pushing it just over the limit.

### Why You Don't Notice

Here's the killer pattern — code that looks correct but silently fails:

```cpp
// ❌ THE BUG: publish() returns false, but you never check it
mqtt_client.publish(topic_location.c_str(), json.c_str());
Serial.printf("[LOC] Published: %.6f,%.6f\n", lat, lon);  // Always prints!
```

`publish()` returns a `boolean` — `false` means the message was NOT sent. But since virtually no one checks this return value, the Serial print executes unconditionally, creating the illusion of success.

## The Fix

**One line of code.** Add this in your `setup()`:

```cpp
void setup() {
  Serial.begin(115200);
  
  // ✅ THE FIX: Increase MQTT buffer from default 256 to 512 bytes
  mqtt_client.setBufferSize(512);
  
  // ... rest of setup
}
```

### Better: Always Check Publish Results

```cpp
// ✅ CORRECT: Check return value and log actual status
bool sent = mqtt_client.publish(topic.c_str(), json.c_str());
Serial.printf("[LOC] %s: %.6f,%.6f (len=%d)\n", 
  sent ? "Published" : "FAILED",   // Now you'll actually know
  lat, lon, json.length());
```

## Quick Reference

| Buffer Size | Max Payload (approx) | Use Case |
|------------|---------------------|----------|
| 256 (default) | ~200 bytes | Simple sensor data |
| 512 | ~460 bytes | GPS + telemetry |
| 1024 | ~970 bytes | Rich JSON payloads |

**Formula:** Available payload ≈ Buffer Size - Topic Length - ~10 bytes MQTT overhead

## Debug Helper

Drop this into your project to catch buffer issues early:

```cpp
bool safePublish(PubSubClient &client, const char* topic, const char* payload) {
    int totalSize = strlen(topic) + strlen(payload) + 10; // MQTT overhead
    int bufferSize = client.getBufferSize();
    
    if (totalSize > bufferSize) {
        Serial.printf("[MQTT] ERROR: Packet %d bytes > buffer %d bytes!\n", 
                      totalSize, bufferSize);
        Serial.printf("[MQTT] Topic: %s (%d bytes)\n", topic, strlen(topic));
        Serial.printf("[MQTT] Payload: %d bytes\n", strlen(payload));
        Serial.printf("[MQTT] Fix: mqtt_client.setBufferSize(%d);\n", totalSize + 50);
        return false;
    }
    
    bool sent = client.publish(topic, payload);
    if (!sent) {
        Serial.printf("[MQTT] Publish failed (connected=%d)\n", client.connected());
    }
    return sent;
}
```

## Affected Libraries

| Library | Default Buffer | Silent Fail? | Fix |
|---------|---------------|-------------|-----|
| [PubSubClient](https://github.com/knolleary/pubsubclient) | 256 bytes | ✅ Yes | `setBufferSize()` |
| [AsyncMqttClient](https://github.com/marvinroger/async-mqtt-client) | No fixed limit | ❌ No | N/A |
| [arduino-mqtt](https://github.com/256dpi/arduino-mqtt) | 128 bytes | ✅ Yes | Constructor param |

## Real-World Impact

This bug was discovered in a **production fleet tracking system** with ESP32 devices mounted in commercial vehicles. GPS location data silently failed to reach the server for hours while telemetry and heartbeat messages worked perfectly — because the location payload was just 12 bytes over the default buffer limit.

The Serial Monitor showed `Published` for every single message. The broker logs told a different story.

## Key Takeaways

1. **Always call `setBufferSize()`** — Don't rely on the default 256 bytes
2. **Always check `publish()` return value** — It's a boolean for a reason
3. **Calculate your packet size** — Topic + Payload + ~10 bytes overhead
4. **Test at the broker level** — Serial Monitor lies; broker traces don't

## Contributing

Found another MQTT/ESP32 gotcha? Open a PR! Let's save other developers from silent failures.

## License

MIT — Use freely, save debugging hours.

---

*Discovered during production debugging of [TulipFleet](https://tulipfleet.com) — a fleet management platform using ESP32 IoT devices.*

*Built by [@mightyforever74](https://github.com/mightyforever74)*
