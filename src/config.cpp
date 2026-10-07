#include "config.h"
#include <Preferences.h>
#include <memory>
#include "display.h"
#include "log.h"

Config cfg;
const char* const SECRET_KEEP = "__KEEP__";

static const char* NVS_NS = "relay4";
static const char* NVS_KEY = "cfg";

#define MSIZE(m) sizeof(((Config*)nullptr)->m)
#define OFS(m) (uint16_t) offsetof(Config, m)
#define F_STR(k, m, fl, g, l, h) {k, l, g, FT_STR, fl, OFS(m), (uint16_t)MSIZE(m), 0, 0, nullptr, h}
#define F_SECRET(k, m, fl, g, l, h) {k, l, g, FT_SECRET, fl, OFS(m), (uint16_t)MSIZE(m), 0, 0, nullptr, h}
#define F_BOOL(k, m, fl, g, l, h) {k, l, g, FT_BOOL, fl, OFS(m), 1, 0, 1, nullptr, h}
#define F_U8(k, m, fl, g, l, mn, mx, o, h) {k, l, g, FT_U8, fl, OFS(m), 1, mn, mx, o, h}
#define F_U16(k, m, fl, g, l, mn, mx, h) {k, l, g, FT_U16, fl, OFS(m), 2, mn, mx, nullptr, h}

static const char* LOG_LEVELS = "Off|Error|Warning|Info|Debug";
static const uint8_t NET = FF_REBOOT | FF_LOCAL;

