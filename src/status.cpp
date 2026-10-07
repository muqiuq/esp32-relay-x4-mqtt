#include "status.h"
#include <WiFi.h>
#include "board.h"
#include "config.h"
#include "display.h"
#include "health.h"
#include "log.h"
#include "mqtt.h"
#include "net.h"
#include "relays.h"
#include "shutters.h"
#include "system.h"

namespace {
const char* pendingReason = nullptr;
uint32_t lastPeriodic = 0;
uint32_t lastPublish = 0;
uint32_t seenRelaySeq = 0;
uint32_t seenShutterSeq = 0;
constexpr uint32_t MIN_GAP_MS = 200;  // coalesces bursts of changes
}  // namespace

void statusBuild(JsonDocument& doc, const char* reason) {
  doc["device"] = cfgHostname();
  doc["fw"] = FW_VERSION;
  doc["author"] = FW_AUTHOR;
  if (reason) doc["reason"] = reason;
  doc["uptime"] = (uint32_t)systemUptimeSec();
  doc["uptimeStr"] = systemUptimeStr();
  char tbuf[32];
  if (systemTimeStr(tbuf, sizeof(tbuf))) doc["time"] = tbuf;

  JsonArray relays = doc["relays"].to<JsonArray>();
  JsonArray presses = doc["presses"].to<JsonArray>();
  JsonArray pins = doc["pins"].to<JsonArray>();
  for (uint8_t i = 0; i < RELAY_COUNT; i++) {
    relays.add(relayIsOn(i) ? 1 : 0);
    presses.add(relayPressCount(i));
    pins.add(RELAY_PINS[i]);
  }
  shuttersToJson(doc["shutters"].to<JsonArray>());

  uint8_t codes[16];
  size_t n = healthCollect(codes, sizeof(codes));
  JsonArray errors = doc["errors"].to<JsonArray>();
  for (size_t i = 0; i < n; i++) {
    JsonObject e = errors.add<JsonObject>();
    e["code"] = codes[i];
    e["msg"] = healthText(codes[i]);
  }
#if DISPLAY_CLK_PIN >= 0
  JsonObject d = doc["display"].to<JsonObject>();
  d["ok"] = displayPresent();
  d["text"] = displayCurrentText();
#endif

  JsonObject w = doc["wifi"].to<JsonObject>();
  bool sta = netStaConnected();
  w["connected"] = sta;
  if (sta) {
    w["ssid"] = WiFi.SSID();
    w["rssi"] = WiFi.RSSI();
    w["ip"] = WiFi.localIP().toString();
    w["bssid"] = WiFi.BSSIDstr();
    w["ch"] = WiFi.channel();
  }
  w["reconnects"] = netReconnects();
  w["ap"] = netApActive();
  if (netApActive()) {
    w["apIp"] = WiFi.softAPIP().toString();
    w["apClients"] = netApClients();
  }

  JsonObject m = doc["mqtt"].to<JsonObject>();
  m["enabled"] = mqttEnabled();
  m["connected"] = mqttConnected();
  m["reconnects"] = mqttReconnects();
  m["base"] = cfgTopicBase();

  JsonObject s = doc["sys"].to<JsonObject>();
  s["heap"] = ESP.getFreeHeap();
  s["minHeap"] = ESP.getMinFreeHeap();
  s["maxBlock"] = ESP.getMaxAllocHeap();
  s["boots"] = systemBootCount();
  s["resetReason"] = systemResetReason();
  s["lastRestart"] = systemLastRestart();
  s["mac"] = deviceMacStr();
  s["chipRev"] = ESP.getChipRevision();
  s["sdk"] = ESP.getSdkVersion();

  JsonObject c = doc["cfg"].to<JsonObject>();
  c["shortMs"] = cfg.shortMs;
  c["longMs"] = cfg.longMs;
  c["gapMs"] = cfg.gapMs;
  c["interlock"] = cfg.interlock;
  c["interval"] = cfg.statusInterval;
}

void statusRequest(const char* reason) { pendingReason = reason; }

void statusLoop() {
  uint32_t now = millis();

  uint32_t rs = relaysChangeSeq();
  uint32_t ss = shuttersChangeSeq();
  if (rs != seenRelaySeq || ss != seenShutterSeq) {
    seenRelaySeq = rs;
    seenShutterSeq = ss;
    if (cfg.publishOnChange && !pendingReason) pendingReason = "change";
  }
  if (now - lastPeriodic >= cfg.statusInterval * 1000UL) {
    lastPeriodic = now;
    if (!pendingReason) pendingReason = "interval";
  }

  if (!pendingReason) return;
  if (!mqttConnected()) {
    pendingReason = nullptr;  // a fresh status is sent on (re)connect anyway
    return;
  }
  if (now - lastPublish < MIN_GAP_MS) return;

  JsonDocument doc;
  statusBuild(doc, pendingReason);
  if (mqttPublishJson("status", doc, cfg.statusRetain))
    LOGD("STAT", "Status published (%s)", pendingReason);
  else
    LOGW("STAT", "Status publish failed");
  pendingReason = nullptr;
  lastPublish = now;
}
