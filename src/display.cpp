#include "display.h"
#include <WiFi.h>
#include <time.h>
#include "board.h"
#include "config.h"
#include "health.h"
#include "log.h"
#include "net.h"
#include "relays.h"
#include "shutters.h"
#include "system.h"
#include "tm1637.h"
#include "web.h"

#if DISPLAY_CLK_PIN >= 0

namespace {

struct Frame {
  uint8_t raw[TM1637::DIGITS];
  uint8_t brightness;
  bool on;
};

TM1637 chip;
TaskHandle_t task = nullptr;
portMUX_TYPE frameMux = portMUX_INITIALIZER_UNLOCKED;
Frame pending;
volatile bool ackOk = true;
volatile bool written = false;

const uint8_t ORDER_STANDARD[TM1637::DIGITS] = {0, 1, 2, 3, 4, 5};
// Common 6-digit TM1637 boards wire the grids as 3-2-1-6-5-4 (mapping is its own inverse)
const uint8_t ORDER_MODULE[TM1637::DIGITS] = {2, 1, 0, 5, 4, 3};

Frame lastSent;
bool haveSent = false;
uint32_t lastRender = 0;
String currentText;
bool lastAck = true;
bool wasSta = false;

String msg;
uint32_t msgStart = 0;
uint32_t msgDuration = 0;

enum Item : uint8_t {
  IT_AP1, IT_AP2, IT_AP3, IT_AP4, IT_CLOCK, IT_SH1, IT_SH2, IT_IP1, IT_IP2, IT_RSSI, IT_UPTIME, IT_REBOOT,
  IT_COUNT,  // items above take part in the rotation
  IT_ERROR,
  IT_NONE
};
uint8_t curItem = IT_NONE;
uint8_t rotIdx = IT_COUNT - 1;
uint32_t itemStart = 0;
bool lastWasError = false;
uint32_t errCounter = 0;
uint8_t curErr = 0;

constexpr uint32_t RENDER_MS = 100;
constexpr uint32_t REFRESH_MS = 5000;  // rewrite periodically in case the chip lost its state

// ---------------------------------------------------------------------------
// Output task: the bit-banged bus is slow, so it never runs in loop()
// ---------------------------------------------------------------------------
bool swapped = false;  // CLK/DIO wires detected as swapped

void setPins(bool swap) {
  swapped = swap;
  chip.begin(swap ? DISPLAY_DIO_PIN : DISPLAY_CLK_PIN, swap ? DISPLAY_CLK_PIN : DISPLAY_DIO_PIN, DISPLAY_BIT_DELAY_US);
}

// Called once when the display stops answering. Tries swapped CLK/DIO, then
// checks the idle line levels: a powered module pulls both lines high, even
// against the ESP32's internal pull-downs. Returns true if the display answers.
bool diagnose(const Frame& f) {
  setPins(!swapped);
  if (chip.write(f.raw, f.brightness, f.on)) {
    LOGW("DISP", "Display answers with CLK/DIO swapped: using GPIO%d as CLK, GPIO%d as DIO",
         swapped ? DISPLAY_DIO_PIN : DISPLAY_CLK_PIN, swapped ? DISPLAY_CLK_PIN : DISPLAY_DIO_PIN);
    return true;
  }
  setPins(!swapped);

  pinMode(DISPLAY_CLK_PIN, INPUT_PULLDOWN);
  pinMode(DISPLAY_DIO_PIN, INPUT_PULLDOWN);
  delay(5);
  bool clkHigh = digitalRead(DISPLAY_CLK_PIN) == HIGH;
  bool dioHigh = digitalRead(DISPLAY_DIO_PIN) == HIGH;
  setPins(swapped);

  uint32_t clkRise = TM1637::riseTimeUs(DISPLAY_CLK_PIN);
  uint32_t dioRise = TM1637::riseTimeUs(DISPLAY_DIO_PIN);

  LOGW("DISP", "Line check: GPIO%d (CLK) %s, rise %u us; GPIO%d (DIO) %s, rise %u us", DISPLAY_CLK_PIN,
       clkHigh ? "pulled up by module" : "NO pull-up", (unsigned)clkRise, DISPLAY_DIO_PIN,
       dioHigh ? "pulled up by module" : "NO pull-up", (unsigned)dioRise);
  if (!clkHigh && !dioHigh)
    LOGW("DISP", "-> module not powered or not connected: check VCC (3.3 V), GND and that the wires are on GPIO%d/%d",
         DISPLAY_CLK_PIN, DISPLAY_DIO_PIN);
  else if (!clkHigh || !dioHigh)
    LOGW("DISP", "-> GPIO%d is not connected to the module: check that wire and pin",
         clkHigh ? DISPLAY_DIO_PIN : DISPLAY_CLK_PIN);
  else
    LOGW("DISP", "-> wiring looks fine but no answer: check the module is a TM1637 and its power (try 5 V VCC)");
  return false;
}

void displayTask(void*) {
  bool diagnosed = false;
  for (;;) {
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(REFRESH_MS));
    Frame f;
    portENTER_CRITICAL(&frameMux);
    f = pending;
    portEXIT_CRITICAL(&frameMux);
    bool ok = chip.write(f.raw, f.brightness, f.on);
    if (!ok && !diagnosed) {
      diagnosed = true;
      ok = diagnose(f);
    }
    if (ok) diagnosed = false;  // diagnose again after the next failure
    ackOk = ok;
    written = true;
  }
}