static const FieldDef FIELDS[] = {
    F_STR("wifiSsid", wifiSsid, NET, "WiFi", "SSID", nullptr),
    F_SECRET("wifiPass", wifiPass, NET, "WiFi", "Password", nullptr),
    F_STR("hostname", hostname, NET | FF_HOSTNAME, "WiFi", "Hostname",
          "Empty = relay4-XXXXXX. Also used for DHCP, mDNS (.local) and the config AP name."),
    F_STR("staticIp", staticIp, NET | FF_IP, "WiFi", "Static IP", "Empty = DHCP"),
    F_STR("gateway", gateway, NET | FF_IP, "WiFi", "Gateway", nullptr),
    F_STR("subnet", subnet, NET | FF_IP, "WiFi", "Subnet mask", "Empty = 255.255.255.0"),
    F_STR("dns", dns, NET | FF_IP, "WiFi", "DNS server", "Empty = gateway"),
    F_SECRET("apPass", apPass, NET | FF_WPA, "WiFi", "Config AP password", "Empty = open access point. 8-63 characters."),
    F_U16("apFallbackSec", apFallbackSec, 0, "WiFi", "Open config AP after (s) without WiFi", 0, 3600,
          "0 = only when no WiFi is configured. WiFi reconnects continue while the AP is open."),

    F_STR("mqttHost", mqttHost, NET, "MQTT", "Broker host", "Hostname or IP. Empty = MQTT disabled."),
    F_U16("mqttPort", mqttPort, NET, "MQTT", "Broker port", 1, 65535, nullptr),
    F_STR("mqttUser", mqttUser, NET, "MQTT", "User", nullptr),
    F_SECRET("mqttPass", mqttPass, NET, "MQTT", "Password", nullptr),
    F_STR("mqttClientId", mqttClientId, NET, "MQTT", "Client ID", "Empty = relay4-<MAC>"),
    F_STR("topicBase", topicBase, NET | FF_TOPIC, "MQTT", "Base topic",
          "Empty = esp32/relay4-<MAC>. Subtopics: /status /cmd /result /availability /log"),
    F_U16("mqttKeepalive", mqttKeepalive, 0, "MQTT", "Keepalive (s)", 5, 600, "Applied on next reconnect"),
    F_U16("statusInterval", statusInterval, 0, "MQTT", "Status interval (s)", 10, 65535, nullptr),
    F_BOOL("statusRetain", statusRetain, 0, "MQTT", "Retain status message", nullptr),
    F_BOOL("publishOnChange", publishOnChange, 0, "MQTT", "Publish status on every change", nullptr),
    F_U8("mqttLogLevel", mqttLogLevel, 0, "MQTT", "Forward log to <base>/log", 0, 4, LOG_LEVELS, nullptr),

    F_BOOL("relayActiveHigh", relayActiveHigh, FF_LOCAL, "Relays", "Relays are active HIGH",
           "On for ESP32_Relay_X4. Wrong polarity energizes ALL relays (both directions)!"),
    F_BOOL("interlock", interlock, 0, "Relays", "Interlock up/down",
           "Never energize both relays of a shutter at the same time. A new press releases the partner first."),
    F_U16("shortMs", shortMs, 0, "Relays", "Short press (ms)", 20, 10000, nullptr),
    F_U16("longMs", longMs, 0, "Relays", "Long press (ms)", 20, 60000, nullptr),
    F_U16("gapMs", gapMs, 0, "Relays", "Pause between presses (ms)", 0, 10000,
          "Minimum release time before a relay or its partner is pressed again"),
    F_U16("maxOnSec", maxOnSec, 0, "Relays", "Max ON time (s)", 0, 3600, "Safety limit for ON commands. 0 = unlimited."),

    F_STR("s1Name", shutterName[0], 0, "Shutters", "Shutter 1 name", "Relay 1 = up, relay 2 = down"),
    F_BOOL("s1Invert", shutterInvert[0], 0, "Shutters", "Shutter 1: swap up/down", nullptr),
    F_STR("s2Name", shutterName[1], 0, "Shutters", "Shutter 2 name", "Relay 3 = up, relay 4 = down"),
    F_BOOL("s2Invert", shutterInvert[1], 0, "Shutters", "Shutter 2: swap up/down", nullptr),
    F_U8("stopMode", stopMode, 0, "Shutters", "STOP command", 0, 1,
         "Short press of last direction|Short press of opposite direction",
         "Button pressed for STOP. If the last direction is unknown, UP is pressed."),
    F_U16("travelSec", travelSec, 0, "Shutters", "Travel time (s)", 0, 600,
          "Used to estimate open/closed after a long press. 0 = assume end position immediately."),

    F_U16("rebootNoWifiMin", rebootNoWifiMin, 0, "Watchdog", "Reboot after (min) without WiFi", 0, 1440, "0 = disabled"),
    F_U16("rebootNoMqttMin", rebootNoMqttMin, 0, "Watchdog", "Reboot after (min) without MQTT", 0, 1440, "0 = disabled"),
    F_U16("rebootEveryH", rebootEveryH, 0, "Watchdog", "Scheduled reboot every (h)", 0, 720, "0 = disabled"),

    F_SECRET("webPass", webPass, FF_LOCAL, "System", "Web / OTA password",
             "User name: admin. Empty = no authentication."),
    F_STR("ntpServer", ntpServer, FF_REBOOT, "System", "NTP server", nullptr),
    F_STR("tz", tz, FF_REBOOT, "System", "Timezone (POSIX)", "e.g. CET-1CEST,M3.5.0,M10.5.0/3"),
    F_U8("logLevel", logLevel, 0, "System", "Serial log level", 0, 4, LOG_LEVELS, nullptr),

    F_U8("dispMode", dispMode, 0, "Display", "Display mode", 0, 3, "Off|Status rotation|Clock|Shutter states",
         "Errors (Err nn), setup info and events are shown in every mode except Off"),
    F_U8("dispBrightness", dispBrightness, 0, "Display", "Brightness (0-7)", 0, 7, nullptr, nullptr),
    F_U8("dispPageSec", dispPageSec, 0, "Display", "Seconds per page", 1, 30, nullptr, nullptr),
    F_BOOL("dispEvents", dispEvents, 0, "Display", "Show commands (e.g. 1uP Lo, r2 on)", nullptr),
    F_U8("dispOrder", dispOrder, 0, "Display", "Digit order", 0, 1, "Standard 123456|6-digit module 321654",
         "Change if the text appears scrambled (test with the command SHOW 123456)"),
};
static constexpr size_t FIELD_COUNT = sizeof(FIELDS) / sizeof(FIELDS[0]);

