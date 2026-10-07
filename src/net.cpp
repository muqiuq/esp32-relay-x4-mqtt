#include "net.h"
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include "config.h"
#include "log.h"

namespace {

const IPAddress AP_IP(192, 168, 4, 1);
constexpr uint32_t RETRY_MS = 20000;               // STA reconnect interval
constexpr uint32_t RETRY_WITH_AP_CLIENT_MS = 120000;  // slower while someone uses the portal
constexpr uint32_t AP_LINGER_MS = 60000;           // keep AP after STA came back

DNSServer dns;
bool apActive = false;
bool portalForced = false;
uint32_t portalUntil = 0;
bool staConnected = false;
bool everConnected = false;
bool servicesStarted = false;
uint32_t downSince = 0;
uint32_t connectedAt = 0;
uint32_t lastBegin = 0;
uint32_t reconnects = 0;
volatile uint8_t lastReason = 0;

void onWifiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: {
      uint8_t r = info.wifi_sta_disconnected.reason;
      lastReason = r;
      LOGW("WIFI", "STA disconnected, reason %u (%s)", r, WiFi.disconnectReasonName((wifi_err_reason_t)r));
      break;
    }
    case ARDUINO_EVENT_WIFI_AP_STACONNECTED: {
      const uint8_t* m = info.wifi_ap_staconnected.mac;
      LOGI("WIFI", "AP client %02X:%02X:%02X:%02X:%02X:%02X connected", m[0], m[1], m[2], m[3], m[4], m[5]);
      break;
    }
    case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED: {
      const uint8_t* m = info.wifi_ap_stadisconnected.mac;
      LOGI("WIFI", "AP client %02X:%02X:%02X:%02X:%02X:%02X left", m[0], m[1], m[2], m[3], m[4], m[5]);
      break;
    }
    default:
      break;
  }
}

void staBegin() {
  if (!cfg.wifiSsid[0]) return;
  LOGI("WIFI", "Connecting to '%s'...", cfg.wifiSsid);
  WiFi.begin(cfg.wifiSsid, cfg.wifiPass[0] ? cfg.wifiPass : nullptr);
  lastBegin = millis();
}

void startAp() {
  if (apActive) return;
  String ssid = cfgHostname();
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(AP_IP, AP_IP, IPAddress(255, 255, 255, 0));
  if (!WiFi.softAP(ssid.c_str(), cfg.apPass[0] ? cfg.apPass : nullptr)) {
    LOGE("WIFI", "Starting access point failed");
    return;
  }
  dns.setErrorReplyCode(DNSReplyCode::NoError);
  dns.start(53, "*", AP_IP);
  apActive = true;
  LOGW("WIFI", "Config AP '%s' started (%s), open http://%s/", ssid.c_str(), cfg.apPass[0] ? "WPA2" : "open",
       AP_IP.toString().c_str());
}

void stopAp() {
  if (!apActive) return;
  dns.stop();
  WiFi.softAPdisconnect(true);
  apActive = false;
  portalForced = false;
  LOGI("WIFI", "Config AP stopped");
}

void startServices() {
  String host = cfgHostname();
  if (MDNS.begin(host.c_str())) {
    MDNS.addService("http", "tcp", 80);
    LOGI("WIFI", "mDNS: http://%s.local/", host.c_str());
  } else {
    LOGW("WIFI", "mDNS start failed");
  }
  if (cfg.ntpServer[0]) {
    configTzTime(cfg.tz, cfg.ntpServer);
    LOGI("WIFI", "NTP: %s, TZ %s", cfg.ntpServer, cfg.tz);
  }
}

bool parseIp(const char* s, IPAddress& ip) { return s[0] && ip.fromString(s); }

}  // namespace

