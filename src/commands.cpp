#include "commands.h"
#include <ArduinoJson.h>
#include <memory>
#include "board.h"
#include "config.h"
#include "display.h"
#include "log.h"
#include "net.h"
#include "relays.h"
#include "shutters.h"
#include "status.h"
#include "system.h"

namespace {

const char HELP_TEXT[] =
    "Relay:   <1-4>,SHORT|LONG|ON|OFF   <1-4>,PRESS,<ms>   <1-4>,<ms>   ALL,OFF\n"
    "Shutter: S<1-2>,UP|DOWN[,SHORT|LONG|<ms>]   S<1-2>,STOP   (OPEN/CLOSE = UP/DOWN)\n"
    "System:  STATUS  REBOOT  CONFIG  SET <key> <value>  GET <key>  PORTAL  PING  HELP  FACTORYRESET\n"
    "Display: SHOW <text>   (5 s; '|' starts the next frame, e.g. SHOW HELLO|123456)\n"
    "JSON:    {\"relay\":1,\"action\":\"long\"}  {\"relay\":2,\"action\":\"press\",\"ms\":800}\n"
    "         {\"shutter\":1,\"action\":\"up\",\"press\":\"short\"}  {\"cmd\":\"status\"}\n"
    "         {\"config\":{\"shortMs\":300}}  [ several commands ]";

constexpr long MIN_PRESS_MS = 20;
constexpr long MAX_PRESS_MS = 600000;
constexpr long NO_MS = -1;   // no duration given
constexpr long BAD_MS = -2;  // duration given but not a number

CmdResult ok(const String& m) {
  CmdResult r;
  r.ok = true;
  r.msg = m;
  return r;
}

CmdResult fail(const String& m) {
  CmdResult r;
  r.ok = false;
  r.msg = m;
  return r;
}

bool isNumber(const String& s) {
  if (s.isEmpty()) return false;
  for (size_t i = 0; i < s.length(); i++) {
    if (!isdigit((unsigned char)s[i])) return false;
  }
  return true;
}

// Validates a custom press duration
bool checkDuration(long ms, String& err) {
  if (ms == BAD_MS) {
    err = "duration (ms) must be a number";
    return false;
  }
  long maxMs = cfg.maxOnSec ? (long)cfg.maxOnSec * 1000L : MAX_PRESS_MS;
  if (ms < MIN_PRESS_MS || ms > maxMs) {
    err = String("duration must be ") + MIN_PRESS_MS + ".." + maxMs + " ms";
    return false;
  }
  return true;
}

// Parses "1".."4" / "R1".."R4" -> 0..3, "ALL" -> -1, invalid -> -2
int parseRelay(String s) {
  s.trim();
  s.toUpperCase();
  if (s == "ALL") return -1;
  if (s.startsWith("R")) s.remove(0, 1);
  if (!isNumber(s)) return -2;
  long n = s.toInt();
  return (n >= 1 && n <= RELAY_COUNT) ? (int)(n - 1) : -2;
}

// Parses "1".."2" / "S1".."S2" -> 0..1, invalid -> -1
int parseShutter(String s) {
  s.trim();
  s.toUpperCase();
  if (s.startsWith("S")) s.remove(0, 1);
  if (!isNumber(s)) return -1;
  long n = s.toInt();
  return (n >= 1 && n <= SHUTTER_COUNT) ? (int)(n - 1) : -1;
}

CmdResult doRelay(int idx, String action, long ms) {
  action.trim();
  action.toUpperCase();
  if (isNumber(action)) {  // {"relay":1,"press":800}
    ms = action.toInt();
    action = "PRESS";
  }
  if (relaysLocked() && action != "OFF") return fail("relays locked (firmware update in progress)");

  if (idx == -1) {
    if (action != "OFF") return fail("only OFF is supported for ALL");
    relaysAllOff();
    displayEvent("AL oFF");
    return ok("all relays off");
  }
  if (idx < 0 || idx >= RELAY_COUNT) return fail("invalid relay (1-4)");
  uint8_t ch = (uint8_t)idx;
  String name = String("R") + (ch + 1);

  String ev = String("r") + (ch + 1) + " ";  // display event, e.g. "r1 Lo"
  if (action == "ON") {
    if (!relayOn(ch)) return fail(name + ": rejected");
    displayEvent(ev + "on");
    return ok(name + " on");
  }
  if (action == "OFF") {
    if (!relayOff(ch)) return fail(name + ": rejected");
    displayEvent(ev + "oFF");
    return ok(name + " off");
  }

  if (action == "SHORT" || action == "S") {
    ms = cfg.shortMs;
    ev += "Sh";
  } else if (action == "LONG" || action == "L") {
    ms = cfg.longMs;
    ev += "Lo";
  } else if (action == "PRESS" || action == "PULSE" || action.isEmpty()) {
    if (ms == NO_MS) return fail("missing duration, e.g. 1,PRESS,800");
    String err;
    if (!checkDuration(ms, err)) return fail(err);
    ev += "Pr";
  } else {
    return fail("unknown relay action '" + action + "' (SHORT, LONG, ON, OFF, PRESS)");
  }
  if (!relayPress(ch, (uint32_t)ms)) return fail(name + ": rejected (queue full?)");
  displayEvent(ev);
  return ok(name + " press " + ms + " ms");
}

CmdResult doShutter(int idx, String action, String press, long ms) {
  action.trim();
  action.toUpperCase();
  press.trim();
  press.toUpperCase();
  if (ms == NO_MS && isNumber(press)) ms = press.toInt();  // {"shutter":1,"action":"up","press":800}
  if (idx < 0 || idx >= SHUTTER_COUNT) return fail("invalid shutter (S1-S2)");
  if (relaysLocked()) return fail("relays locked (firmware update in progress)");

  String msg;
  if (action == "STOP") return shutterStop(idx, msg) ? ok(msg) : fail(msg);

  ShutterDir dir;
  if (action == "UP" || action == "OPEN")
    dir = DIR_UP;
  else if (action == "DOWN" || action == "CLOSE")
    dir = DIR_DOWN;
  else
    return fail("unknown shutter action '" + action + "' (UP, DOWN, STOP)");

  uint32_t dur = cfg.longMs;
  bool isLong = true;
  if (ms != NO_MS) {
    String err;
    if (!checkDuration(ms, err)) return fail(err);
    dur = (uint32_t)ms;
    isLong = dur >= (uint32_t)(cfg.shortMs + cfg.longMs) / 2;
  } else if (press.isEmpty() || press == "LONG" || press == "L") {
    // defaults
  } else if (press == "SHORT" || press == "S" || press == "STEP") {
    dur = cfg.shortMs;
    isLong = false;
  } else {
    return fail("unknown press type '" + press + "' (SHORT, LONG or ms)");
  }
  return shutterMove(idx, dir, dur, isLong, msg) ? ok(msg) : fail(msg);
}

CmdResult applyConfigJson(JsonObjectConst obj, CmdSource src) {
  std::unique_ptr<Config> tmp(new Config(cfg));
  String err;
  if (configApplyJson(*tmp, obj, src == SRC_MQTT, err)) return fail(err);  // all or nothing
  bool reboot = configNeedsReboot(cfg, *tmp);
  cfg = *tmp;
  if (!configSave()) return fail("saving config failed");
  systemApplyConfig();
  return ok(reboot ? "saved, reboot required to apply" : "saved and applied");
}

CmdResult doSet(const String& args, CmdSource src) {
  int sp = args.indexOf(' ');
  String key = sp < 0 ? args : args.substring(0, sp);
  String value = sp < 0 ? String() : args.substring(sp + 1);
  key.trim();
  const FieldDef* f = configFindField(key.c_str());
  if (!f) return fail("unknown setting '" + key + "'");
  if (src == SRC_MQTT && (f->flags & FF_LOCAL)) return fail(String(f->key) + ": not changeable via MQTT");

  std::unique_ptr<Config> tmp(new Config(cfg));
  String err;
  if (!configSetValue(*tmp, *f, value.c_str(), err)) return fail(err);
  bool reboot = configNeedsReboot(cfg, *tmp);
  cfg = *tmp;
  if (!configSave()) return fail("saving config failed");
  systemApplyConfig();
  String shown = f->type == FT_SECRET ? String("***") : configGetValue(cfg, *f);
  return ok(String(f->key) + " = " + shown + (reboot ? " (reboot required to apply)" : ""));
}

CmdResult doGet(String key) {
  key.trim();
  const FieldDef* f = configFindField(key.c_str());
  if (!f) return fail("unknown setting '" + key + "'");
  if (f->type == FT_SECRET) return ok(String(f->key) + " = " + (configGetValue(cfg, *f).isEmpty() ? "(empty)" : "***"));
  return ok(String(f->key) + " = " + configGetValue(cfg, *f));
}

String configDump() {
  JsonDocument doc;
  configToJson(cfg, doc.to<JsonObject>(), false);
  String s;
  serializeJson(doc, s);
  return s;
}

CmdResult execText(String line, CmdSource src) {
  line.trim();
  if (line.isEmpty()) return fail("empty command");

  String upper = line;
  upper.toUpperCase();
  if (upper.startsWith("SET ")) return doSet(line.substring(4), src);
  if (upper.startsWith("GET ")) return doGet(line.substring(4));
  if (upper.startsWith("SHOW ")) {
    String text = line.substring(5);
    displayShow(text, 5000);
    return ok("display: " + text);
  }

  // Tokenize
  String tok[4];
  int n = 0;
  size_t i = 0;
  auto isSep = [](char c) { return c == ',' || c == ';' || c == ' ' || c == '\t'; };
  while (i < line.length()) {
    while (i < line.length() && isSep(line[i])) i++;
    if (i >= line.length()) break;
    size_t start = i;
    while (i < line.length() && !isSep(line[i])) i++;
    if (n >= 4) return fail("too many arguments");
    tok[n++] = line.substring(start, i);
  }
  if (n == 0) return fail("empty command");

  String t0 = tok[0];
  t0.toUpperCase();

  if (n == 1) {
    if (t0 == "STATUS") {
      statusRequest("request");
      return ok("status published");
    }
    if (t0 == "REBOOT" || t0 == "RESTART") {
      systemRequestRestart("command");
      return ok("rebooting");
    }
    if (t0 == "HELP" || t0 == "?") return ok(HELP_TEXT);
    if (t0 == "CONFIG") return ok(configDump());
    if (t0 == "PING") return ok("pong");
    if (t0 == "PORTAL" || t0 == "AP") {
      netStartPortal(PORTAL_BUTTON_MS);
      return ok("config access point '" + cfgHostname() + "' opened for 10 minutes");
    }
    if (t0 == "FACTORYRESET") {
      if (src == SRC_MQTT) return fail("not allowed via MQTT");
      systemFactoryReset();
      return ok("config erased, rebooting");
    }
    if (t0 == "OFF" || t0 == "ALLOFF") return doRelay(-1, "OFF", NO_MS);
  }

  // Shutter: S1,UP[,SHORT|LONG|ms]
  if (t0.length() >= 2 && t0[0] == 'S' && isNumber(t0.substring(1))) {
    int idx = parseShutter(t0);
    if (n < 2) return fail("missing action (UP, DOWN, STOP)");
    String press;
    long ms = NO_MS;
    if (n >= 3) {
      if (isNumber(tok[2]))
        ms = tok[2].toInt();
      else
        press = tok[2];
    }
    if (n > 3) return fail("too many arguments");
    return doShutter(idx, tok[1], press, ms);
  }

  // Relay: 1,LONG / 1,PRESS,800 / 1,800 / ALL,OFF
  int idx = parseRelay(t0);
  if (idx == -2) return fail("unknown command '" + tok[0] + "', try HELP");
  if (n < 2) return fail("missing action (SHORT, LONG, ON, OFF, PRESS)");
  if (n > 3) return fail("too many arguments");
  if (isNumber(tok[1])) {
    if (n > 2) return fail("too many arguments");
    return doRelay(idx, "PRESS", tok[1].toInt());
  }
  long ms = NO_MS;
  if (n == 3) {
    if (!isNumber(tok[2])) return fail("duration must be a number in ms");
    ms = tok[2].toInt();
  }
  return doRelay(idx, tok[1], ms);
}

String jsonStr(JsonVariantConst v) {
  if (v.is<const char*>()) return v.as<const char*>();
  if (v.is<long>()) return String(v.as<long>());
  return String();
}

CmdResult execJsonObject(JsonObjectConst o, CmdSource src) {
  if (o["config"].is<JsonObjectConst>()) return applyConfigJson(o["config"].as<JsonObjectConst>(), src);

  String action = jsonStr(o["action"]);
  String press = jsonStr(o["press"]);
  long ms = NO_MS;
  if (!o["ms"].isNull()) {
    String t = jsonStr(o["ms"]);
    ms = isNumber(t) ? t.toInt() : BAD_MS;
  }

  if (!o["relay"].isNull()) {
    int idx = parseRelay(jsonStr(o["relay"]));
    if (idx == -2) return fail("invalid relay (1-4 or \"all\")");
    if (action.isEmpty()) action = press;  // {"relay":1,"press":"long"}
    return doRelay(idx, action, ms);
  }
  if (!o["shutter"].isNull()) {
    int idx = parseShutter(jsonStr(o["shutter"]));
    return doShutter(idx, action, press, ms);
  }
  if (o["cmd"].is<const char*>()) return execText(o["cmd"].as<const char*>(), src);
  if (o["show"].is<const char*>()) {  // {"show":"HELLO","ms":5000}
    long showMs = ms == NO_MS ? 5000 : ms;
    if (showMs < 100 || showMs > 600000) return fail("ms must be 100..600000");
    displayShow(o["show"].as<const char*>(), (uint32_t)showMs);
    return ok(String("display: ") + o["show"].as<const char*>());
  }
  return fail("unknown JSON command (expected relay, shutter, cmd, show or config)");
}

}  // namespace

