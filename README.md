# ESP32 Relay X4 – MQTT roller shutter button emulator

Developed by **Philipp Albrecht** <philipp@uisa.ch> using Claude Opus 5.5, 2026-10-07
· <https://uisa.ch>

Firmware for the **ESP32_Relay_X4 v1.1** board. The four relays emulate the
UP/DOWN buttons of two roller shutter controllers (Rolladensteuerung) and are
controlled via MQTT, a web UI or the serial console.

| Relay | GPIO | Default function |
|-------|------|------------------|
| 1     | 32   | Shutter 1 UP     |
| 2     | 33   | Shutter 1 DOWN   |
| 3     | 25   | Shutter 2 UP     |
| 4     | 26   | Shutter 2 DOWN   |

Status LED GPIO 23, BOOT button GPIO 0, TM1637 display CLK GPIO 18 / DIO GPIO 19
(see `src/board.h`).

## First start

1. Flash: `pio run -e esp32dev -t upload && pio device monitor`
2. With no WiFi configured, the device opens the access point **`relay4-XXXXXX`**
   (open by default). Connect to it. A captive portal opens; otherwise browse to
   <http://192.168.4.1/config>.
3. Enter WiFi + MQTT settings and click **Save**. The device reboots and joins your
   network. It can then be reached at `http://relay4-XXXXXX.local/` or via its IP.

WiFi can also be set from the serial console:

```
SET wifiSsid MyNetwork
SET wifiPass secret
SET mqttHost 192.168.1.10
REBOOT
```

## MQTT

Base topic: `esp32/relay4-<MAC>` (e.g. `esp32/relay4-24A160123456`). It can be changed in the config.

| Topic                 | Direction | Content |
|-----------------------|-----------|---------|
| `<base>/cmd`          | in        | Commands (text or JSON) |
| `<base>/result`       | out       | `{"ok":true,"cmd":"1,LONG","msg":"R1 press 2000 ms"}` |
| `<base>/status`       | out       | Status JSON every 180 s, on every change and on connect (retained) |
| `<base>/availability` | out       | `online` / `offline` (retained, LWT) |
| `<base>/log`          | out       | Log lines (optional, set *Forward log* level) |

A retained message on `<base>/cmd` is deleted on every connect, so a command is
never replayed after a reconnect.

### Commands

Text commands are case-insensitive. Separators can be `,`, `;` or a space.

| Command | Action |
|---------|--------|
| `1,SHORT` / `1,LONG` | Short / long press relay 1 (`R1,LONG` also works) |
| `1,PRESS,800` or `1,800` | Press relay 1 for 800 ms |
| `1,ON` / `1,OFF` | Hold / release relay 1 (ON is limited by *Max ON time*) |
| `ALL,OFF` or `OFF` | Release all relays, drop queued presses |
| `S1,UP` / `S1,DOWN` | Shutter 1 long press up / down (full travel) |
| `S1,UP,SHORT` / `S1,DOWN,SHORT` | Shutter 1 short press (step / stop) |
| `S1,UP,1200` | Shutter 1 up button for 1200 ms |
| `S1,STOP` | Short press to stop (see *STOP command* setting) |
| `S1,OPEN` / `S1,CLOSE` | Same as UP / DOWN (Home Assistant defaults) |
| `STATUS` | Publish status now |
| `REBOOT` | Restart (waits until no relay is active) |
| `CONFIG` | Show config JSON (secrets hidden) |
| `SET <key> <value>` / `GET <key>` | Change / read a setting (keys: see `/api/config`) |
| `PORTAL` | Open the config access point for 10 minutes |
| `SHOW <text>` | Show text on the display for 5 s (`\|` = next frame, e.g. `SHOW HELLO\|123456`) |
| `PING`, `HELP` | |
| `FACTORYRESET` | Erase config (web/serial only, not MQTT) |

JSON commands:

```json
{"relay":1,"action":"long"}
{"relay":2,"action":"press","ms":800}
{"relay":"all","action":"off"}
{"shutter":1,"action":"up"}
{"shutter":2,"action":"down","press":"short"}
{"shutter":1,"action":"stop"}
{"cmd":"status"}
{"show":"HELLO","ms":3000}
{"config":{"shortMs":250,"longMs":2500}}
[{"shutter":1,"action":"up"},{"shutter":2,"action":"up"}]
```

Network and security settings (WiFi, MQTT, topic, passwords) and the relay
polarity can't be changed via MQTT. Use the web UI or the serial console for those.
Passwords are masked in logs and in the MQTT result echo.

### Status JSON (abridged)

