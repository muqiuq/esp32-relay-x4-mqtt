#include "health.h"
#include <PubSubClient.h>
#include <WiFi.h>
#include <esp_system.h>
#include <time.h>
#include "board.h"
#include "config.h"
#include "mqtt.h"
#include "net.h"
#include "system.h"

namespace {
constexpr uint32_t GRACE_MS = 15000;       // normal (re)connects don't raise errors
constexpr uint32_t NTP_GRACE_MS = 120000;  // time to get the first NTP answer
constexpr uint64_t RESET_INFO_SEC = 3600;  // how long reset-related codes stay visible
constexpr int WEAK_RSSI = -80;
}  // namespace

const char* healthText(uint8_t code) {
  switch (code) {
    case H_NO_WIFI_CONFIG:
      return "No WiFi configured (setup AP open)";
    case H_WIFI_DOWN:
      return "WiFi not connected";
    case H_WIFI_AUTH:
      return "WiFi password wrong";
    case H_WIFI_NO_SSID:
      return "WiFi network not found";
    case H_MQTT_NO_CONFIG:
      return "No MQTT broker configured";
    case H_MQTT_UNREACHABLE:
      return "MQTT broker not reachable";
    case H_MQTT_AUTH:
      return "MQTT login rejected";
    case H_MQTT_REFUSED:
      return "MQTT connection refused";
    case H_NO_TIME:
      return "Time not synchronized (NTP)";
    case H_WEAK_SIGNAL:
      return "Weak WiFi signal";
    case H_LOW_MEMORY:
      return "Low memory";
    case H_RESET_CRASH:
      return "Restarted after a crash";
    case H_RESET_WATCHDOG:
      return "Restarted by watchdog";
    case H_RESET_BROWNOUT:
      return "Restarted by brownout (check power supply)";
    case H_RESET_CONNECTION:
      return "Restarted by connection watchdog";
  }
  return "Unknown";
}

size_t healthCollect(uint8_t* codes, size_t max) {
  size_t n = 0;
  auto add = [&](uint8_t c) {
    if (n < max) codes[n++] = c;
  };
  uint32_t now = millis();
  bool sta = netStaConnected();

  if (!cfg.wifiSsid[0]) {
    add(H_NO_WIFI_CONFIG);
  } else if (!sta && now - netWifiDownSince() >= GRACE_MS) {
    uint8_t r = netLastDisconnectReason();
    if (r == WIFI_REASON_NO_AP_FOUND)
      add(H_WIFI_NO_SSID);
    else if (r == WIFI_REASON_AUTH_FAIL || r == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT || r == WIFI_REASON_HANDSHAKE_TIMEOUT)
      add(H_WIFI_AUTH);
    else
      add(H_WIFI_DOWN);
  }

  if (sta) {
    if (!mqttEnabled()) {
      add(H_MQTT_NO_CONFIG);
    } else if (!mqttConnected() && now - mqttDownSince() >= GRACE_MS) {
      int s = mqttLastState();
      if (s == MQTT_CONNECT_BAD_CREDENTIALS || s == MQTT_CONNECT_UNAUTHORIZED)
        add(H_MQTT_AUTH);
      else if (s == MQTT_CONNECT_BAD_PROTOCOL || s == MQTT_CONNECT_BAD_CLIENT_ID || s == MQTT_CONNECT_UNAVAILABLE)
        add(H_MQTT_REFUSED);
      else
        add(H_MQTT_UNREACHABLE);
    }
    if (cfg.ntpServer[0] && time(nullptr) < 1700000000 && now - netConnectedSince() >= NTP_GRACE_MS) add(H_NO_TIME);
    if (WiFi.RSSI() < WEAK_RSSI) add(H_WEAK_SIGNAL);
  }

  if (ESP.getFreeHeap() < 2 * LOW_HEAP_BYTES) add(H_LOW_MEMORY);

  if (systemUptimeSec() < RESET_INFO_SEC) {
    switch (esp_reset_reason()) {
      case ESP_RST_PANIC:
        add(H_RESET_CRASH);
        break;
      case ESP_RST_INT_WDT:
      case ESP_RST_TASK_WDT:
      case ESP_RST_WDT:
        add(H_RESET_WATCHDOG);
        break;
      case ESP_RST_BROWNOUT:
        add(H_RESET_BROWNOUT);
        break;
      default:
        break;
    }
    const char* lr = systemLastRestart();
    if (!strcmp(lr, "wifi-timeout") || !strcmp(lr, "mqtt-timeout")) add(H_RESET_CONNECTION);
  }
  return n;
}
