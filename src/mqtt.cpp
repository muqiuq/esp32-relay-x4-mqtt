#include "mqtt.h"
#include <PubSubClient.h>
#include <WiFi.h>
#include "board.h"
#include "commands.h"
#include "config.h"
#include "log.h"
#include "status.h"

namespace {

WiFiClient net;
PubSubClient client(net);

String base, tCmd, tAvail;
bool wasConnected = false;
uint32_t lastAttempt = 0;
uint32_t backoff = 0;
uint32_t downSince = 0;
uint32_t connects = 0;
uint32_t logSeq = 0;

String pendingCmd;
bool hasPending = false;

constexpr uint32_t BACKOFF_MIN_MS = 2000;
constexpr uint32_t BACKOFF_MAX_MS = 60000;
constexpr int LOG_LINES_PER_LOOP = 5;

const char* stateName(int s) {
  switch (s) {
    case MQTT_CONNECTION_TIMEOUT:
      return "timeout";
    case MQTT_CONNECTION_LOST:
      return "connection lost";
    case MQTT_CONNECT_FAILED:
      return "connect failed";
    case MQTT_DISCONNECTED:
      return "disconnected";
    case MQTT_CONNECTED:
      return "connected";
    case MQTT_CONNECT_BAD_PROTOCOL:
      return "bad protocol";
    case MQTT_CONNECT_BAD_CLIENT_ID:
      return "bad client id";
    case MQTT_CONNECT_UNAVAILABLE:
      return "server unavailable";
    case MQTT_CONNECT_BAD_CREDENTIALS:
      return "bad credentials";
    case MQTT_CONNECT_UNAUTHORIZED:
      return "unauthorized";
  }
  return "unknown";
}

// PubSubClient processes at most one packet per loop() call, so a single slot
// is enough. The command is executed after loop() returns, never inside the
// callback (the payload buffer is reused when publishing).
void onMessage(char* topic, uint8_t* payload, unsigned int len) {
  if (tCmd != topic) return;
  if (len == 0) return;  // our own "clear retained" message or empty payload
  if (len > CMD_MAX_LEN) {
    LOGW("MQTT", "Command too long (%u bytes), ignored", len);
    return;
  }
  if (hasPending) {
    LOGW("MQTT", "Command dropped, previous one still pending");
    return;
  }
  pendingCmd = String(reinterpret_cast<const char*>(payload), len);
  hasPending = true;
}

bool connect() {
  String cid = cfgClientId();
  client.setServer(cfg.mqttHost, cfg.mqttPort);
  client.setKeepAlive(cfg.mqttKeepalive);
  LOGI("MQTT", "Connecting to %s:%u as '%s'...", cfg.mqttHost, cfg.mqttPort, cid.c_str());

  uint32_t t0 = millis();
  const char* user = cfg.mqttUser[0] ? cfg.mqttUser : nullptr;
  const char* pass = user ? cfg.mqttPass : nullptr;
  bool ok = client.connect(cid.c_str(), user, pass, tAvail.c_str(), 1, true, "offline", true);
  uint32_t dt = millis() - t0;
  if (!ok) {
    LOGW("MQTT", "Connect failed after %u ms: %s (%d)", (unsigned)dt, stateName(client.state()), client.state());
    return false;
  }
  LOGI("MQTT", "Connected in %u ms", (unsigned)dt);

  client.publish(tAvail.c_str(), "online", true);
  // Delete a retained command before subscribing, so it can never be replayed
  // on every reconnect (e.g. "1,LONG" published with retain by mistake).
  client.publish(tCmd.c_str(), "", true);
  if (!client.subscribe(tCmd.c_str(), 1)) LOGE("MQTT", "Subscribe to %s failed", tCmd.c_str());
  LOGI("MQTT", "Listening on %s", tCmd.c_str());
  statusRequest("connect");
  return true;
}

void publishResult(const String& cmd, const CmdResult& r) {
  JsonDocument doc;
  doc["ok"] = r.ok;
  String shown = commandRedact(cmd);
  doc["cmd"] = shown.length() > 200 ? shown.substring(0, 200) + "..." : shown;
  doc["msg"] = r.msg;
  mqttPublishJson("result", doc, false);
}

void forwardLogs() {
  if (cfg.mqttLogLevel == LL_OFF) {
    logSeq = logLastSeq();
    return;
  }
  char line[LOG_LINE_LEN];
  uint8_t level;
  int sent = 0;
  // Never log from here: it would feed itself
  while (sent < LOG_LINES_PER_LOOP && logGetNext(logSeq, line, sizeof(line), &level)) {
    if (level > cfg.mqttLogLevel) continue;
    if (!mqttPublish("log", line, strlen(line), false)) break;
    sent++;
  }
}

}  // namespace