// Segments: bit0=a (top) .. bit6=g (middle), bit7=decimal point
uint8_t segmentsFor(char c) {
  switch (c) {
    case '0': case 'O': case 'D': return 0x3F;
    case '1': return 0x06;
    case '2': case 'Z': case 'z': return 0x5B;
    case '3': return 0x4F;
    case '4': return 0x66;
    case '5': case 'S': case 's': return 0x6D;
    case '6': return 0x7D;
    case '7': return 0x07;
    case '8': return 0x7F;
    case '9': case 'g': return 0x6F;
    case 'A': case 'a': return 0x77;
    case 'B': case 'b': return 0x7C;
    case 'C': return 0x39;
    case 'c': return 0x58;
    case 'd': return 0x5E;
    case 'E': case 'e': return 0x79;
    case 'F': case 'f': return 0x71;
    case 'G': return 0x3D;
    case 'H': case 'K': case 'k': case 'X': case 'x': return 0x76;
    case 'h': return 0x74;
    case 'I': case 'l': return 0x30;
    case 'i': return 0x10;
    case 'J': case 'j': return 0x1E;
    case 'L': return 0x38;
    case 'M': case 'm': return 0x37;
    case 'N': case 'n': return 0x54;
    case 'o': return 0x5C;
    case 'P': case 'p': return 0x73;
    case 'Q': case 'q': return 0x67;
    case 'R': case 'r': return 0x50;
    case 'T': case 't': return 0x78;
    case 'U': case 'W': return 0x3E;
    case 'u': case 'v': case 'V': case 'w': return 0x1C;
    case 'Y': case 'y': return 0x6E;
    case '-': return 0x40;
    case '_': return 0x08;
    case '=': return 0x48;
    case '^': return 0x01;
    case '\'': return 0x02;
    case '"': return 0x22;
    case '[': return 0x39;
    case ']': return 0x0F;
    case '?': return 0x53;
    case '*': return 0x63;  // degree sign
    default: return 0x00;
  }
}

// Converts text to 6 digits; '.' lights the decimal point of the previous digit
void encode(const char* text, uint8_t out[TM1637::DIGITS]) {
  memset(out, 0, TM1637::DIGITS);
  uint8_t pos = 0;
  for (const char* p = text; *p; p++) {
    if (*p == '.') {
      if (pos > 0 && !(out[pos - 1] & 0x80))
        out[pos - 1] |= 0x80;
      else if (pos < TM1637::DIGITS)
        out[pos++] = 0x80;
      continue;
    }
    if (pos >= TM1637::DIGITS) break;
    out[pos++] = segmentsFor(*p);
  }
}

void submit(const String& text, bool on) {
  uint8_t logical[TM1637::DIGITS];
  encode(text.c_str(), logical);
  const uint8_t* order = cfg.dispOrder ? ORDER_MODULE : ORDER_STANDARD;
  Frame f;
  memset(&f, 0, sizeof(f));
  for (uint8_t i = 0; i < TM1637::DIGITS; i++) f.raw[order[i]] = logical[i];
  f.brightness = cfg.dispBrightness;
  f.on = on;
  if (haveSent && memcmp(&f, &lastSent, sizeof(f)) == 0) return;
  lastSent = f;
  haveSent = true;
  portENTER_CRITICAL(&frameMux);
  pending = f;
  portEXIT_CRITICAL(&frameMux);
  if (task) xTaskNotifyGive(task);
}

