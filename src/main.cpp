// ESP32 Relay X4 - MQTT roller shutter button emulator
//
// Developed by Philipp Albrecht <philipp@uisa.ch> using Claude Opus 5.5, 2026-10-07
// https://uisa.ch
//
// Module overview:
//   relays    timed relay outputs in a dedicated high-priority task (interlock, queue)
//   shutters  UP/DOWN/STOP logic for two shutters (2 relays each)
//   commands  text/JSON command parser shared by MQTT, web and serial
//   net       WiFi station + captive portal access point
//   mqtt      broker connection, LWT, command topic, status/log publishing
//   web       dashboard, config form, OTA, HTTP API
//   system    watchdogs, restart handling, LED, BOOT button
//   display   TM1637 status display, health/error codes (health)
//   config    persistent settings (NVS)
//   log       non-blocking serial log + ring buffer

#include <Arduino.h>
#include "board.h"
#include "commands.h"
#include "config.h"
#include "display.h"
#include "log.h"
#include "mqtt.h"
#include "net.h"
#include "relays.h"
#include "shutters.h"
#include "status.h"
#include "system.h"
#include "web.h"

// Serial console: type any command (e.g. "1,LONG", "S1,UP", "HELP") and press enter
static void serialLoop() {
  static char buf[CMD_MAX_LEN + 1];
  static size_t len = 0;
  static bool overflow = false;
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r' || c == '\n') {
      if (overflow) {
        LOGW("CMD", "[serial] line too long, ignored");
      } else if (len) {
        CmdResult r = commandExecute(buf, len, SRC_SERIAL);
        // Multi-line or long results (HELP, CONFIG) are only partially shown in the log
        if (r.msg.indexOf('\n') >= 0 || r.msg.length() > 100) logRaw(r.msg.c_str());
      }
      len = 0;
      overflow = false;
    } else if (len < CMD_MAX_LEN) {
      buf[len++] = c;
    } else {
      overflow = true;
    }
  }
}

void setup() {
  logInit(115200);
  LOGI("BOOT", "==== %s %s (built %s %s) ====", FW_NAME, FW_VERSION, __DATE__, __TIME__);
  LOGI("BOOT", "Developed by %s, %s", FW_AUTHOR, FW_URL);

  configLoad();
  logSetLevels(cfg.logLevel, cfg.mqttLogLevel);

  // Relays first, so the outputs are defined (off) as early as possible
  relaysInit({cfg.relayActiveHigh, cfg.interlock, cfg.gapMs, cfg.maxOnSec * 1000UL});

  systemInit();
  displayInit();
  LOGI("BOOT", "Device %s, MAC %s, base topic %s", cfgHostname().c_str(), deviceMacStr(), cfgTopicBase().c_str());
  LOGI("BOOT", "Press timing: short %u ms, long %u ms, gap %u ms", cfg.shortMs, cfg.longMs, cfg.gapMs);

  netInit();
  mqttInit();
  webInit();
  LOGI("BOOT", "Ready. Type HELP on this console for commands.");
}

void loop() {
  netLoop();
  mqttLoop();
  webLoop();
  shuttersLoop();
  statusLoop();
  serialLoop();
  displayLoop();
  systemLoop();  // also feeds the task watchdog
  delay(1);
}