void configDefaults(Config& c) {
  memset(&c, 0, sizeof(c));
  c.apFallbackSec = 120;
  c.mqttPort = 1883;
  c.mqttKeepalive = 30;
  c.statusInterval = 180;
  c.statusRetain = true;
  c.publishOnChange = true;
  c.mqttLogLevel = LL_OFF;
  c.relayActiveHigh = true;
  c.interlock = true;
  c.shortMs = 300;
  c.longMs = 2000;
  c.gapMs = 300;
  c.maxOnSec = 60;
  c.stopMode = 0;
  c.travelSec = 60;
  c.rebootNoWifiMin = 10;
  c.rebootNoMqttMin = 15;
  c.rebootEveryH = 0;
  strlcpy(c.ntpServer, "pool.ntp.org", sizeof(c.ntpServer));
  strlcpy(c.tz, "CET-1CEST,M3.5.0,M10.5.0/3", sizeof(c.tz));
  c.logLevel = LL_INFO;
  c.dispMode = 1;
  c.dispBrightness = 4;
  c.dispPageSec = 3;
  c.dispEvents = true;
  c.dispOrder = 1;
}

const FieldDef* configFields(size_t& count) {
  count = FIELD_COUNT;
  return FIELDS;
}

const FieldDef* configFindField(const char* key) {
  for (const FieldDef& f : FIELDS) {
    if (strcasecmp(f.key, key) == 0) return &f;
  }
  return nullptr;
}

// ---------------------------------------------------------------------------
// Validation helpers
// ---------------------------------------------------------------------------
static bool parseBool(const String& s, bool& out) {
  String v = s;
  v.trim();
  v.toLowerCase();
  if (v == "1" || v == "true" || v == "on" || v == "yes") {
    out = true;
    return true;
  }
  if (v == "0" || v == "false" || v == "off" || v == "no" || v.isEmpty()) {
    out = false;
    return true;
  }
  return false;
}

static bool validHostname(const String& s) {
  if (s.length() > 32) return false;
  for (size_t i = 0; i < s.length(); i++) {
    char ch = s[i];
    if (!isalnum((unsigned char)ch) && ch != '-') return false;
  }
  return s.isEmpty() || (s[0] != '-' && s[s.length() - 1] != '-');
}

// Returns the option index for a label like "debug", or -1
static int optionIndex(const char* options, const String& label) {
  if (!options) return -1;
  int idx = 0;
  const char* p = options;
  while (*p) {
    const char* end = strchr(p, '|');
    size_t len = end ? (size_t)(end - p) : strlen(p);
    if (label.length() == len && strncasecmp(p, label.c_str(), len) == 0) return idx;
    if (!end) break;
    p = end + 1;
    idx++;
  }
  return -1;
}

bool configSetValue(Config& c, const FieldDef& f, const char* value, String& err) {
  uint8_t* ptr = reinterpret_cast<uint8_t*>(&c) + f.offset;
  String v = value ? value : "";

  switch (f.type) {
    case FT_STR:
    case FT_SECRET: {
      if (f.type == FT_STR) v.trim();  // secrets may legitimately contain spaces
      if (v.length() >= f.size) {
        err = String(f.key) + ": too long (max " + (f.size - 1) + ")";
        return false;
      }
      if ((f.flags & FF_IP) && !v.isEmpty()) {
        IPAddress ip;
        if (!ip.fromString(v)) {
          err = String(f.key) + ": invalid IPv4 address";
          return false;
        }
      }
      if ((f.flags & FF_HOSTNAME) && !validHostname(v)) {
        err = String(f.key) + ": only letters, digits and '-' allowed";
        return false;
      }
      if (f.flags & FF_TOPIC) {
        while (v.endsWith("/")) v.remove(v.length() - 1);
        if (v.indexOf('#') >= 0 || v.indexOf('+') >= 0) {
          err = String(f.key) + ": wildcards (# +) not allowed";
          return false;
        }
      }
      if ((f.flags & FF_WPA) && !v.isEmpty() && (v.length() < 8 || v.length() > 63)) {
        err = String(f.key) + ": must be empty or 8-63 characters";
        return false;
      }
      strlcpy(reinterpret_cast<char*>(ptr), v.c_str(), f.size);
      return true;
    }
    case FT_BOOL: {
      bool b;
      if (!parseBool(v, b)) {
        err = String(f.key) + ": expected true/false";
        return false;
      }
      *reinterpret_cast<bool*>(ptr) = b;
      return true;
    }
    case FT_U8:
    case FT_U16: {
      v.trim();
      long n = optionIndex(f.options, v);
      if (n < 0) {
        char* end = nullptr;
        n = strtol(v.c_str(), &end, 10);
        if (v.isEmpty() || *end != '\0') {
          err = String(f.key) + ": not a number";
          return false;
        }
      }
      if (n < f.min || n > f.max) {
        err = String(f.key) + ": out of range (" + f.min + ".." + f.max + ")";
        return false;
      }
      if (f.type == FT_U8)
        *ptr = (uint8_t)n;
      else
        *reinterpret_cast<uint16_t*>(ptr) = (uint16_t)n;
      return true;
    }
  }
  return false;
}