// ---------------------------------------------------------------------------
// Page rendering
// ---------------------------------------------------------------------------
String fmt(const char* f, ...) __attribute__((format(printf, 1, 2)));
String fmt(const char* f, ...) {
  char buf[24];
  va_list ap;
  va_start(ap, f);
  vsnprintf(buf, sizeof(buf), f, ap);
  va_end(ap);
  return buf;
}

bool timeSynced() { return time(nullptr) >= 1700000000; }

String ipFrame(const IPAddress& ip, bool second) {
  return second ? fmt("%3u.%3u", ip[2], ip[3]) : fmt("%3u.%3u.", ip[0], ip[1]);
}

const char* shutterText(ShutterState s) {
  switch (s) {
    case SH_OPEN:
      return "oPEn";
    case SH_CLOSED:
      return "cLoS";
    case SH_OPENING:
      return "uP";
    case SH_CLOSING:
      return "dn";
    case SH_STOPPED:
      return "StoP";
    default:
      return "----";
  }
}

String clockText() {
  if (!timeSynced()) return "--.--.--";
  time_t t = time(nullptr);
  struct tm tm;
  localtime_r(&t, &tm);
  char buf[12];
  strftime(buf, sizeof(buf), "%H.%M.%S", &tm);
  return buf;
}

String uptimeText() {
  uint64_t s = systemUptimeSec();
  unsigned days = s / 86400, hours = (s / 3600) % 24, mins = (s / 60) % 60;
  if (days == 0) return fmt("u %2u.%02u", hours, mins);
  if (days < 100) return fmt("u%2ud%02u", days, hours);
  return fmt("u%4ud", days);
}

String rebootText() {
  int32_t left = systemRebootCountdownSec();
  if (left < 0) return "rb --";
  if (left < 6000) return fmt("rb%2u.%02u", (unsigned)(left / 60), (unsigned)(left % 60));
  return fmt("rb%3uh", (unsigned)(left / 3600));
}

String apNameText() {
  if (!cfg.hostname[0]) return deviceMacHex() + 6;  // AP SSID is relay4-XXXXXX
  return cfgHostname().substring(0, TM1637::DIGITS);
}

bool applicable(uint8_t it) {
  uint8_t mode = cfg.dispMode;  // 1 rotation, 2 clock, 3 shutters
  switch (it) {
    case IT_AP1:
    case IT_AP2:
    case IT_AP3:
    case IT_AP4:
      return netApActive();
    case IT_CLOCK:
      return mode == 2 || (mode == 1 && timeSynced());
    case IT_SH1:
    case IT_SH2:
      return mode == 1 || mode == 3;
    case IT_IP1:
    case IT_IP2:
    case IT_RSSI:
      return mode == 1 && netStaConnected();
    case IT_UPTIME:
      return mode == 1;
    case IT_REBOOT:
      return systemRebootCountdownSec() >= 0;
  }
  return false;
}

String renderItem(uint8_t it) {
  switch (it) {
    case IT_AP1:
      return cfg.wifiSsid[0] ? "AP on" : "SEtuP";
    case IT_AP2:
      return apNameText();
    case IT_AP3:
      return ipFrame(WiFi.softAPIP(), false);
    case IT_AP4:
      return ipFrame(WiFi.softAPIP(), true);
    case IT_CLOCK:
      return clockText();
    case IT_SH1:
    case IT_SH2: {
      uint8_t idx = it - IT_SH1;
      return String(idx + 1) + " " + shutterText(shutterState(idx));
    }
    case IT_IP1:
      return ipFrame(WiFi.localIP(), false);
    case IT_IP2:
      return ipFrame(WiFi.localIP(), true);
    case IT_RSSI: {
      int r = WiFi.RSSI();
      return r <= -100 ? fmt("%ddb", r) : fmt("%d db", r);
    }
    case IT_UPTIME:
      return uptimeText();
    case IT_REBOOT:
      return rebootText();
    case IT_ERROR:
      return fmt("Err %02u", curErr);
  }
  return String();
}