```json
{
  "device": "relay4-123456", "fw": "1.0.0", "reason": "interval",
  "uptime": 3600, "uptimeStr": "0d 01:00:00", "time": "2026-10-07T14:03:00+0200",
  "relays": [0,0,0,0], "presses": [12,9,3,4], "pins": [32,33,25,26],
  "shutters": [{"id":1,"name":"Wohnzimmer","state":"open","last":"up_long","ago":95,"upRelay":1,"downRelay":2}, ...],
  "errors": [{"code":6,"msg":"MQTT broker not reachable"}],
  "display": {"ok":true,"text":"1 oPEn"},
  "wifi": {"connected":true,"ssid":"x","rssi":-58,"ip":"192.168.1.50","bssid":"..","ch":6,"reconnects":0,"ap":false},
  "mqtt": {"enabled":true,"connected":true,"reconnects":0,"base":"esp32/relay4-..."},
  "sys":  {"heap":210000,"minHeap":190000,"maxBlock":110000,"boots":7,"resetReason":"software","lastRestart":"mqtt-timeout",...},
  "cfg":  {"shortMs":300,"longMs":2000,"gapMs":300,"interlock":true,"interval":180}
}
```

`reason` is `interval`, `change`, `connect` or `request`. Shutter `state` is an estimate
(`unknown`, `opening`, `closing`, `open`, `closed`, `stopped`) based on the commands
sent and *Travel time*. Presses on the physical wall buttons aren't visible to the
firmware.

## Relay behaviour

- **Short / long press**: durations are configurable (default 300 ms / 2000 ms).
- **Queue**: two `1,SHORT` in a row give two separate presses with *Pause between presses*
  (default 300 ms) in between. A press is never cut short by the next one. Up to 4
  presses can be pending per relay; further ones are rejected with an error.
- **Interlock** (default on): UP and DOWN of a shutter are never energized together.
  A command on one relay releases its partner immediately, drops the partner's queued
  presses and waits the pause time before pressing.
- **Max ON time** (default 60 s): limits `ON` and long custom presses.
- Timing runs in a dedicated high-priority task. Press durations stay exact even while
  WiFi/MQTT reconnects block the main loop.

## Resilience

- WiFi reconnects every 20 s. The config AP opens after 120 s without WiFi
  (*Open config AP after*) and closes again 60 s after WiFi is back.
- MQTT reconnects with backoff (2 s → 60 s).
- **Reboot** after 10 min without WiFi or 15 min without MQTT (configurable, 0 = off).
  No reboot happens while a client is connected to the config AP, or while a relay is active.
- Optional scheduled reboot every N hours.
- Task watchdog (45 s) on the main loop and the relay task. Reboot if free heap stays
  below 16 KB for 60 s.
- Reset reason, firmware restart reason (`wifi-timeout`, `mqtt-timeout`, `ota`, …) and
  boot counter are reported in the status.
- Config is stored as JSON in NVS. Invalid or unknown stored values fall back to defaults.

## Web UI

- `/`: dashboard with shutter/relay buttons, status, live log and a command line
- `/config`: all settings, WiFi scan, reboot, factory reset
- `/update`: firmware upload (OTA)
- `/help`: this manual, plus this device's hostname, IP and MQTT topic
- API: `GET /api/status`, `POST /api/cmd` (body = command), `GET|POST /api/config`,
  `GET /api/log?after=<seq>`, `GET /log`

```
curl -X POST -d 'c=S1,UP' http://relay4-123456.local/api/cmd
curl -H 'Content-Type: application/json' -d '{"relay":1,"action":"short"}' http://relay4-123456.local/api/cmd
```

Set a *Web / OTA password* (user `admin`) to protect the UI, API and OTA.

## OTA update

Upload `.pio/build/esp32dev/firmware.bin` at `/update`, or from PlatformIO:

```
export RELAY4_WEBPASS=secret   # only if a web password is set
pio run -e ota -t upload --upload-port relay4-123456.local
```

## Status display (TM1637, 6 digits)

Wiring: **VCC → 3.3 V**, GND → GND, CLK → G18, DIO → G19.
Use 3.3 V, not 5 V: the module's pull-up resistors go to VCC, and ESP32 pins aren't
5 V tolerant. G4, G16, G17, G21 and G22 would also work (change `src/board.h`). Avoid
G0, G2, G5 and G15 (boot-strapping pins) and G23 (status LED).

If the text appears scrambled (e.g. `SHOW 123456` shows `321654`), switch *Digit order*
in the config. If the display isn't connected or not responding, the log shows a warning
and the status JSON reports `"display":{"ok":false}`.