String configGetValue(const Config& c, const FieldDef& f) {
  const uint8_t* ptr = reinterpret_cast<const uint8_t*>(&c) + f.offset;
  switch (f.type) {
    case FT_STR:
    case FT_SECRET:
      return String(reinterpret_cast<const char*>(ptr));
    case FT_BOOL:
      return *reinterpret_cast<const bool*>(ptr) ? "1" : "0";
    case FT_U8:
      return String(*ptr);
    case FT_U16:
      return String(*reinterpret_cast<const uint16_t*>(ptr));
  }
  return String();
}

void configToJson(const Config& c, JsonObject obj, bool includeSecrets) {
  for (const FieldDef& f : FIELDS) {
    const uint8_t* ptr = reinterpret_cast<const uint8_t*>(&c) + f.offset;
    switch (f.type) {
      case FT_STR:
        obj[f.key] = reinterpret_cast<const char*>(ptr);
        break;
      case FT_SECRET: {
        const char* s = reinterpret_cast<const char*>(ptr);
        obj[f.key] = includeSecrets ? s : (s[0] ? SECRET_KEEP : "");
        break;
      }
      case FT_BOOL:
        obj[f.key] = *reinterpret_cast<const bool*>(ptr);
        break;
      case FT_U8:
        obj[f.key] = *ptr;
        break;
      case FT_U16:
        obj[f.key] = *reinterpret_cast<const uint16_t*>(ptr);
        break;
    }
  }
}

int configApplyJson(Config& c, JsonObjectConst obj, bool remote, String& err) {
  int errors = 0;
  auto addErr = [&](const String& e) {
    if (err.length()) err += "; ";
    err += e;
    errors++;
  };

  for (JsonPairConst kv : obj) {
    const char* key = kv.key().c_str();
    const FieldDef* f = configFindField(key);
    if (!f) {
      addErr(String(key) + ": unknown setting");
      continue;
    }
    if (remote && (f->flags & FF_LOCAL)) {
      addErr(String(f->key) + ": not changeable via MQTT");
      continue;
    }
    JsonVariantConst v = kv.value();
    String s;
    if (v.is<bool>())
      s = v.as<bool>() ? "1" : "0";
    else if (v.is<long>())
      s = String(v.as<long>());
    else if (v.is<double>())
      s = String(lround(v.as<double>()));
    else if (v.is<const char*>())
      s = v.as<const char*>();
    else {
      addErr(String(f->key) + ": unsupported value type");
      continue;
    }
    if (f->type == FT_SECRET && s == SECRET_KEEP) continue;
    String e;
    if (!configSetValue(c, *f, s.c_str(), e)) addErr(e);
  }
  return errors;
}