void netInit() {
  String host = cfgHostname();
  WiFi.persistent(false);  // credentials live in our own config
  WiFi.onEvent(onWifiEvent);
  WiFi.setHostname(host.c_str());
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false);  // reconnects are handled in netLoop()
  WiFi.setSleep(false);          // mains powered: favour latency and stability

  IPAddress ip, gw, sn, dnsIp;
  if (parseIp(cfg.staticIp, ip)) {
    if (!parseIp(cfg.gateway, gw)) gw = IPAddress(ip[0], ip[1], ip[2], 1);
    if (!parseIp(cfg.subnet, sn)) sn = IPAddress(255, 255, 255, 0);
    if (!parseIp(cfg.dns, dnsIp)) dnsIp = gw;
    WiFi.config(ip, gw, sn, dnsIp);
    LOGI("WIFI", "Static IP %s, gw %s, dns %s", ip.toString().c_str(), gw.toString().c_str(),
         dnsIp.toString().c_str());
  }

  downSince = millis();
  if (!cfg.wifiSsid[0]) {
    LOGW("WIFI", "No WiFi configured");
    startAp();
  } else {
    staBegin();
  }
}

void netLoop() {
  uint32_t now = millis();
  bool conn = WiFi.isConnected();

  if (conn && !staConnected) {
    staConnected = true;
    connectedAt = now;
    if (everConnected) reconnects++;
    everConnected = true;
    LOGI("WIFI", "Connected to '%s' (BSSID %s, ch %d, RSSI %d dBm), IP %s", WiFi.SSID().c_str(),
         WiFi.BSSIDstr().c_str(), WiFi.channel(), WiFi.RSSI(), WiFi.localIP().toString().c_str());
    if (!servicesStarted) {
      startServices();
      servicesStarted = true;
    }
  } else if (!conn && staConnected) {
    staConnected = false;
    downSince = now;
    LOGW("WIFI", "Connection lost");
    lastBegin = now - RETRY_MS + 2000;  // first retry after 2 s
  }

  if (!conn && cfg.wifiSsid[0]) {
    uint32_t interval = (apActive && WiFi.softAPgetStationNum() > 0) ? RETRY_WITH_AP_CLIENT_MS : RETRY_MS;
    if (now - lastBegin >= interval) staBegin();

    if (!apActive && cfg.apFallbackSec && now - downSince >= cfg.apFallbackSec * 1000UL) {
      LOGW("WIFI", "No WiFi for %u s, opening config AP", (unsigned)cfg.apFallbackSec);
      startAp();
    }
  }

  if (apActive) {
    if (portalForced && (int32_t)(now - portalUntil) >= 0) portalForced = false;
    if (conn && !portalForced && now - connectedAt >= AP_LINGER_MS && WiFi.softAPgetStationNum() == 0) stopAp();
    dns.processNextRequest();
  }
}

bool netStaConnected() { return staConnected; }
bool netApActive() { return apActive; }
uint8_t netApClients() { return apActive ? WiFi.softAPgetStationNum() : 0; }
uint32_t netWifiDownSince() { return downSince; }
uint32_t netReconnects() { return reconnects; }
uint32_t netConnectedSince() { return connectedAt; }
uint8_t netLastDisconnectReason() { return lastReason; }

void netStartPortal(uint32_t durationMs) {
  portalForced = true;
  portalUntil = millis() + durationMs;
  startAp();
}

String netScanJson() {
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_FAILED) {
    WiFi.scanNetworks(true);
    return "{\"scanning\":true}";
  }
  if (n == WIFI_SCAN_RUNNING) return "{\"scanning\":true}";

  JsonDocument doc;
  JsonArray arr = doc["networks"].to<JsonArray>();
  for (int i = 0; i < n; i++) {
    String ssid = WiFi.SSID(i);
    if (ssid.isEmpty()) continue;
    bool dup = false;
    for (JsonObject o : arr) {
      if (ssid == o["ssid"].as<const char*>()) {
        dup = true;
        break;
      }
    }
    if (dup) continue;  // results are sorted by RSSI, keep the strongest
    JsonObject o = arr.add<JsonObject>();
    o["ssid"] = ssid;
    o["rssi"] = WiFi.RSSI(i);
    o["open"] = WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
  }
  WiFi.scanDelete();
  String out;
  serializeJson(doc, out);
  return out;
}