void advance() {
  uint8_t codes[16];
  size_t n = healthCollect(codes, sizeof(codes));
  // Errors are shown after every regular page
  if (n && !lastWasError) {
    curErr = codes[errCounter++ % n];
    curItem = IT_ERROR;
    lastWasError = true;
    return;
  }
  lastWasError = false;
  for (uint8_t k = 1; k <= IT_COUNT; k++) {
    uint8_t c = (rotIdx + k) % IT_COUNT;
    if (applicable(c)) {
      rotIdx = c;
      curItem = c;
      return;
    }
  }
  if (n) {  // only errors to show
    curErr = codes[errCounter++ % n];
    curItem = IT_ERROR;
    lastWasError = true;
    return;
  }
  curItem = IT_NONE;
}

String rotation(uint32_t now) {
  uint32_t pageMs = cfg.dispPageSec * 1000UL;
  bool stale = curItem == IT_NONE || (curItem < IT_COUNT && !applicable(curItem));
  if (stale || now - itemStart >= pageMs) {
    advance();
    itemStart = now;
  }
  return renderItem(curItem);
}

String compose(uint32_t now) {
  uint32_t held = systemButtonHeldMs();
  if (held >= 2000) {
    uint32_t left = held >= FACTORY_RESET_HOLD_MS ? 0 : (FACTORY_RESET_HOLD_MS - held + 999) / 1000;
    return fmt("FAc %2u", (unsigned)left);
  }
  if (systemRestartPending()) return "rESEt";
  int32_t kb = webOtaProgressKB();
  if (kb >= 0) return fmt("UP%4d", (int)kb);

  if (msgDuration) {
    uint32_t elapsed = now - msgStart;
    if (elapsed < msgDuration) {
      int frames = 1;
      for (size_t i = 0; i < msg.length(); i++) frames += msg[i] == '|';
      uint32_t frameMs = msgDuration / frames ? msgDuration / frames : 1;
      int idx = min((int)(elapsed / frameMs), frames - 1);
      int start = 0;
      for (int f = 0; f < idx; f++) start = msg.indexOf('|', start) + 1;
      int end = msg.indexOf('|', start);
      return end < 0 ? msg.substring(start) : msg.substring(start, end);
    }
    msgDuration = 0;
  }

  String on;
  for (uint8_t i = 0; i < RELAY_COUNT; i++) {
    if (relayIsOn(i)) on += (char)('1' + i);
  }
  if (on.length()) return (on.length() <= 3 ? "on " : "on") + on;

  return rotation(now);
}

}  // namespace

void displayInit() {
  setPins(false);
  xTaskCreatePinnedToCore(displayTask, "display", 4096, nullptr, 1, &task, 0);  // room for log output
  if (cfg.dispMode) {
    submit("boot", true);
    displayShow("F " FW_VERSION, 2000);
  } else {
    submit("", false);
  }
  LOGI("DISP", "TM1637 on CLK GPIO%d, DIO GPIO%d", DISPLAY_CLK_PIN, DISPLAY_DIO_PIN);
}

void displayLoop() {
  uint32_t now = millis();

  bool ack = ackOk;
  if (written && ack != lastAck) {
    lastAck = ack;
    if (ack)
      LOGI("DISP", "Display responding");
    else
      LOGW("DISP", "Display not responding - check wiring (CLK GPIO%d, DIO GPIO%d, VCC 3.3 V)", DISPLAY_CLK_PIN,
           DISPLAY_DIO_PIN);
  }

  bool sta = netStaConnected();
  if (sta && !wasSta) {
    IPAddress ip = WiFi.localIP();
    displayShow("Conn|" + ipFrame(ip, false) + "|" + ipFrame(ip, true), 4500);
  }
  wasSta = sta;

  if (now - lastRender < RENDER_MS) return;
  lastRender = now;
  bool on = cfg.dispMode != 0;
  currentText = on ? compose(now) : String();
  submit(currentText, on);
}

void displayShow(const String& text, uint32_t ms) {
  msg = text;
  msgStart = millis();
  msgDuration = ms ? ms : 1;
}

void displayEvent(const String& text) {
  if (cfg.dispEvents) displayShow(text, 2500);
}

bool displayPresent() { return ackOk; }
String displayCurrentText() { return currentText; }

#else  // display disabled

void displayInit() {}
void displayLoop() {}
void displayShow(const String&, uint32_t) {}
void displayEvent(const String&) {}
bool displayPresent() { return false; }
String displayCurrentText() { return String(); }

#endif