bool configNeedsReboot(const Config& a, const Config& b) {
  for (const FieldDef& f : FIELDS) {
    if (!(f.flags & FF_REBOOT)) continue;
    const uint8_t* pa = reinterpret_cast<const uint8_t*>(&a) + f.offset;
    const uint8_t* pb = reinterpret_cast<const uint8_t*>(&b) + f.offset;
    bool differs = (f.type == FT_STR || f.type == FT_SECRET)
                       ? strcmp(reinterpret_cast<const char*>(pa), reinterpret_cast<const char*>(pb)) != 0
                       : memcmp(pa, pb, f.size) != 0;
    if (differs) return true;
  }
  return false;
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------
void configLoad() {
  configDefaults(cfg);
  Preferences prefs;
  if (!prefs.begin(NVS_NS, false)) {
    LOGE("CFG", "NVS open failed, using defaults");
    return;
  }
  String json = prefs.getString(NVS_KEY, "");
  prefs.end();
  if (json.isEmpty()) {
    LOGI("CFG", "No stored config, using defaults");
    return;
  }
  JsonDocument doc;
  DeserializationError de = deserializeJson(doc, json);
  if (de || !doc.is<JsonObject>()) {
    LOGE("CFG", "Stored config corrupt (%s), using defaults", de.c_str());
    return;
  }
  // Lenient: invalid values (e.g. after a firmware update changed limits) keep their default
  String err;
  int errors = configApplyJson(cfg, doc.as<JsonObjectConst>(), false, err);
  if (errors) LOGW("CFG", "Ignored %d stored value(s): %s", errors, err.c_str());
  LOGI("CFG", "Config loaded (%u bytes)", (unsigned)json.length());
}

bool configSave() {
  JsonDocument doc;
  configToJson(cfg, doc.to<JsonObject>(), true);
  String json;
  serializeJson(doc, json);

  Preferences prefs;
  if (!prefs.begin(NVS_NS, false)) {
    LOGE("CFG", "NVS open failed, config not saved");
    return false;
  }
  bool ok = true;
  if (prefs.getString(NVS_KEY, "") != json) {
    ok = prefs.putString(NVS_KEY, json.c_str()) == json.length();
    if (ok) {
      LOGI("CFG", "Config saved (%u bytes)", (unsigned)json.length());
      displayEvent("SAuEd");
    } else {
      LOGE("CFG", "Writing config to NVS failed");
    }
  } else {
    LOGD("CFG", "Config unchanged, nothing written");
  }
  prefs.end();
  return ok;
}

void configErase() {
  Preferences prefs;
  if (prefs.begin(NVS_NS, false)) {
    prefs.remove(NVS_KEY);
    prefs.end();
  }
  LOGW("CFG", "Config erased");
}

// ---------------------------------------------------------------------------
// Derived values
// ---------------------------------------------------------------------------
static uint8_t macBytes[6];
static bool macRead = false;

static void readMac() {
  if (macRead) return;
  uint64_t mac = ESP.getEfuseMac();  // base MAC == WiFi STA MAC
  for (int i = 0; i < 6; i++) macBytes[i] = (mac >> (8 * i)) & 0xFF;
  macRead = true;
}

const char* deviceMacHex() {
  static char buf[13];
  readMac();
  snprintf(buf, sizeof(buf), "%02X%02X%02X%02X%02X%02X", macBytes[0], macBytes[1], macBytes[2], macBytes[3],
           macBytes[4], macBytes[5]);
  return buf;
}

const char* deviceMacStr() {
  static char buf[18];
  readMac();
  snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X", macBytes[0], macBytes[1], macBytes[2], macBytes[3],
           macBytes[4], macBytes[5]);
  return buf;
}

String cfgDefaultHostname() { return String("relay4-") + (deviceMacHex() + 6); }
String cfgDefaultTopicBase() { return String("esp32/relay4-") + deviceMacHex(); }
String cfgDefaultClientId() { return String("relay4-") + deviceMacHex(); }

String cfgHostname() { return cfg.hostname[0] ? String(cfg.hostname) : cfgDefaultHostname(); }
String cfgTopicBase() { return cfg.topicBase[0] ? String(cfg.topicBase) : cfgDefaultTopicBase(); }
String cfgClientId() { return cfg.mqttClientId[0] ? String(cfg.mqttClientId) : cfgDefaultClientId(); }

String cfgShutterName(uint8_t idx) {
  if (idx < SHUTTER_COUNT && cfg.shutterName[idx][0]) return cfg.shutterName[idx];
  return String("Shutter ") + (idx + 1);
}
