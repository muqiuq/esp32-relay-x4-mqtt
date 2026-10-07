#include "relays.h"
#include <esp_task_wdt.h>
#include <atomic>
#include "log.h"

namespace {

enum RelayOp : uint8_t { OP_PRESS, OP_ON, OP_OFF, OP_ALL_OFF };

struct RelayCmd {
  RelayOp op;
  uint8_t ch;
  uint32_t ms;
};

static constexpr uint8_t MAX_PENDING = 4;  // presses accepted per relay (enforced by the sender)
static constexpr uint8_t QUEUE_DEPTH = 8;  // internal ring, larger so it can never overflow
static constexpr uint32_t HOLD = 0;        // queue entry meaning "on until off/maxOn"

struct Channel {
  bool on = false;
  bool hold = false;
  uint32_t onAt = 0;
  uint32_t dur = 0;
  uint32_t offAt = 0;
  bool released = false;  // offAt valid
  uint32_t queue[QUEUE_DEPTH];
  uint8_t qHead = 0;
  uint8_t qCount = 0;
};

Channel chans[RELAY_COUNT];
QueueHandle_t cmdQueue = nullptr;
portMUX_TYPE settingsMux = portMUX_INITIALIZER_UNLOCKED;
RelaySettings settings;
RelaySettings active;  // copy owned by the relay task

volatile bool stateOn[RELAY_COUNT];
volatile uint8_t statePending[RELAY_COUNT];
volatile uint32_t pressCount[RELAY_COUNT];
volatile uint32_t changeSeq = 0;
volatile bool locked = false;
// Press commands sent but not yet taken from the FreeRTOS queue (sender ++, task --)
std::atomic<uint8_t> inFlight[RELAY_COUNT];

inline uint8_t partner(uint8_t ch) { return ch ^ 1; }

void writePin(uint8_t ch, bool on) {
  digitalWrite(RELAY_PINS[ch], (on == active.activeHigh) ? HIGH : LOW);
}

void syncPending(uint8_t ch) { statePending[ch] = chans[ch].qCount; }

void setOn(uint8_t ch, uint32_t dur) {
  Channel& c = chans[ch];
  c.on = true;
  c.hold = (dur == HOLD);
  c.dur = dur;
  c.onAt = millis();
  writePin(ch, true);
  stateOn[ch] = true;
  pressCount[ch] = pressCount[ch] + 1;
  changeSeq = changeSeq + 1;
  if (c.hold)
    LOGI("RELAY", "R%u ON (hold)", ch + 1);
  else
    LOGI("RELAY", "R%u ON (%u ms)", ch + 1, (unsigned)dur);
}

void setOff(uint8_t ch, const char* why) {
  Channel& c = chans[ch];
  if (!c.on) return;
  c.on = false;
  c.offAt = millis();
  c.released = true;
  writePin(ch, false);
  stateOn[ch] = false;
  changeSeq = changeSeq + 1;
  LOGI("RELAY", "R%u OFF after %u ms%s%s", ch + 1, (unsigned)(c.offAt - c.onAt), why ? " - " : "", why ? why : "");
}

void clearQueue(uint8_t ch) {
  if (chans[ch].qCount) LOGD("RELAY", "R%u dropped %u queued press(es)", ch + 1, chans[ch].qCount);
  chans[ch].qCount = 0;
  syncPending(ch);
}

bool push(uint8_t ch, uint32_t dur) {
  Channel& c = chans[ch];
  if (c.qCount >= QUEUE_DEPTH) {
    LOGW("RELAY", "R%u queue full, press dropped", ch + 1);
    return false;
  }
  c.queue[(c.qHead + c.qCount) % QUEUE_DEPTH] = dur;
  c.qCount++;
  syncPending(ch);
  return true;
}

uint32_t pop(uint8_t ch) {
  Channel& c = chans[ch];
  uint32_t d = c.queue[c.qHead];
  c.qHead = (c.qHead + 1) % QUEUE_DEPTH;
  c.qCount--;
  syncPending(ch);
  return d;
}

void preemptPartner(uint8_t ch) {
  if (!active.interlock) return;
  uint8_t p = partner(ch);
  clearQueue(p);
  setOff(p, "interlock");
}

// Signed comparison: offAt may be a few ms *after* `now` (set later in the same tick)
bool elapsed(uint32_t now, uint32_t since, uint32_t duration) {
  return (int32_t)(now - since) >= (int32_t)duration;
}

bool gapElapsed(uint8_t ch, uint32_t now) {
  const Channel& c = chans[ch];
  return !c.released || elapsed(now, c.offAt, active.gapMs);
}

bool canStart(uint8_t ch, uint32_t now) {
  if (!gapElapsed(ch, now)) return false;
  if (active.interlock) {
    uint8_t p = partner(ch);
    if (chans[p].on || !gapElapsed(p, now)) return false;
  }
  return true;
}

void handle(const RelayCmd& cmd) {
  switch (cmd.op) {
    case OP_PRESS: {
      inFlight[cmd.ch]--;
      preemptPartner(cmd.ch);
      // A press replaces a hold (active or still queued): release first, then press
      Channel& c = chans[cmd.ch];
      bool holdQueued = false;
      for (uint8_t i = 0; i < c.qCount; i++) holdQueued |= c.queue[(c.qHead + i) % QUEUE_DEPTH] == HOLD;
      if (holdQueued) clearQueue(cmd.ch);
      if (c.on && c.hold) setOff(cmd.ch, "replaced by press");
      push(cmd.ch, cmd.ms);
      break;
    }
    case OP_ON: {
      preemptPartner(cmd.ch);
      clearQueue(cmd.ch);
      Channel& c = chans[cmd.ch];
      if (c.on) {
        c.hold = true;
        c.onAt = millis();
        LOGI("RELAY", "R%u held ON", cmd.ch + 1);
      } else {
        push(cmd.ch, HOLD);
      }
      break;
    }
    case OP_OFF:
      clearQueue(cmd.ch);
      setOff(cmd.ch, nullptr);
      break;
    case OP_ALL_OFF:
      for (uint8_t i = 0; i < RELAY_COUNT; i++) {
        clearQueue(i);
        setOff(i, "all off");
      }
      break;
  }
}

void tick() {
  uint32_t now = millis();
  for (uint8_t i = 0; i < RELAY_COUNT; i++) {
    Channel& c = chans[i];
    if (c.on) {
      uint32_t limit = c.hold ? active.maxOnMs : c.dur;
      if (limit && elapsed(now, c.onAt, limit)) setOff(i, c.hold ? "max ON time reached" : nullptr);
    }
    if (!c.on && c.qCount && canStart(i, now)) setOn(i, pop(i));
  }
}

void relayTask(void*) {
  esp_task_wdt_add(nullptr);
  for (;;) {
    esp_task_wdt_reset();
    // Pick up settings changes
    portENTER_CRITICAL(&settingsMux);
    RelaySettings s = settings;
    portEXIT_CRITICAL(&settingsMux);
    bool levelChanged = s.activeHigh != active.activeHigh;
    active = s;
    if (levelChanged) {
      for (uint8_t i = 0; i < RELAY_COUNT; i++) writePin(i, chans[i].on);
    }

    RelayCmd cmd;
    if (xQueueReceive(cmdQueue, &cmd, pdMS_TO_TICKS(5)) == pdTRUE) {
      do {
        handle(cmd);
      } while (xQueueReceive(cmdQueue, &cmd, 0) == pdTRUE);
    }
    tick();
  }
}

bool send(RelayOp op, uint8_t ch, uint32_t ms) {
  if (!cmdQueue) return false;
  if (locked && op != OP_ALL_OFF && op != OP_OFF) return false;
  if (op != OP_ALL_OFF && ch >= RELAY_COUNT) return false;
  if (op == OP_PRESS) {
    if (statePending[ch] + inFlight[ch] >= MAX_PENDING) {
      LOGW("RELAY", "R%u: %u presses already pending, rejected", ch + 1, MAX_PENDING);
      return false;
    }
    inFlight[ch]++;
  }
  RelayCmd cmd{op, ch, ms};
  if (xQueueSend(cmdQueue, &cmd, pdMS_TO_TICKS(50)) != pdTRUE) {
    if (op == OP_PRESS) inFlight[ch]--;
    LOGE("RELAY", "Command queue full");
    return false;
  }
  return true;
}

}  // namespace

