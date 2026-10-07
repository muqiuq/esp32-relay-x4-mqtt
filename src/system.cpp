#include "system.h"
#include <Preferences.h>
#include <esp_task_wdt.h>
#include <esp_timer.h>
#include <time.h>
#include "board.h"
#include "config.h"
#include "log.h"
#include "mqtt.h"
#include "net.h"
#include "relays.h"

namespace {

// Survives software resets and panics (not power loss): tells why we restarted
constexpr uint32_t RTC_MAGIC = 0x52454C34;  // "REL4"
RTC_NOINIT_ATTR uint32_t rtcMagic;
RTC_NOINIT_ATTR char rtcReason[24];

char lastRestart[24] = "";
uint32_t bootCount = 0;

bool restartPending = false;
uint32_t restartAt = 0;
uint32_t restartRequestedAt = 0;
char restartReason[24] = "";
constexpr uint32_t RESTART_MAX_WAIT_MS = 15000;

uint32_t lastCheck = 0;
uint32_t lowHeapSince = 0;

// BOOT button
bool btnRaw = false, btnStable = false;
uint32_t btnChangedAt = 0, btnPressedAt = 0;
bool factoryFired = false;
bool btnWarned = false;

void doRestart() {
  LOGW("SYS", "Restarting: %s", restartReason);
  relaysAllOff();
  mqttShutdown();
  rtcMagic = RTC_MAGIC;
  strlcpy(rtcReason, restartReason, sizeof(rtcReason));
  delay(300);  // let the relay task release, the log task flush and TCP send
  ESP.restart();
}

// Seconds until the connection watchdog reboots (0 = due now), -1 if not counting down
int32_t rebootCountdown(const char** reason) {
  if (netApClients() > 0) return -1;  // someone is using the config portal
  uint32_t now = millis();
  int32_t best = -1;
  auto consider = [&](uint32_t since, uint16_t minutes, const char* why) {
    uint32_t limit = minutes * 60000UL;
    uint32_t elapsed = now - since;
    int32_t left = elapsed >= limit ? 0 : (int32_t)((limit - elapsed + 999) / 1000);
    if (best < 0 || left < best) {
      best = left;
      if (reason) *reason = why;
    }
  };
  if (!cfg.wifiSsid[0]) return -1;
  if (cfg.rebootNoWifiMin && !netStaConnected()) consider(netWifiDownSince(), cfg.rebootNoWifiMin, "wifi-timeout");
  if (cfg.rebootNoMqttMin && mqttEnabled() && !mqttConnected())
    consider(mqttDownSince(), cfg.rebootNoMqttMin, "mqtt-timeout");
  return best;
}

void checkConnectivity() {
  if (restartPending) return;
  const char* why = "";
  if (rebootCountdown(&why) == 0) {
    LOGE("SYS", "Connection watchdog expired (%s)", why);
    systemRequestRestart(why, 0);
  }
}

void checkHealth(uint32_t now) {
  uint32_t heap = ESP.getFreeHeap();
  if (heap < LOW_HEAP_BYTES) {
    if (!lowHeapSince) {
      lowHeapSince = now ? now : 1;
      LOGW("SYS", "Low memory: %u bytes free", (unsigned)heap);
    } else if (now - lowHeapSince >= LOW_HEAP_GRACE_MS && !restartPending) {
      systemRequestRestart("low-heap", 0);
    }
  } else {
    lowHeapSince = 0;
  }
  if (cfg.rebootEveryH && !restartPending && systemUptimeSec() >= (uint64_t)cfg.rebootEveryH * 3600ULL) {
    systemRequestRestart("scheduled", 0);
  }
}

void buttonLoop(uint32_t now) {
#if BUTTON_PIN >= 0
  bool raw = digitalRead(BUTTON_PIN) == LOW;
  if (raw != btnRaw) {
    btnRaw = raw;
    btnChangedAt = now;
  }
  if (now - btnChangedAt < 30 || raw == btnStable) {
    // stable state unchanged: check long hold
    if (btnStable && !factoryFired) {
      uint32_t held = now - btnPressedAt;
      if (held >= 3000 && !btnWarned) {
        btnWarned = true;
        LOGW("SYS", "Button held - keep holding for factory reset");
      }
      if (held >= FACTORY_RESET_HOLD_MS) {
        factoryFired = true;
        systemFactoryReset();
      }
    }
    return;
  }
  btnStable = raw;
  if (btnStable) {
    btnPressedAt = now;
    btnWarned = false;
  } else if (!factoryFired) {
    uint32_t held = now - btnPressedAt;
    if (held < 2000) {
      LOGI("SYS", "Button pressed: opening config AP");
      netStartPortal(PORTAL_BUTTON_MS);
    } else {
      LOGI("SYS", "Button released after %u ms, nothing to do", (unsigned)held);
    }
  }
#endif
}

void ledLoop(uint32_t now) {
#if LED_PIN >= 0
  // 20 slots of 100 ms; bit n = LED state in slot n
  uint32_t pattern;
  if (btnStable && now - btnPressedAt >= 2000)
    pattern = 0x55555;  // fast flicker: factory reset approaching
  else if (netApActive())
    pattern = 0x33333;  // fast blink: config AP open
  else if (!netStaConnected())
    pattern = 0x003FF;  // slow blink: connecting to WiFi
  else if (mqttEnabled() && !mqttConnected())
    pattern = 0x00033;  // double blink: no MQTT
  else
    pattern = 0xFFFFE;  // on with a short heartbeat gap
  uint32_t slot = (now / 100) % 20;
  bool on = (pattern >> slot) & 1;
  digitalWrite(LED_PIN, on != LED_ACTIVE_LOW ? HIGH : LOW);
#endif
}

const char* resetReasonName(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON:
      return "power-on";
    case ESP_RST_EXT:
      return "external";
    case ESP_RST_SW:
      return "software";
    case ESP_RST_PANIC:
      return "panic";
    case ESP_RST_INT_WDT:
      return "int-watchdog";
    case ESP_RST_TASK_WDT:
      return "task-watchdog";
    case ESP_RST_WDT:
      return "watchdog";
    case ESP_RST_DEEPSLEEP:
      return "deep-sleep";
    case ESP_RST_BROWNOUT:
      return "brownout";
    case ESP_RST_SDIO:
      return "sdio";
    default:
      return "unknown";
  }
}

}  // namespace

