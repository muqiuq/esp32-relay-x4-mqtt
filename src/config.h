#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "board.h"

// Persistent configuration (stored as JSON in NVS).
// Every field is described in a table (see config.cpp) which drives defaults,
// validation, JSON import/export, the web form and the SET command.
struct Config {
  // WiFi
  char wifiSsid[33];
  char wifiPass[65];
  char hostname[33];
  char staticIp[16];
  char gateway[16];
  char subnet[16];
  char dns[16];
  char apPass[65];
  uint16_t apFallbackSec;
  // MQTT
  char mqttHost[65];
  uint16_t mqttPort;
  char mqttUser[65];
  char mqttPass[65];
  char mqttClientId[65];
  char topicBase[97];
  uint16_t mqttKeepalive;
  uint16_t statusInterval;
  bool statusRetain;
  bool publishOnChange;
  uint8_t mqttLogLevel;
  // Relays
  bool relayActiveHigh;
  bool interlock;
  uint16_t shortMs;
  uint16_t longMs;
  uint16_t gapMs;
  uint16_t maxOnSec;
  // Shutters
  char shutterName[SHUTTER_COUNT][33];
  bool shutterInvert[SHUTTER_COUNT];
  uint8_t stopMode;
  uint16_t travelSec;
  // Connection watchdog
  uint16_t rebootNoWifiMin;
  uint16_t rebootNoMqttMin;
  uint16_t rebootEveryH;
  // System
  char webPass[65];
  char ntpServer[65];
  char tz[65];
  uint8_t logLevel;
  // Display
  uint8_t dispMode;
  uint8_t dispBrightness;
  uint8_t dispPageSec;
  bool dispEvents;
  uint8_t dispOrder;
};

enum FieldType : uint8_t { FT_STR, FT_SECRET, FT_BOOL, FT_U8, FT_U16 };

enum : uint8_t {
  FF_REBOOT   = 0x01,  // takes effect after a reboot
  FF_LOCAL    = 0x02,  // not changeable via MQTT (network / security settings)
  FF_IP       = 0x04,  // empty or IPv4 address
  FF_HOSTNAME = 0x08,  // empty or valid hostname
  FF_TOPIC    = 0x10,  // MQTT topic without wildcards
  FF_WPA      = 0x20,  // empty or 8..63 characters
};

struct FieldDef {
  const char* key;
  const char* label;
  const char* group;
  FieldType type;
  uint8_t flags;
  uint16_t offset;
  uint16_t size;        // buffer size for strings
  uint16_t min, max;    // numeric range
  const char* options;  // "A|B|C": rendered as select, value = index
  const char* help;
};

extern Config cfg;
// Placeholder used instead of real secrets when exporting the config.
// Importing it leaves the stored secret unchanged.
extern const char* const SECRET_KEEP;

void configDefaults(Config& c);
void configLoad();
bool configSave();
void configErase();

const FieldDef* configFields(size_t& count);
const FieldDef* configFindField(const char* key);
bool configSetValue(Config& c, const FieldDef& f, const char* value, String& err);
String configGetValue(const Config& c, const FieldDef& f);
void configToJson(const Config& c, JsonObject obj, bool includeSecrets);
// Applies every key of obj to c. Invalid keys/values are skipped and reported
// in err. remote = true rejects FF_LOCAL fields. Returns the number of errors.
int configApplyJson(Config& c, JsonObjectConst obj, bool remote, String& err);
bool configNeedsReboot(const Config& a, const Config& b);

// Derived values (defaults are MAC based and therefore not stored)
const char* deviceMacHex();  // "24A160123456"
const char* deviceMacStr();  // "24:A1:60:12:34:56"
String cfgDefaultHostname();
String cfgDefaultTopicBase();
String cfgDefaultClientId();
String cfgHostname();
String cfgTopicBase();
String cfgClientId();
String cfgShutterName(uint8_t idx);