void relaysInit(const RelaySettings& s) {
  settings = s;
  active = s;
  for (uint8_t i = 0; i < RELAY_COUNT; i++) {
    // Set the level before switching to output to avoid a glitch
    writePin(i, false);
    pinMode(RELAY_PINS[i], OUTPUT);
    writePin(i, false);
    stateOn[i] = false;
    statePending[i] = 0;
    pressCount[i] = 0;
    inFlight[i] = 0;
  }
  cmdQueue = xQueueCreate(16, sizeof(RelayCmd));
  // Priority above loop() (1), pinned to the application core
  xTaskCreatePinnedToCore(relayTask, "relays", 4096, nullptr, 5, nullptr, 1);
  LOGI("RELAY", "Relays on GPIO %u/%u/%u/%u, active %s, interlock %s", RELAY_PINS[0], RELAY_PINS[1], RELAY_PINS[2],
       RELAY_PINS[3], s.activeHigh ? "HIGH" : "LOW", s.interlock ? "on" : "off");
}

void relaysApplySettings(const RelaySettings& s) {
  portENTER_CRITICAL(&settingsMux);
  settings = s;
  portEXIT_CRITICAL(&settingsMux);
}

bool relayPress(uint8_t ch, uint32_t ms) { return ms > 0 && send(OP_PRESS, ch, ms); }
bool relayOn(uint8_t ch) { return send(OP_ON, ch, 0); }
bool relayOff(uint8_t ch) { return send(OP_OFF, ch, 0); }
bool relaysAllOff() { return send(OP_ALL_OFF, 0, 0); }

void relaysSetLocked(bool lock) {
  locked = lock;
  if (lock) relaysAllOff();
}
bool relaysLocked() { return locked; }

bool relayIsOn(uint8_t ch) { return ch < RELAY_COUNT && stateOn[ch]; }
uint32_t relayPressCount(uint8_t ch) { return ch < RELAY_COUNT ? pressCount[ch] : 0; }

bool relaysBusy() {
  if (cmdQueue && uxQueueMessagesWaiting(cmdQueue)) return true;
  for (uint8_t i = 0; i < RELAY_COUNT; i++) {
    if (stateOn[i] || statePending[i]) return true;
  }
  return false;
}

uint32_t relaysChangeSeq() { return changeSeq; }
