#pragma once
#include <Arduino.h>
#include "board.h"

// Relay driver running in its own high-priority FreeRTOS task, so press
// durations stay exact even while the network code blocks (MQTT connect,
// DNS, HTTP upload, ...).
//
// Behaviour per relay (channel 0..3):
//  - press(ms): queued; consecutive presses on the same relay are executed one
//    after another with `gapMs` release time in between.
//  - on():      held until off() or `maxOnMs` (0 = unlimited).
//  - off():     releases immediately and drops queued presses.
// With interlock enabled, relays 0/1 and 2/3 are pairs: any press/on on one
// relay releases its partner and drops the partner's queue, then waits `gapMs`.

struct RelaySettings {
  bool activeHigh;
  bool interlock;
  uint32_t gapMs;
  uint32_t maxOnMs;
};

void relaysInit(const RelaySettings& s);
void relaysApplySettings(const RelaySettings& s);

bool relayPress(uint8_t ch, uint32_t ms);
bool relayOn(uint8_t ch);
bool relayOff(uint8_t ch);
bool relaysAllOff();
// Rejects all commands and releases all relays (used during OTA)
void relaysSetLocked(bool locked);
bool relaysLocked();

bool relayIsOn(uint8_t ch);
uint32_t relayPressCount(uint8_t ch);
bool relaysBusy();          // any relay on or presses pending
uint32_t relaysChangeSeq();  // incremented on every output change
