#include "web.h"
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_task_wdt.h>
#include <memory>
#include "board.h"
#include "commands.h"
#include "config.h"
#include "display.h"
#include "help_html.h"
#include "log.h"
#include "net.h"
#include "relays.h"
#include "status.h"
#include "system.h"

namespace {

WebServer server(80);
bool otaAuthorized = false;
String otaError;
int32_t otaBytes = -1;  // -1 = no update running

const char CSS[] PROGMEM = R"CSS(
*{box-sizing:border-box}body{font-family:system-ui,-apple-system,Segoe UI,Roboto,sans-serif;margin:0;background:#f3f4f6;color:#111}
header{background:#1f2937;color:#fff;padding:10px 16px;display:flex;gap:18px;align-items:center;flex-wrap:wrap}
header b{font-size:1.1em}header a{color:#cbd5e1;text-decoration:none}header a:hover{color:#fff}
main{max-width:920px;margin:auto;padding:8px 12px}
.card{background:#fff;border-radius:10px;padding:12px 16px;margin:12px 0;box-shadow:0 1px 3px #0002}
h2{font-size:1.05em;margin:4px 0 10px}
button{padding:8px 12px;border:0;border-radius:6px;background:#2563eb;color:#fff;cursor:pointer;margin:2px;font-size:.95em}
button:hover{filter:brightness(1.1)}button.sec{background:#6b7280}button.warn{background:#dc2626}
.row{display:flex;flex-wrap:wrap;align-items:center;gap:8px;padding:6px 0;border-bottom:1px solid #eee}
.name{min-width:150px;font-weight:600}.badge{min-width:70px;text-align:center;padding:2px 8px;border-radius:10px;background:#e5e7eb;font-size:.85em}
.badge.on,.badge.opening,.badge.closing{background:#16a34a;color:#fff}.badge.open{background:#bfdbfe}.badge.closed{background:#c7d2fe}
table{border-collapse:collapse;width:100%}td{padding:4px 6px;border-bottom:1px solid #eee;vertical-align:top;white-space:pre-line}td:first-child{color:#555;width:35%}
input,select{padding:7px;width:100%;border:1px solid #ccc;border-radius:6px;font-size:.95em}
input[type=checkbox]{width:auto}label{display:block;margin:10px 0 2px;font-weight:600}
small{color:#666}fieldset{border:1px solid #ddd;border-radius:8px;margin:12px 0;padding:6px 14px 12px}legend{font-weight:700;padding:0 6px}
pre{background:#111827;color:#d1d5db;padding:8px;height:320px;overflow:auto;font-size:12px;border-radius:6px;white-space:pre-wrap}
.toc{display:flex;flex-wrap:wrap;gap:4px 14px;margin:0 0 10px}.md h1{font-size:1.3em}
.md h2{font-size:1.15em;margin-top:24px;padding-bottom:4px;border-bottom:1px solid #ddd}.md h3{font-size:1em;margin-top:16px}
.md pre{height:auto;background:#f3f4f6;color:#111;border:1px solid #ddd}
.md code{background:#eef2f7;padding:1px 4px;border-radius:4px;white-space:pre;font-size:.9em}
.md th{text-align:left;padding:4px 6px;border-bottom:2px solid #ccc}.md td:first-child{width:auto;color:inherit}.md li{margin:3px 0}
footer{text-align:center;color:#888;font-size:.8em;margin:18px 0 10px}footer a{color:#888}
.ok{color:#16a34a}.err{color:#dc2626}.flex{display:flex;gap:6px}.flex input{flex:1}
)CSS";

const char DASHBOARD[] PROGMEM = R"HTML(
<div class=card><h2>Shutters</h2><div id=sh>loading...</div></div>
<div class=card><h2>Relays</h2><div id=rl></div></div>
<div class=card><h2>Command</h2>
<form id=cf class=flex><input id=ci placeholder="e.g. 1,LONG  |  S1,UP  |  S2,DOWN,SHORT  |  HELP" autocomplete=off><button>Send</button></form>
<div id=cr style="white-space:pre-wrap;margin-top:6px"></div></div>
<div class=card><h2>Status</h2><table id=st></table></div>
<div class=card><h2>Log</h2><pre id=lg></pre></div>
<script>
const $=i=>document.getElementById(i);let built=0,seq=0;
const esc=s=>String(s).replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
async function cmd(c){try{const r=await fetch('/api/cmd',{method:'POST',body:c});const j=await r.json();
$('cr').innerHTML='<span class='+(j.ok?'ok':'err')+'>'+(j.ok?'OK':'Error')+'</span> '+esc(j.msg)}catch(e){$('cr').textContent='Error: '+e}setTimeout(poll,200)}
const b=(l,c,k)=>`<button class="${k||''}" onclick="cmd('${c}')">${l}</button>`;
function build(s){let h='';s.shutters.forEach(x=>{const p='S'+x.id;
h+=`<div class=row><span class=name>${esc(x.name)}</span><span class=badge id=ss${x.id}></span><span>`+b('&#9650; Up',p+',UP')+b('&#9650; step',p+',UP,SHORT','sec')+b('&#9632; Stop',p+',STOP','warn')+b('&#9660; step',p+',DOWN,SHORT','sec')+b('&#9660; Down',p+',DOWN')+'</span></div>'});
$('sh').innerHTML=h;h='';s.relays.forEach((v,i)=>{const n=i+1;
h+=`<div class=row><span class=name>Relay ${n} <small>GPIO ${s.pins[i]}</small></span><span class=badge id=rs${n}></span><span>`+b('Short',n+',SHORT')+b('Long',n+',LONG')+b('On',n+',ON','sec')+b('Off',n+',OFF','sec')+'</span></div>'});
$('rl').innerHTML=h+'<div class=row>'+b('All off','ALL,OFF','warn')+'</div>';built=1}
function render(s){if(!built)build(s);
s.shutters.forEach(x=>{const e=$('ss'+x.id);e.textContent=x.state;e.className='badge '+x.state});
s.relays.forEach((v,i)=>{const e=$('rs'+(i+1));e.textContent=v?'ON':'off';e.className='badge'+(v?' on':'')});
const w=s.wifi,m=s.mqtt,y=s.sys;const rows=[['Uptime',s.uptimeStr],['Time',s.time||'not synced'],
['WiFi',w.connected?`${w.ssid} (${w.rssi} dBm, ch ${w.ch})`:'disconnected'],['IP',w.ip||'-'],
['Config AP',w.ap?`open, ${w.apClients} client(s)`:'off'],['MQTT',m.enabled?(m.connected?'connected':'disconnected'):'disabled'],
['Base topic',m.base],['Reconnects',`WiFi ${w.reconnects}, MQTT ${m.reconnects}`],['Free heap',`${y.heap} (min ${y.minHeap})`],
['Boot',`#${y.boots}, ${y.resetReason}${y.lastRestart?' ('+y.lastRestart+')':''}`],['Firmware',s.fw+' / '+y.sdk],['MAC',y.mac],
['Errors',s.errors.length?s.errors.map(e=>'Err '+String(e.code).padStart(2,'0')+' '+e.msg).join('\n'):'none'],
['Display',s.display?(s.display.ok?'"'+s.display.text+'"':'not responding'):'not installed']];
$('st').innerHTML=rows.map(r=>`<tr><td>${r[0]}</td><td>${esc(r[1])}</td></tr>`).join('')}
async function poll(){try{render(await(await fetch('/api/status')).json())}catch(e){}}
async function logs(){try{const j=await(await fetch('/api/log?after='+seq)).json();if(j.lines.length){const p=$('lg'),
end=p.scrollTop+p.clientHeight>=p.scrollHeight-8;p.textContent+=j.lines.join('\n')+'\n';
if(p.textContent.length>60000)p.textContent=p.textContent.slice(-40000);if(end)p.scrollTop=p.scrollHeight}seq=j.seq}catch(e){}}
$('cf').onsubmit=e=>{e.preventDefault();const v=$('ci').value.trim();if(v)cmd(v)};
poll();logs();setInterval(poll,2000);setInterval(logs,2000);
</script>
)HTML";

const char SCAN_JS[] PROGMEM = R"HTML(
<script>
async function scan(){const s=document.getElementById('scanst');s.textContent='scanning...';
for(let i=0;i<15;i++){const j=await(await fetch('/api/scan')).json();
if(j.networks){document.getElementById('ssids').innerHTML=j.networks.map(n=>`<option value="${n.ssid.replace(/"/g,'&quot;')}">${n.rssi} dBm${n.open?' open':''}</option>`).join('');
s.textContent=j.networks.length+' networks found - click the SSID field';return}await new Promise(r=>setTimeout(r,1000))}
s.textContent='scan failed'}
</script>
)HTML";

// Collects small pieces and sends them as larger HTTP chunks
class Chunked {
 public:
  explicit Chunked(int code = 200, const char* type = "text/html") {
    buf_.reserve(1600);
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(code, type, "");
  }
  Chunked& operator<<(const char* s) {
    buf_ += s;
    if (buf_.length() > 1400) flush();
    return *this;
  }
  Chunked& operator<<(const String& s) { return *this << s.c_str(); }
  void addP(PGM_P s) {
    flush();
    server.sendContent_P(s);
  }
  void end() {
    flush();
    server.sendContent("");
  }

 private:
  void flush() {
    if (buf_.length()) server.sendContent(buf_);
    buf_ = "";
  }
  String buf_;
};

String esc(const String& s) {
  String o;
  o.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    switch (c) {
      case '&':
        o += "&amp;";
        break;
      case '<':
        o += "&lt;";
        break;
      case '>':
        o += "&gt;";
        break;
      case '"':
        o += "&quot;";
        break;
      case '\'':
        o += "&#39;";
        break;
      default:
        o += c;
    }
  }
  return o;
}

void pageStart(Chunked& out, const char* title) {
  out << "<!DOCTYPE html><html><head><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
      << "<title>" << esc(cfgHostname()) << " - " << title << "</title><style>";
  out.addP(CSS);
  out << "</style></head><body><header><b>" << esc(cfgHostname()) << "</b>"
      << "<a href=/>Dashboard</a><a href=/config>Config</a><a href=/update>Update</a><a href=/help>Help</a></header><main>";
}

void pageEnd(Chunked& out) {
  out << "<footer>" FW_NAME " " FW_VERSION " &middot; developed by Philipp Albrecht "
         "(<a href='mailto:philipp@uisa.ch'>philipp@uisa.ch</a>) using Claude Opus 5.5 &middot; "
         "<a href='" FW_URL "' target=_blank>uisa.ch</a></footer>";
  out << "</main></body></html>";
  out.end();
}

void sendMessage(int code, const char* title, const String& html, const char* refreshTo = nullptr, int sec = 0) {
  Chunked out(code);
  pageStart(out, title);
  if (refreshTo) out << "<meta http-equiv=refresh content='" << String(sec) << ";url=" << refreshTo << "'>";
  out << "<div class=card><h2>" << title << "</h2><p>" << html << "</p><p><a href=/>Back</a></p></div>";
  pageEnd(out);
}

void sendJson(int code, const JsonDocument& doc) {
  String s;
  serializeJson(doc, s);
  server.send(code, "application/json", s);
}

bool auth() {
  if (!cfg.webPass[0]) return true;
  if (server.authenticate("admin", cfg.webPass)) return true;
  server.requestAuthentication(BASIC_AUTH, "relay4");
  return false;
}

// In AP mode, redirect requests for foreign hosts to the portal (captive portal)
bool captiveRedirect() {
  if (!netApActive()) return false;
  String host = server.hostHeader();
  int colon = host.indexOf(':');
  if (colon >= 0) host.remove(colon);
  IPAddress ip;
  String name = cfgHostname();
  if (host.isEmpty() || ip.fromString(host) || host.equalsIgnoreCase(name) || host.equalsIgnoreCase(name + ".local"))
    return false;
  server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/config", true);
  server.send(302, "text/plain", "");
  return true;
}

// ---------------------------------------------------------------------------
// Pages
// ---------------------------------------------------------------------------
void handleRoot() {
  if (captiveRedirect() || !auth()) return;
  Chunked out;
  pageStart(out, "Dashboard");
  out.addP(DASHBOARD);
  pageEnd(out);
}

String placeholderFor(const char* key) {
  if (!strcmp(key, "hostname")) return cfgDefaultHostname();
  if (!strcmp(key, "topicBase")) return cfgDefaultTopicBase();
  if (!strcmp(key, "mqttClientId")) return cfgDefaultClientId();
  if (!strcmp(key, "s1Name")) return "Shutter 1";
  if (!strcmp(key, "s2Name")) return "Shutter 2";
  return String();
}

void renderField(Chunked& out, const FieldDef& f) {
  String key = f.key;
  String val = configGetValue(cfg, f);

  if (f.type == FT_BOOL) {
    out << "<label><input type=checkbox name=" << key << " value=1" << (val == "1" ? " checked" : "") << "> "
        << esc(f.label) << "</label>";
  } else {
    out << "<label for=" << key << ">" << esc(f.label) << "</label>";
    if (f.options) {
      out << "<select id=" << key << " name=" << key << ">";
      int idx = 0;
      const char* p = f.options;
      while (*p) {
        const char* e = strchr(p, '|');
        String opt = e ? String(p).substring(0, e - p) : String(p);
        out << "<option value=" << String(idx) << (val.toInt() == idx ? " selected" : "") << ">" << esc(opt)
            << "</option>";
        idx++;
        if (!e) break;
        p = e + 1;
      }
      out << "</select>";
    } else if (f.type == FT_U8 || f.type == FT_U16) {
      out << "<input type=number id=" << key << " name=" << key << " min=" << String(f.min) << " max=" << String(f.max)
          << " value=" << val << ">";
    } else if (f.type == FT_SECRET) {
      out << "<input type=password id=" << key << " name=" << key << " maxlength=" << String(f.size - 1)
          << " autocomplete=new-password value=\"" << (val.isEmpty() ? "" : SECRET_KEEP) << "\">";
    } else if (key == "wifiSsid") {
      out << "<div class=flex><input id=wifiSsid name=wifiSsid list=ssids maxlength=" << String(f.size - 1)
          << " value=\"" << esc(val) << "\"><button type=button class=sec onclick=scan()>Scan</button></div>"
          << "<datalist id=ssids></datalist><small id=scanst></small>";
    } else {
      String ph = placeholderFor(f.key);
      out << "<input id=" << key << " name=" << key << " maxlength=" << String(f.size - 1) << " value=\"" << esc(val)
          << "\"" << (ph.isEmpty() ? String() : String(" placeholder=\"") + esc(ph) + "\"") << ">";
    }
  }
  String help;
  if (f.help) help = f.help;
  if (f.flags & FF_REBOOT) help += help.isEmpty() ? "Reboot required" : " (reboot required)";
  if (!help.isEmpty()) out << "<small>" << esc(help) << "</small>";
}

void handleConfigGet() {
  if (!auth()) return;
  Chunked out;
  pageStart(out, "Config");
  if (server.hasArg("saved")) out << "<div class=card><b class=ok>Settings saved and applied.</b></div>";
  if (netApActive() && !netStaConnected())
    out << "<div class=card><b>Setup:</b> enter your WiFi and MQTT settings and press Save. The device then reboots "
           "and joins your network as <b>"
        << esc(cfgHostname()) << "</b> (http://" << esc(cfgHostname()) << ".local/).</div>";

  out << "<form method=post action=/config><input type=hidden name=_form value=1><div class=card>";
  size_t n;
  const FieldDef* fields = configFields(n);
  const char* group = nullptr;
  for (size_t i = 0; i < n; i++) {
    if (!group || strcmp(group, fields[i].group) != 0) {
      if (group) out << "</fieldset>";
      group = fields[i].group;
      out << "<fieldset><legend>" << group << "</legend>";
    }
    renderField(out, fields[i]);
  }
  out << "</fieldset><button type=submit>Save</button></div></form>";
  out << "<div class=card><h2>Maintenance</h2>"
         "<form method=post action=/reboot style=display:inline onsubmit=\"return confirm('Reboot now?')\">"
         "<button class=sec>Reboot</button></form>"
         "<form method=post action=/factory style=display:inline onsubmit=\"return confirm('Erase ALL settings?')\">"
         "<button class=warn>Factory reset</button></form>"
         "<p><a href=/api/config>Export config (JSON, secrets hidden)</a> &middot; <a href=/log>Log (text)</a></p>"
         "<small>Restore / bulk change: POST the JSON to /api/config</small></div>";
  out.addP(SCAN_JS);
  pageEnd(out);
}

// Saves *tmp, applies runtime settings and reboots if a network setting changed.
// Returns true if a reboot was scheduled.
bool commitConfig(const Config& tmp) {
  bool reboot = configNeedsReboot(cfg, tmp);
  cfg = tmp;
  configSave();
  systemApplyConfig();
  if (reboot) systemRequestRestart("config", 1500);
  return reboot;
}

void handleConfigPost() {
  if (!auth()) return;
  std::unique_ptr<Config> tmp(new Config(cfg));
  String errors;
  // Only the complete form may clear checkboxes; a partial POST changes just the fields it contains
  bool fullForm = server.hasArg("_form");
  size_t n;
  const FieldDef* fields = configFields(n);
  for (size_t i = 0; i < n; i++) {
    const FieldDef& f = fields[i];
    String v;
    if (f.type == FT_BOOL && fullForm)
      v = server.hasArg(f.key) ? "1" : "0";  // unchecked boxes are not submitted
    else if (server.hasArg(f.key))
      v = server.arg(f.key);
    else
      continue;
    if (f.type == FT_SECRET && v == SECRET_KEEP) continue;
    String e;
    if (!configSetValue(*tmp, f, v.c_str(), e)) errors += esc(e) + "<br>";
  }
  if (errors.length()) {
    sendMessage(400, "Invalid settings", errors + "<br>Nothing was saved. Go back and correct the values.");
    return;
  }
  LOGI("WEB", "Config saved via web");
  if (commitConfig(*tmp)) {
    String host = esc(cfgHostname());
    sendMessage(200, "Saved - rebooting",
                "The device restarts now to apply the network settings.<br>Afterwards open <a href='http://" + host +
                    ".local/'>http://" + host + ".local/</a> or the IP shown in your router.",
                "/", 20);
  } else {
    server.sendHeader("Location", "/config?saved=1");
    server.send(303);
  }
}

void handleUpdateGet() {
  if (!auth()) return;
  Chunked out;
  pageStart(out, "Update");
  out << "<div class=card><h2>Firmware update</h2><p>Running " FW_NAME " " FW_VERSION " (built " __DATE__ " " __TIME__
         ").</p>"
         "<form method=post action=/update enctype=multipart/form-data>"
         "<input type=file name=firmware accept=.bin required><p><button>Upload &amp; flash</button></p></form>"
         "<small>Select .pio/build/esp32dev/firmware.bin. All relays are released during the update. "
         "The device reboots when done.</small></div>";
  pageEnd(out);
}

void handleUpdateUpload() {
  HTTPUpload& up = server.upload();
  switch (up.status) {
    case UPLOAD_FILE_START:
      otaError = "";
      otaAuthorized = !cfg.webPass[0] || server.authenticate("admin", cfg.webPass);
      if (!otaAuthorized) {
        otaError = "unauthorized";
        LOGW("OTA", "Unauthorized update attempt");
        break;
      }
      LOGW("OTA", "Update started: %s", up.filename.c_str());
      relaysSetLocked(true);
      otaBytes = 0;
      if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) otaError = Update.errorString();
      break;
    case UPLOAD_FILE_WRITE:
      if (otaAuthorized && otaError.isEmpty() && Update.write(up.buf, up.currentSize) != up.currentSize)
        otaError = Update.errorString();
      if (otaAuthorized) otaBytes += up.currentSize;
      break;
    case UPLOAD_FILE_END:
      otaBytes = -1;
      if (!otaAuthorized) break;
      if (otaError.isEmpty()) {
        if (Update.end(true))
          LOGI("OTA", "Update written: %u bytes", (unsigned)up.totalSize);
        else
          otaError = Update.errorString();
      } else {
        Update.abort();
      }
      break;
    case UPLOAD_FILE_ABORTED:
      otaBytes = -1;
      // The completion handler is not called for aborted uploads: unlock here
      if (otaAuthorized) {
        Update.abort();
        relaysSetLocked(false);
      }
      otaError = "upload aborted";
      LOGW("OTA", "Upload aborted");
      break;
  }
  // The whole upload is handled inside one handleClient() call, loop() does not run meanwhile
  esp_task_wdt_reset();
  displayLoop();  // show progress
}

void handleUpdatePost() {
  if (!otaAuthorized) {
    server.requestAuthentication(BASIC_AUTH, "relay4");
    return;
  }
  if (otaError.length()) {
    LOGE("OTA", "Update failed: %s", otaError.c_str());
    relaysSetLocked(false);
    sendMessage(500, "Update failed", esc(otaError));
    return;
  }
  sendMessage(200, "Update successful", "Rebooting into the new firmware...", "/", 15);
  systemRequestRestart("ota", 1000);
}

void handleHelp() {
  if (!auth()) return;
  Chunked out;
  pageStart(out, "Help");
  String host = esc(cfgHostname());
  out << "<div class=card><h2>This device</h2><table>"
      << "<tr><td>Hostname</td><td>" << host << " (http://" << host << ".local/)</td></tr>"
      << "<tr><td>IP address</td><td>" << (netStaConnected() ? WiFi.localIP().toString() : String("not connected"))
      << (netApActive() ? " / config AP " + WiFi.softAPIP().toString() : String()) << "</td></tr>"
      << "<tr><td>MQTT base topic</td><td>" << esc(cfgTopicBase()) << "</td></tr>"
      << "<tr><td>MAC</td><td>" << deviceMacStr() << "</td></tr>"
      << "<tr><td>Firmware</td><td>" FW_NAME " " FW_VERSION " (built " __DATE__ ")</td></tr></table>"
      << "<small>In the examples below, replace <code>esp32/relay4-24A160123456</code> and "
         "<code>relay4-123456</code> with the values above.</small></div>";
  out << "<div class='card md'>";
  out.addP(HELP_HTML);
  out << "</div>";
  pageEnd(out);
}

void handleReboot() {
  if (!auth()) return;
  sendMessage(200, "Rebooting", "The device restarts now.", "/", 15);
  systemRequestRestart("web", 1000);
}

void handleFactory() {
  if (!auth()) return;
  sendMessage(200, "Factory reset",
              "All settings were erased. The device restarts and opens the access point <b>" + esc(cfgHostname()) +
                  "</b>.");
  systemFactoryReset();
}

// ---------------------------------------------------------------------------
// API
// ---------------------------------------------------------------------------
void handleApiStatus() {
  if (!auth()) return;
  JsonDocument doc;
  statusBuild(doc, nullptr);
  sendJson(200, doc);
}

void handleApiCmd() {
  if (!auth()) return;
  String body = server.arg("plain");
  if (body.isEmpty() && server.hasArg("c")) body = server.arg("c");  // /api/cmd?c=1,LONG
  CmdResult r = commandExecute(body.c_str(), body.length(), SRC_WEB);
  JsonDocument doc;
  doc["ok"] = r.ok;
  doc["msg"] = r.msg;
  sendJson(r.ok ? 200 : 400, doc);
}

void handleApiConfigGet() {
  if (!auth()) return;
  JsonDocument doc;
  configToJson(cfg, doc.to<JsonObject>(), false);
  sendJson(200, doc);
}

void handleApiConfigPost() {
  if (!auth()) return;
  JsonDocument in;
  JsonDocument out;
  if (deserializeJson(in, server.arg("plain")) || !in.is<JsonObject>()) {
    out["ok"] = false;
    out["msg"] = "expected a JSON object";
    sendJson(400, out);
    return;
  }
  std::unique_ptr<Config> tmp(new Config(cfg));
  String err;
  if (configApplyJson(*tmp, in.as<JsonObjectConst>(), false, err)) {
    out["ok"] = false;
    out["msg"] = err;
    sendJson(400, out);
    return;
  }
  bool reboot = commitConfig(*tmp);
  out["ok"] = true;
  out["msg"] = reboot ? "saved, rebooting" : "saved and applied";
  sendJson(200, out);
}

void handleApiLog() {
  if (!auth()) return;
  uint32_t seq = server.hasArg("after") ? strtoul(server.arg("after").c_str(), nullptr, 10) : 0;
  if (seq > logLastSeq()) seq = 0;  // device rebooted since the last poll
  JsonDocument doc;
  JsonArray lines = doc["lines"].to<JsonArray>();
  char line[LOG_LINE_LEN];
  while (logGetNext(seq, line, sizeof(line))) lines.add(line);
  doc["seq"] = seq;
  sendJson(200, doc);
}

void handleLogText() {
  if (!auth()) return;
  Chunked out(200, "text/plain");
  uint32_t seq = 0;
  char line[LOG_LINE_LEN];
  while (logGetNext(seq, line, sizeof(line))) out << line << "\n";
  out.end();
}

void handleApiScan() {
  if (!auth()) return;
  server.send(200, "application/json", netScanJson());
}

void handleNotFound() {
  if (captiveRedirect()) return;
  server.send(404, "text/plain", "Not found");
}

}  // namespace

void webInit() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/config", HTTP_GET, handleConfigGet);
  server.on("/config", HTTP_POST, handleConfigPost);
  server.on("/update", HTTP_GET, handleUpdateGet);
  server.on("/update", HTTP_POST, handleUpdatePost, handleUpdateUpload);
  server.on("/help", HTTP_GET, handleHelp);
  server.on("/reboot", HTTP_POST, handleReboot);
  server.on("/factory", HTTP_POST, handleFactory);
  server.on("/log", HTTP_GET, handleLogText);
  server.on("/api/status", HTTP_GET, handleApiStatus);
  server.on("/api/cmd", HTTP_POST, handleApiCmd);
  server.on("/api/config", HTTP_GET, handleApiConfigGet);
  server.on("/api/config", HTTP_POST, handleApiConfigPost);
  server.on("/api/log", HTTP_GET, handleApiLog);
  server.on("/api/scan", HTTP_GET, handleApiScan);
  server.on("/favicon.ico", HTTP_GET, []() { server.send(204); });
  server.onNotFound(handleNotFound);
  server.begin();
  LOGI("WEB", "Web server started on port 80%s", cfg.webPass[0] ? " (password protected)" : "");
}

void webLoop() { server.handleClient(); }

int32_t webOtaProgressKB() { return otaBytes < 0 ? -1 : otaBytes / 1024; }
