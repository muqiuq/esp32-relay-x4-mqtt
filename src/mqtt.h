#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

// MQTT client with reconnect backoff, LWT and command handling.
// Topics below <base> (default esp32/relay4-<MAC>):
//   availability  "online" / "offline" (retained, LWT)
//   status        status JSON (retained by default)
//   cmd           commands (text or JSON)
//   result        command result JSON
//   log           log lines (optional)
void mqttInit();
void mqttLoop();
bool mqttEnabled();
bool mqttConnected();
bool mqttPublish(const char* subtopic, const char* payload, size_t len, bool retain);
bool mqttPublishJson(const char* subtopic, const JsonDocument& doc, bool retain);
uint32_t mqttReconnects();
int mqttLastState();  // PubSubClient state (MQTT_CONNECT_BAD_CREDENTIALS, ...)
uint32_t mqttDownSince();  // millis() when the connection was lost (or boot)
// Graceful disconnect before a restart
void mqttShutdown();