void systemInit() {
  esp_reset_reason_t rr = esp_reset_reason();
  if (rtcMagic == RTC_MAGIC && rr == ESP_RST_SW) {
    rtcReason[sizeof(rtcReason) - 1] = '\0';
    strlcpy(lastRestart, rtcReason, sizeof(lastRestart));
  }
  rtcMagic = 0;

  Preferences prefs;
  if (prefs.begin("relay4", false)) {
    bootCount = prefs.getUInt("boots", 0) + 1;
    prefs.putUInt("boots", bootCount);
    prefs.end();
  }
  LOGI("SYS", "Boot #%u, reset reason: %s%s%s", (unsigned)bootCount, resetReasonName(rr),
       lastRestart[0] ? ", requested by firmware: " : "", lastRestart);

#if LED_PIN >= 0
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LED_ACTIVE_LOW ? HIGH : LOW);
#endif
#if BUTTON_PIN >= 0
  pinMode(BUTTON_PIN, INPUT_PULLUP);
#endif

  // Task watchdog for loop(): resets the chip if loop() hangs
  esp_task_wdt_init(WDT_TIMEOUT_S, true);
  esp_task_wdt_add(nullptr);
  LOGI("SYS", "Task watchdog: %u s", (unsigned)WDT_TIMEOUT_S);
}

void systemApplyConfig() {
  logSetLevels(cfg.logLevel, cfg.mqttLogLevel);
  relaysApplySettings({cfg.relayActiveHigh, cfg.interlock, cfg.gapMs, cfg.maxOnSec * 1000UL});
}

void systemLoop() {
  uint32_t now = millis();
  esp_task_wdt_reset();
  buttonLoop(now);
  ledLoop(now);

  if (restartPending && (int32_t)(now - restartAt) >= 0) {
    if (!relaysBusy() || now - restartRequestedAt >= RESTART_MAX_WAIT_MS) doRestart();
  }

  if (now - lastCheck >= 1000) {
    lastCheck = now;
    checkConnectivity();
    checkHealth(now);
  }
}

void systemRequestRestart(const char* reason, uint32_t delayMs) {
  if (restartPending) return;
  restartPending = true;
  restartRequestedAt = millis();
  restartAt = restartRequestedAt + delayMs;
  strlcpy(restartReason, reason, sizeof(restartReason));
  LOGW("SYS", "Restart requested: %s", reason);
}

void systemFactoryReset() {
  LOGW("SYS", "FACTORY RESET");
  configErase();
  systemRequestRestart("factory-reset", 500);
}

uint64_t systemUptimeSec() { return esp_timer_get_time() / 1000000ULL; }

String systemUptimeStr() {
  uint64_t s = systemUptimeSec();
  char buf[24];
  snprintf(buf, sizeof(buf), "%ud %02u:%02u:%02u", (unsigned)(s / 86400), (unsigned)(s / 3600 % 24),
           (unsigned)(s / 60 % 60), (unsigned)(s % 60));
  return buf;
}

bool systemTimeStr(char* buf, size_t len) {
  time_t now = time(nullptr);
  if (now < 1700000000) return false;  // not synced yet
  struct tm tm;
  localtime_r(&now, &tm);
  return strftime(buf, len, "%Y-%m-%dT%H:%M:%S%z", &tm) > 0;
}

const char* systemResetReason() { return resetReasonName(esp_reset_reason()); }
const char* systemLastRestart() { return lastRestart; }
uint32_t systemBootCount() { return bootCount; }
bool systemRestartPending() { return restartPending; }
int32_t systemRebootCountdownSec() { return restartPending ? -1 : rebootCountdown(nullptr); }

uint32_t systemButtonHeldMs() {
  if (!btnStable || factoryFired) return 0;
  return millis() - btnPressedAt;
}
