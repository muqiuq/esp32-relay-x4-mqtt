#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

// Builds the status JSON (MQTT <base>/status and /api/status).
void statusBuild(JsonDocument& doc, const char* reason);
// Publishes the status as soon as possible (e.g. "request", "connect")
void statusRequest(const char* reason);
// Periodic publishing every statusInterval seconds plus on changes
void statusLoop();