void mqttInit() {
  base = cfgTopicBase();
  tCmd = base + "/cmd";
  tAvail = base + "/availability";
  client.setBufferSize(MQTT_BUFFER_SIZE);
  client.setCallback(onMessage);
  client.setSocketTimeout(5);
  downSince = millis();
  if (!mqttEnabled())
    LOGW("MQTT", "No broker configured, MQTT disabled");
  else
    LOGI("MQTT", "Broker %s:%u, base topic '%s'", cfg.mqttHost, cfg.mqttPort, base.c_str());
}

void mqttLoop() {
  if (!mqttEnabled()) return;
  uint32_t now = millis();

  // WiFi gone: drop the socket now instead of writing into a dead connection
  // until the keepalive expires (writes would block the loop for seconds)
  if (!WiFi.isConnected() && client.connected()) net.stop();

  if (client.connected()) {
    client.loop();
    if (hasPending) {
      String cmd = pendingCmd;
      pendingCmd = String();
      hasPending = false;
      CmdResult r = commandExecute(cmd.c_str(), cmd.length(), SRC_MQTT);
      publishResult(cmd, r);
    }
    forwardLogs();
    return;
  }

  if (wasConnected) {
    wasConnected = false;
    downSince = now;
    LOGW("MQTT", "Connection lost: %s (%d)", stateName(client.state()), client.state());
    backoff = 1000;
    lastAttempt = now;
  }
  if (!WiFi.isConnected()) return;
  if (now - lastAttempt < backoff) return;

  if (connect()) {
    wasConnected = true;
    connects++;
    backoff = 0;
  } else {
    backoff = backoff < BACKOFF_MIN_MS ? BACKOFF_MIN_MS : min(backoff * 2, BACKOFF_MAX_MS);
    LOGI("MQTT", "Next attempt in %u s", (unsigned)(backoff / 1000));
  }
  lastAttempt = millis();  // connect() may have blocked for a while
}

bool mqttEnabled() { return cfg.mqttHost[0] != '\0'; }
bool mqttConnected() { return client.connected(); }

bool mqttPublish(const char* subtopic, const char* payload, size_t len, bool retain) {
  if (!client.connected()) return false;
  String topic = base + "/" + subtopic;
  // beginPublish streams the payload, so its size is not limited by the buffer
  if (!client.beginPublish(topic.c_str(), len, retain)) return false;
  size_t written = client.write(reinterpret_cast<const uint8_t*>(payload), len);
  if (!client.endPublish() || written != len) {
    net.stop();  // stream is out of sync or broken: reconnect cleanly
    return false;
  }
  return true;
}

bool mqttPublishJson(const char* subtopic, const JsonDocument& doc, bool retain) {
  String s;
  serializeJson(doc, s);
  return mqttPublish(subtopic, s.c_str(), s.length(), retain);
}

int mqttLastState() { return client.state(); }

uint32_t mqttReconnects() { return connects > 1 ? connects - 1 : 0; }
// While the loss has not been processed by mqttLoop() yet, the outage starts now
uint32_t mqttDownSince() { return wasConnected && !client.connected() ? millis() : downSince; }

void mqttShutdown() {
  if (!client.connected()) return;
  client.publish(tAvail.c_str(), "offline", true);
  client.disconnect();
}