String commandRedact(const String& cmd) {
  String t = cmd;
  t.trim();
  String upper = t;
  upper.toUpperCase();
  if (upper.indexOf("PASS") < 0) return t;  // all secret keys end in "Pass"
  if (upper.startsWith("SET ")) {
    String rest = t.substring(4);
    rest.trim();
    int sp = rest.indexOf(' ');
    return "SET " + (sp < 0 ? rest : rest.substring(0, sp)) + " ***";
  }
  return "(command containing a secret, redacted)";
}

const char* cmdSourceName(CmdSource src) {
  switch (src) {
    case SRC_MQTT:
      return "mqtt";
    case SRC_WEB:
      return "web";
    case SRC_SERIAL:
      return "serial";
  }
  return "?";
}

CmdResult commandExecute(const char* payload, size_t len, CmdSource src) {
  if (len > CMD_MAX_LEN) {
    LOGW("CMD", "[%s] command too long (%u bytes)", cmdSourceName(src), (unsigned)len);
    return fail("command too long");
  }
  String s(payload, len);
  s.trim();
  LOGI("CMD", "[%s] %s", cmdSourceName(src), commandRedact(s).c_str());

  CmdResult r;
  if (s.startsWith("{") || s.startsWith("[")) {
    JsonDocument doc;
    DeserializationError e = deserializeJson(doc, s);
    if (e) {
      r = fail(String("invalid JSON: ") + e.c_str());
    } else if (doc.is<JsonArrayConst>()) {
      String msgs;
      bool allOk = true;
      for (JsonVariantConst item : doc.as<JsonArrayConst>()) {
        CmdResult one;
        if (item.is<JsonObjectConst>())
          one = execJsonObject(item.as<JsonObjectConst>(), src);
        else if (item.is<const char*>())
          one = execText(item.as<const char*>(), src);
        else
          one = fail("invalid array element");
        allOk &= one.ok;
        if (msgs.length()) msgs += "; ";
        msgs += one.msg;
      }
      r = allOk ? ok(msgs) : fail(msgs);
    } else if (doc.is<JsonObjectConst>()) {
      r = execJsonObject(doc.as<JsonObjectConst>(), src);
    } else {
      r = fail("invalid JSON command");
    }
  } else {
    r = execText(s, src);
  }

  int nl = r.msg.indexOf('\n');
  int shown = nl < 0 ? (int)r.msg.length() : nl;
  if (r.ok)
    LOGI("CMD", "-> %.*s", shown, r.msg.c_str());
  else
    LOGW("CMD", "-> error: %.*s", shown, r.msg.c_str());
  if (!r.ok) displayEvent("c Err");
  return r;
}