What is shown (highest priority first):

| Display | Meaning |
|---------|---------|
| `FAc  8` | BOOT button held: seconds until factory reset |
| `rESEt` | Restart pending |
| `UP 512` | Firmware update running (kB received) |
| `1uP Lo` `1dn Sh` `2 StoP` | Shutter command (Lo = long, Sh = short press) |
| `r1 Sh` `r1 Lo` `r1 Pr` `r1 on` `r1 oFF` `AL oFF` | Relay command |
| `c Err` | A command was rejected (see log / `<base>/result`) |
| `SAuEd` | Settings saved |
| `Conn` → IP | WiFi connected, followed by the IP address |
| `on 13` | Relays currently energized (relay 1 and 3) |
| rotation | Pages below, with `Err nn` after every page while errors are active |

Rotation pages (*Display mode* = Status rotation; *Clock* and *Shutter states* show a subset):

| Display | Meaning |
|---------|---------|
| `SEtuP` / `AP on`, `A1b2C3`, `192.168.` `  4.  1` | Config AP open: AP name suffix and address (only while open) |
| `14.25.30` | Time (after NTP sync) |
| `1 oPEn` | Shutter 1 state: `oPEn` `cLoS` `uP` `dn` `StoP` `----` (unknown) |
| `192.168.` → `  1. 50` | IP address |
| `-58 db` | WiFi signal strength |
| `u 3d14` / `u 14.25` | Uptime: 3 days 14 h / 14 h 25 min |
| `rb 9.59` | Connection watchdog reboots in 9 min 59 s |

### Error codes

Shown as `Err nn` and in the status JSON (`errors`). Several can be active at once.

| Code | Meaning | What to check |
|------|---------|---------------|
| 01 | No WiFi configured, setup AP open | Connect to the AP and configure |
| 02 | WiFi not connected | Router, distance |
| 03 | WiFi password wrong | WiFi password |
| 04 | WiFi network not found | SSID, 2.4 GHz band, distance |
| 05 | No MQTT broker configured | MQTT settings |
| 06 | MQTT broker not reachable | Broker host/port, broker running |
| 07 | MQTT login rejected | MQTT user/password, broker ACL |
| 08 | MQTT connection refused | Client ID in use / broker unavailable |
| 09 | Time not synchronized | NTP server, internet access |
| 10 | Weak WiFi signal (< -80 dBm) | Antenna, position, access point |
| 11 | Low memory | Report it, the device reboots itself if it gets worse |
| 12 | Restarted after a crash (1 h) | Serial log / report |
| 13 | Restarted by watchdog (1 h) | Serial log / report |
| 14 | Restarted by brownout (1 h) | **Power supply too weak** (relays switching) |
| 15 | Restarted by connection watchdog (1 h) | WiFi / broker stability |

WiFi and MQTT errors only appear after 15 s, so normal reconnects don't flash errors.

Display settings: mode (off, status rotation, clock, shutter states), brightness 0–7,
seconds per page, show commands, digit order.

## LED and BOOT button

| LED pattern | Meaning |
|-------------|---------|
| On, short gap every 2 s | All good (heartbeat) |
| Slow blink (1 s) | Connecting to WiFi |
| Double blink | WiFi OK, MQTT not connected |
| Fast blink | Config access point open |
| Flicker | BOOT button held – factory reset at 10 s |

BOOT button: short press opens the config AP for 10 minutes. Hold it for 10 s to
factory reset.

## Serial log

115200 baud. Every line has an uptime timestamp, a level and a tag:

```
[     12.345] I WIFI  Connected to 'MyNet' (BSSID .., ch 6, RSSI -58 dBm), IP 192.168.1.50
[     12.890] I MQTT  Connected in 120 ms
[     30.001] I CMD   [mqtt] S1,UP
[     30.002] I RELAY R1 ON (2000 ms)
[     32.003] I RELAY R1 OFF after 2001 ms
```

The level is set with *Serial log level* (`SET logLevel debug` for more detail). You can
type commands into the serial monitor.

## Home Assistant example

```yaml
mqtt:
  cover:
    - name: "Wohnzimmer"
      command_topic: "esp32/relay4-24A160123456/cmd"
      payload_open: "S1,UP"
      payload_close: "S1,DOWN"
      payload_stop: "S1,STOP"
      state_topic: "esp32/relay4-24A160123456/status"
      value_template: "{{ value_json.shutters[0].state }}"
      state_open: "open"
      state_opening: "opening"
      state_closed: "closed"
      state_closing: "closing"
      availability_topic: "esp32/relay4-24A160123456/availability"
```
