#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

// Roller shutter logic on top of the relay driver. Each shutter uses two
// relays that emulate the UP and DOWN buttons of a shutter controller.
// The position is not measured; the state is estimated from the commands sent
// (wall buttons pressed by hand are not seen).

enum ShutterDir : int8_t { DIR_NONE = 0, DIR_UP = 1, DIR_DOWN = -1 };
enum ShutterState : uint8_t { SH_UNKNOWN, SH_OPENING, SH_CLOSING, SH_OPEN, SH_CLOSED, SH_STOPPED };

// Presses the UP or DOWN button for pressMs. isLong marks a full-travel press.
bool shutterMove(uint8_t idx, ShutterDir dir, uint32_t pressMs, bool isLong, String& msg);
bool shutterStop(uint8_t idx, String& msg);
uint8_t shutterRelay(uint8_t idx, ShutterDir dir);  // 0-based relay channel
ShutterState shutterState(uint8_t idx);

void shuttersLoop();
void shuttersToJson(JsonArray arr);
uint32_t shuttersChangeSeq();
