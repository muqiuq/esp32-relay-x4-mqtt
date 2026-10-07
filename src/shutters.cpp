#include "shutters.h"
#include <utility>
#include "board.h"
#include "config.h"
#include "display.h"
#include "log.h"
#include "relays.h"

namespace {

const char* const STATE_NAMES[] = {"unknown", "opening", "closing", "open", "closed", "stopped"};

struct Shutter {
  ShutterState state = SH_UNKNOWN;
  ShutterDir lastDir = DIR_NONE;
  uint32_t moveStart = 0;
  uint32_t lastCmdAt = 0;
  bool hasCmd = false;
  char lastAction[12] = "";
};

Shutter shutters[SHUTTER_COUNT];
uint32_t changeSeq = 0;

void setState(uint8_t idx, ShutterState st) {
  Shutter& s = shutters[idx];
  if (s.state == st) return;
  LOGI("SHUT", "S%u %s -> %s", idx + 1, STATE_NAMES[s.state], STATE_NAMES[st]);
  s.state = st;
  changeSeq++;
}

void noteCommand(uint8_t idx, const char* action) {
  Shutter& s = shutters[idx];
  s.lastCmdAt = millis();
  s.hasCmd = true;
  strlcpy(s.lastAction, action, sizeof(s.lastAction));
  changeSeq++;
}

}  // namespace

uint8_t shutterRelay(uint8_t idx, ShutterDir dir) {
  uint8_t up = idx * 2, down = idx * 2 + 1;
  if (cfg.shutterInvert[idx]) std::swap(up, down);
  return dir == DIR_DOWN ? down : up;
}

bool shutterMove(uint8_t idx, ShutterDir dir, uint32_t pressMs, bool isLong, String& msg) {
  if (idx >= SHUTTER_COUNT || dir == DIR_NONE) {
    msg = "invalid shutter";
    return false;
  }
  uint8_t ch = shutterRelay(idx, dir);
  if (!relayPress(ch, pressMs)) {
    msg = "relay command rejected";
    return false;
  }
  Shutter& s = shutters[idx];
  s.lastDir = dir;
  const char* dirName = dir == DIR_UP ? "up" : "down";
  noteCommand(idx, isLong ? (dir == DIR_UP ? "up_long" : "down_long") : (dir == DIR_UP ? "up_short" : "down_short"));

  if (isLong) {
    s.moveStart = millis();
    if (cfg.travelSec == 0)
      setState(idx, dir == DIR_UP ? SH_OPEN : SH_CLOSED);
    else
      setState(idx, dir == DIR_UP ? SH_OPENING : SH_CLOSING);
  } else {
    // A short press either stops a moving shutter or moves it one step.
    // At the end position in the same direction nothing happens.
    bool atEnd = (s.state == SH_OPEN && dir == DIR_UP) || (s.state == SH_CLOSED && dir == DIR_DOWN);
    if (!atEnd) setState(idx, SH_STOPPED);
  }
  msg = cfgShutterName(idx) + ": " + dirName + (isLong ? " long" : " short") + " (R" + (ch + 1) + ", " + pressMs +
        " ms)";
  displayEvent(String(idx + 1) + (dir == DIR_UP ? "uP " : "dn ") + (isLong ? "Lo" : "Sh"));  // "1uP Lo"
  return true;
}

bool shutterStop(uint8_t idx, String& msg) {
  if (idx >= SHUTTER_COUNT) {
    msg = "invalid shutter";
    return false;
  }
  Shutter& s = shutters[idx];
  ShutterDir dir = DIR_UP;  // fallback if direction is unknown
  if (s.lastDir != DIR_NONE) dir = (cfg.stopMode == 1) ? (ShutterDir)(-s.lastDir) : s.lastDir;
  uint8_t ch = shutterRelay(idx, dir);
  if (!relayPress(ch, cfg.shortMs)) {
    msg = "relay command rejected";
    return false;
  }
  noteCommand(idx, "stop");
  setState(idx, SH_STOPPED);
  msg = cfgShutterName(idx) + ": stop (R" + (ch + 1) + " short)";
  displayEvent(String(idx + 1) + " StoP");
  return true;
}

void shuttersLoop() {
  uint32_t now = millis();
  for (uint8_t i = 0; i < SHUTTER_COUNT; i++) {
    Shutter& s = shutters[i];
    if ((s.state == SH_OPENING || s.state == SH_CLOSING) && now - s.moveStart >= cfg.travelSec * 1000UL) {
      setState(i, s.state == SH_OPENING ? SH_OPEN : SH_CLOSED);
    }
  }
}

void shuttersToJson(JsonArray arr) {
  uint32_t now = millis();
  for (uint8_t i = 0; i < SHUTTER_COUNT; i++) {
    const Shutter& s = shutters[i];
    JsonObject o = arr.add<JsonObject>();
    o["id"] = i + 1;
    o["name"] = cfgShutterName(i);
    o["state"] = STATE_NAMES[s.state];
    o["last"] = s.hasCmd ? s.lastAction : "";
    if (s.hasCmd)
      o["ago"] = (now - s.lastCmdAt) / 1000;
    else
      o["ago"] = nullptr;
    o["upRelay"] = shutterRelay(i, DIR_UP) + 1;
    o["downRelay"] = shutterRelay(i, DIR_DOWN) + 1;
  }
}

uint32_t shuttersChangeSeq() { return changeSeq; }

ShutterState shutterState(uint8_t idx) { return idx < SHUTTER_COUNT ? shutters[idx].state : SH_UNKNOWN; }
