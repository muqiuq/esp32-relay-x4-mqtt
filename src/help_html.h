// GENERATED from README.md by tools/readme_to_help.py - do not edit
#pragma once
#include <Arduino.h>

static const char HELP_HTML[] PROGMEM = R"HELPHTML(
<nav class="toc"><a href="#first-start">First start</a><a href="#mqtt">MQTT</a><a href="#relay-behaviour">Relay behaviour</a><a href="#resilience">Resilience</a><a href="#web-ui">Web UI</a><a href="#ota-update">OTA update</a><a href="#status-display-tm1637-6-digits">Status display (TM1637, 6 digits)</a><a href="#led-and-boot-button">LED and BOOT button</a><a href="#serial-log">Serial log</a><a href="#home-assistant-example">Home Assistant example</a></nav>
<h1 id="esp32-relay-x4-mqtt-roller-shutter-button-emulator">ESP32 Relay X4 – MQTT roller shutter button emulator</h1>
<p>Developed by <b>Philipp Albrecht</b> <a href="mailto:philipp@uisa.ch">philipp@uisa.ch</a> using Claude Opus 5.5, 2026-10-07 · <a href="https://uisa.ch" target="_blank">https://uisa.ch</a></p>
<p>Firmware for the <b>ESP32_Relay_X4 v1.1</b> board. The four relays emulate the UP/DOWN buttons of two roller shutter controllers (Rolladensteuerung) and are controlled via MQTT, a web UI or the serial console.</p>
<table>
<tr><th>Relay</th><th>GPIO</th><th>Default function</th></tr>
<tr><td>1</td><td>32</td><td>Shutter 1 UP</td></tr>
<tr><td>2</td><td>33</td><td>Shutter 1 DOWN</td></tr>
<tr><td>3</td><td>25</td><td>Shutter 2 UP</td></tr>
<tr><td>4</td><td>26</td><td>Shutter 2 DOWN</td></tr>
</table>
<p>Status LED GPIO 23, BOOT button GPIO 0, TM1637 display CLK GPIO 18 / DIO GPIO 19 (see <code>src/board.h</code>).</p>
<h2 id="first-start">First start</h2>
<ol><li>Flash: <code>pio run -e esp32dev -t upload &amp;&amp; pio device monitor</code></li><li>With no WiFi configured, the device opens the access point <b><code>relay4-XXXXXX</code></b> (open by default). Connect to it. A captive portal opens; otherwise browse to <a href="http://192.168.4.1/config" target="_blank">http://192.168.4.1/config</a>.</li><li>Enter WiFi + MQTT settings and click <b>Save</b>. The device reboots and joins your network. It can then be reached at <code>http://relay4-XXXXXX.local/</code> or via its IP.</li></ol>
<p>WiFi can also be set from the serial console:</p>
<pre>SET wifiSsid MyNetwork
SET wifiPass secret
SET mqttHost 192.168.1.10
REBOOT</pre>
<h2 id="mqtt">MQTT</h2>
<p>Base topic: <code>esp32/relay4-&lt;MAC&gt;</code> (e.g. <code>esp32/relay4-24A160123456</code>). It can be changed in the config.</p>
<table>
<tr><th>Topic</th><th>Direction</th><th>Content</th></tr>
<tr><td><code>&lt;base&gt;/cmd</code></td><td>in</td><td>Commands (text or JSON)</td></tr>
<tr><td><code>&lt;base&gt;/result</code></td><td>out</td><td><code>{&quot;ok&quot;:true,&quot;cmd&quot;:&quot;1,LONG&quot;,&quot;msg&quot;:&quot;R1 press 2000 ms&quot;}</code></td></tr>
<tr><td><code>&lt;base&gt;/status</code></td><td>out</td><td>Status JSON every 180 s, on every change and on connect (retained)</td></tr>
<tr><td><code>&lt;base&gt;/availability</code></td><td>out</td><td><code>online</code> / <code>offline</code> (retained, LWT)</td></tr>
<tr><td><code>&lt;base&gt;/log</code></td><td>out</td><td>Log lines (optional, set <i>Forward log</i> level)</td></tr>
</table>
<p>A retained message on <code>&lt;base&gt;/cmd</code> is deleted on every connect, so a command is never replayed after a reconnect.</p>
<h3 id="commands">Commands</h3>
<p>Text commands are case-insensitive. Separators can be <code>,</code>, <code>;</code> or a space.</p>
<table>
<tr><th>Command</th><th>Action</th></tr>
<tr><td><code>1,SHORT</code> / <code>1,LONG</code></td><td>Short / long press relay 1 (<code>R1,LONG</code> also works)</td></tr>
<tr><td><code>1,PRESS,800</code> or <code>1,800</code></td><td>Press relay 1 for 800 ms</td></tr>
<tr><td><code>1,ON</code> / <code>1,OFF</code></td><td>Hold / release relay 1 (ON is limited by <i>Max ON time</i>)</td></tr>
<tr><td><code>ALL,OFF</code> or <code>OFF</code></td><td>Release all relays, drop queued presses</td></tr>
<tr><td><code>S1,UP</code> / <code>S1,DOWN</code></td><td>Shutter 1 long press up / down (full travel)</td></tr>
<tr><td><code>S1,UP,SHORT</code> / <code>S1,DOWN,SHORT</code></td><td>Shutter 1 short press (step / stop)</td></tr>
<tr><td><code>S1,UP,1200</code></td><td>Shutter 1 up button for 1200 ms</td></tr>
<tr><td><code>S1,STOP</code></td><td>Short press to stop (see <i>STOP command</i> setting)</td></tr>
<tr><td><code>S1,OPEN</code> / <code>S1,CLOSE</code></td><td>Same as UP / DOWN (Home Assistant defaults)</td></tr>
<tr><td><code>STATUS</code></td><td>Publish status now</td></tr>
<tr><td><code>REBOOT</code></td><td>Restart (waits until no relay is active)</td></tr>
<tr><td><code>CONFIG</code></td><td>Show config JSON (secrets hidden)</td></tr>
<tr><td><code>SET &lt;key&gt; &lt;value&gt;</code> / <code>GET &lt;key&gt;</code></td><td>Change / read a setting (keys: see <code>/api/config</code>)</td></tr>
<tr><td><code>PORTAL</code></td><td>Open the config access point for 10 minutes</td></tr>
<tr><td><code>SHOW &lt;text&gt;</code></td><td>Show text on the display for 5 s (<code>|</code> = next frame, e.g. <code>SHOW HELLO|123456</code>)</td></tr>
<tr><td><code>PING</code>, <code>HELP</code></td><td></td></tr>
<tr><td><code>FACTORYRESET</code></td><td>Erase config (web/serial only, not MQTT)</td></tr>
</table>
<p>JSON commands:</p>
<pre>{&quot;relay&quot;:1,&quot;action&quot;:&quot;long&quot;}
{&quot;relay&quot;:2,&quot;action&quot;:&quot;press&quot;,&quot;ms&quot;:800}
{&quot;relay&quot;:&quot;all&quot;,&quot;action&quot;:&quot;off&quot;}
{&quot;shutter&quot;:1,&quot;action&quot;:&quot;up&quot;}
{&quot;shutter&quot;:2,&quot;action&quot;:&quot;down&quot;,&quot;press&quot;:&quot;short&quot;}
{&quot;shutter&quot;:1,&quot;action&quot;:&quot;stop&quot;}
{&quot;cmd&quot;:&quot;status&quot;}
{&quot;show&quot;:&quot;HELLO&quot;,&quot;ms&quot;:3000}
{&quot;config&quot;:{&quot;shortMs&quot;:250,&quot;longMs&quot;:2500}}
[{&quot;shutter&quot;:1,&quot;action&quot;:&quot;up&quot;},{&quot;shutter&quot;:2,&quot;action&quot;:&quot;up&quot;}]</pre>
<p>Network and security settings (WiFi, MQTT, topic, passwords) and the relay polarity can't be changed via MQTT. Use the web UI or the serial console for those. Passwords are masked in logs and in the MQTT result echo.</p>
<h3 id="status-json-abridged">Status JSON (abridged)</h3>
<pre>{
  &quot;device&quot;: &quot;relay4-123456&quot;, &quot;fw&quot;: &quot;1.0.0&quot;, &quot;reason&quot;: &quot;interval&quot;,
  &quot;uptime&quot;: 3600, &quot;uptimeStr&quot;: &quot;0d 01:00:00&quot;, &quot;time&quot;: &quot;2026-10-07T14:03:00+0200&quot;,
  &quot;relays&quot;: [0,0,0,0], &quot;presses&quot;: [12,9,3,4], &quot;pins&quot;: [32,33,25,26],
  &quot;shutters&quot;: [{&quot;id&quot;:1,&quot;name&quot;:&quot;Wohnzimmer&quot;,&quot;state&quot;:&quot;open&quot;,&quot;last&quot;:&quot;up_long&quot;,&quot;ago&quot;:95,&quot;upRelay&quot;:1,&quot;downRelay&quot;:2}, ...],
  &quot;errors&quot;: [{&quot;code&quot;:6,&quot;msg&quot;:&quot;MQTT broker not reachable&quot;}],
  &quot;display&quot;: {&quot;ok&quot;:true,&quot;text&quot;:&quot;1 oPEn&quot;},
  &quot;wifi&quot;: {&quot;connected&quot;:true,&quot;ssid&quot;:&quot;x&quot;,&quot;rssi&quot;:-58,&quot;ip&quot;:&quot;192.168.1.50&quot;,&quot;bssid&quot;:&quot;..&quot;,&quot;ch&quot;:6,&quot;reconnects&quot;:0,&quot;ap&quot;:false},
  &quot;mqtt&quot;: {&quot;enabled&quot;:true,&quot;connected&quot;:true,&quot;reconnects&quot;:0,&quot;base&quot;:&quot;esp32/relay4-...&quot;},
  &quot;sys&quot;:  {&quot;heap&quot;:210000,&quot;minHeap&quot;:190000,&quot;maxBlock&quot;:110000,&quot;boots&quot;:7,&quot;resetReason&quot;:&quot;software&quot;,&quot;lastRestart&quot;:&quot;mqtt-timeout&quot;,...},
  &quot;cfg&quot;:  {&quot;shortMs&quot;:300,&quot;longMs&quot;:2000,&quot;gapMs&quot;:300,&quot;interlock&quot;:true,&quot;interval&quot;:180}
}</pre>
<p><code>reason</code> is <code>interval</code>, <code>change</code>, <code>connect</code> or <code>request</code>. Shutter <code>state</code> is an estimate (<code>unknown</code>, <code>opening</code>, <code>closing</code>, <code>open</code>, <code>closed</code>, <code>stopped</code>) based on the commands sent and <i>Travel time</i>. Presses on the physical wall buttons aren't visible to the firmware.</p>
<h2 id="relay-behaviour">Relay behaviour</h2>
<ul><li><b>Short / long press</b>: durations are configurable (default 300 ms / 2000 ms).</li><li><b>Queue</b>: two <code>1,SHORT</code> in a row give two separate presses with <i>Pause between presses</i> (default 300 ms) in between. A press is never cut short by the next one. Up to 4 presses can be pending per relay; further ones are rejected with an error.</li><li><b>Interlock</b> (default on): UP and DOWN of a shutter are never energized together. A command on one relay releases its partner immediately, drops the partner's queued presses and waits the pause time before pressing.</li><li><b>Max ON time</b> (default 60 s): limits <code>ON</code> and long custom presses.</li><li>Timing runs in a dedicated high-priority task. Press durations stay exact even while WiFi/MQTT reconnects block the main loop.</li></ul>
<h2 id="resilience">Resilience</h2>
<ul><li>WiFi reconnects every 20 s. The config AP opens after 120 s without WiFi (<i>Open config AP after</i>) and closes again 60 s after WiFi is back.</li><li>MQTT reconnects with backoff (2 s → 60 s).</li><li><b>Reboot</b> after 10 min without WiFi or 15 min without MQTT (configurable, 0 = off). No reboot happens while a client is connected to the config AP, or while a relay is active.</li><li>Optional scheduled reboot every N hours.</li><li>Task watchdog (45 s) on the main loop and the relay task. Reboot if free heap stays below 16 KB for 60 s.</li><li>Reset reason, firmware restart reason (<code>wifi-timeout</code>, <code>mqtt-timeout</code>, <code>ota</code>, …) and boot counter are reported in the status.</li><li>Config is stored as JSON in NVS. Invalid or unknown stored values fall back to defaults.</li></ul>
<h2 id="web-ui">Web UI</h2>
<ul><li><code>/</code>: dashboard with shutter/relay buttons, status, live log and a command line</li><li><code>/config</code>: all settings, WiFi scan, reboot, factory reset</li><li><code>/update</code>: firmware upload (OTA)</li><li><code>/help</code>: this manual, plus this device's hostname, IP and MQTT topic</li><li>API: <code>GET /api/status</code>, <code>POST /api/cmd</code> (body = command), <code>GET|POST /api/config</code>, <code>GET /api/log?after=&lt;seq&gt;</code>, <code>GET /log</code></li></ul>
<pre>curl -X POST -d &#x27;c=S1,UP&#x27; http://relay4-123456.local/api/cmd
curl -H &#x27;Content-Type: application/json&#x27; -d &#x27;{&quot;relay&quot;:1,&quot;action&quot;:&quot;short&quot;}&#x27; http://relay4-123456.local/api/cmd</pre>
<p>Set a <i>Web / OTA password</i> (user <code>admin</code>) to protect the UI, API and OTA.</p>
<h2 id="ota-update">OTA update</h2>
<p>Upload <code>.pio/build/esp32dev/firmware.bin</code> at <code>/update</code>, or from PlatformIO:</p>
<pre>export RELAY4_WEBPASS=secret   # only if a web password is set
pio run -e ota -t upload --upload-port relay4-123456.local</pre>
<h2 id="status-display-tm1637-6-digits">Status display (TM1637, 6 digits)</h2>
<p>Wiring: <b>VCC → 3.3 V</b>, GND → GND, CLK → G18, DIO → G19. Use 3.3 V, not 5 V: the module's pull-up resistors go to VCC, and ESP32 pins aren't 5 V tolerant. G4, G16, G17, G21 and G22 would also work (change <code>src/board.h</code>). Avoid G0, G2, G5 and G15 (boot-strapping pins) and G23 (status LED).</p>
<p>If the text appears scrambled (e.g. <code>SHOW 123456</code> shows <code>321654</code>), switch <i>Digit order</i> in the config. If the display isn't connected or not responding, the log shows a warning and the status JSON reports <code>&quot;display&quot;:{&quot;ok&quot;:false}</code>.</p>
<p>What is shown (highest priority first):</p>
<table>
<tr><th>Display</th><th>Meaning</th></tr>
<tr><td><code>FAc  8</code></td><td>BOOT button held: seconds until factory reset</td></tr>
<tr><td><code>rESEt</code></td><td>Restart pending</td></tr>
<tr><td><code>UP 512</code></td><td>Firmware update running (kB received)</td></tr>
<tr><td><code>1uP Lo</code> <code>1dn Sh</code> <code>2 StoP</code></td><td>Shutter command (Lo = long, Sh = short press)</td></tr>
<tr><td><code>r1 Sh</code> <code>r1 Lo</code> <code>r1 Pr</code> <code>r1 on</code> <code>r1 oFF</code> <code>AL oFF</code></td><td>Relay command</td></tr>
<tr><td><code>c Err</code></td><td>A command was rejected (see log / <code>&lt;base&gt;/result</code>)</td></tr>
<tr><td><code>SAuEd</code></td><td>Settings saved</td></tr>
<tr><td><code>Conn</code> → IP</td><td>WiFi connected, followed by the IP address</td></tr>
<tr><td><code>on 13</code></td><td>Relays currently energized (relay 1 and 3)</td></tr>
<tr><td>rotation</td><td>Pages below, with <code>Err nn</code> after every page while errors are active</td></tr>
</table>
<p>Rotation pages (<i>Display mode</i> = Status rotation; <i>Clock</i> and <i>Shutter states</i> show a subset):</p>
<table>
<tr><th>Display</th><th>Meaning</th></tr>
<tr><td><code>SEtuP</code> / <code>AP on</code>, <code>A1b2C3</code>, <code>192.168.</code> <code>  4.  1</code></td><td>Config AP open: AP name suffix and address (only while open)</td></tr>
<tr><td><code>14.25.30</code></td><td>Time (after NTP sync)</td></tr>
<tr><td><code>1 oPEn</code></td><td>Shutter 1 state: <code>oPEn</code> <code>cLoS</code> <code>uP</code> <code>dn</code> <code>StoP</code> <code>----</code> (unknown)</td></tr>
<tr><td><code>192.168.</code> → <code>  1. 50</code></td><td>IP address</td></tr>
<tr><td><code>-58 db</code></td><td>WiFi signal strength</td></tr>
<tr><td><code>u 3d14</code> / <code>u 14.25</code></td><td>Uptime: 3 days 14 h / 14 h 25 min</td></tr>
<tr><td><code>rb 9.59</code></td><td>Connection watchdog reboots in 9 min 59 s</td></tr>
</table>
<h3 id="error-codes">Error codes</h3>
<p>Shown as <code>Err nn</code> and in the status JSON (<code>errors</code>). Several can be active at once.</p>
<table>
<tr><th>Code</th><th>Meaning</th><th>What to check</th></tr>
<tr><td>01</td><td>No WiFi configured, setup AP open</td><td>Connect to the AP and configure</td></tr>
<tr><td>02</td><td>WiFi not connected</td><td>Router, distance</td></tr>
<tr><td>03</td><td>WiFi password wrong</td><td>WiFi password</td></tr>
<tr><td>04</td><td>WiFi network not found</td><td>SSID, 2.4 GHz band, distance</td></tr>
<tr><td>05</td><td>No MQTT broker configured</td><td>MQTT settings</td></tr>
<tr><td>06</td><td>MQTT broker not reachable</td><td>Broker host/port, broker running</td></tr>
<tr><td>07</td><td>MQTT login rejected</td><td>MQTT user/password, broker ACL</td></tr>
<tr><td>08</td><td>MQTT connection refused</td><td>Client ID in use / broker unavailable</td></tr>
<tr><td>09</td><td>Time not synchronized</td><td>NTP server, internet access</td></tr>
<tr><td>10</td><td>Weak WiFi signal (&lt; -80 dBm)</td><td>Antenna, position, access point</td></tr>
<tr><td>11</td><td>Low memory</td><td>Report it, the device reboots itself if it gets worse</td></tr>
<tr><td>12</td><td>Restarted after a crash (1 h)</td><td>Serial log / report</td></tr>
<tr><td>13</td><td>Restarted by watchdog (1 h)</td><td>Serial log / report</td></tr>
<tr><td>14</td><td>Restarted by brownout (1 h)</td><td><b>Power supply too weak</b> (relays switching)</td></tr>
<tr><td>15</td><td>Restarted by connection watchdog (1 h)</td><td>WiFi / broker stability</td></tr>
</table>
<p>WiFi and MQTT errors only appear after 15 s, so normal reconnects don't flash errors.</p>
<p>Display settings: mode (off, status rotation, clock, shutter states), brightness 0–7, seconds per page, show commands, digit order.</p>
<h2 id="led-and-boot-button">LED and BOOT button</h2>
<table>
<tr><th>LED pattern</th><th>Meaning</th></tr>
<tr><td>On, short gap every 2 s</td><td>All good (heartbeat)</td></tr>
<tr><td>Slow blink (1 s)</td><td>Connecting to WiFi</td></tr>
<tr><td>Double blink</td><td>WiFi OK, MQTT not connected</td></tr>
<tr><td>Fast blink</td><td>Config access point open</td></tr>
<tr><td>Flicker</td><td>BOOT button held – factory reset at 10 s</td></tr>
</table>
<p>BOOT button: short press opens the config AP for 10 minutes. Hold it for 10 s to factory reset.</p>
<h2 id="serial-log">Serial log</h2>
<p>115200 baud. Every line has an uptime timestamp, a level and a tag:</p>
<pre>[     12.345] I WIFI  Connected to &#x27;MyNet&#x27; (BSSID .., ch 6, RSSI -58 dBm), IP 192.168.1.50
[     12.890] I MQTT  Connected in 120 ms
[     30.001] I CMD   [mqtt] S1,UP
[     30.002] I RELAY R1 ON (2000 ms)
[     32.003] I RELAY R1 OFF after 2001 ms</pre>
<p>The level is set with <i>Serial log level</i> (<code>SET logLevel debug</code> for more detail). You can type commands into the serial monitor.</p>
<h2 id="home-assistant-example">Home Assistant example</h2>
<pre>mqtt:
  cover:
    - name: &quot;Wohnzimmer&quot;
      command_topic: &quot;esp32/relay4-24A160123456/cmd&quot;
      payload_open: &quot;S1,UP&quot;
      payload_close: &quot;S1,DOWN&quot;
      payload_stop: &quot;S1,STOP&quot;
      state_topic: &quot;esp32/relay4-24A160123456/status&quot;
      value_template: &quot;{{ value_json.shutters[0].state }}&quot;
      state_open: &quot;open&quot;
      state_opening: &quot;opening&quot;
      state_closed: &quot;closed&quot;
      state_closing: &quot;closing&quot;
      availability_topic: &quot;esp32/relay4-24A160123456/availability&quot;</pre>
)HELPHTML";
