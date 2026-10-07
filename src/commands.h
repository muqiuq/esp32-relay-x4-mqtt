#pragma once
#include <Arduino.h>

// Command interpreter shared by MQTT, web and serial console.
//
// Text commands (case-insensitive, separators: , ; space):
//   1,SHORT  1,LONG  1,ON  1,OFF  1,PRESS,800  1,800  ALL,OFF   (relay 1..4, "R1" also works)
//   S1,UP  S1,DOWN  S1,STOP  S1,UP,SHORT  S1,DOWN,LONG  S1,UP,1200  (OPEN/CLOSE = UP/DOWN)
//   STATUS  REBOOT  CONFIG  SET <key> <value>  GET <key>  PORTAL  PING  HELP  FACTORYRESET
//   SHOW <text>   (display message for 5 s, '|' separates frames)
// JSON commands:
//   {"relay":1,"action":"long"}   {"relay":2,"action":"press","ms":800}  {"relay":"all","action":"off"}
//   {"shutter":1,"action":"up"}   {"shutter":2,"action":"down","press":"short"}  {"shutter":1,"action":"stop"}
//   {"cmd":"status"}              {"config":{"shortMs":250,"longMs":2500}}   {"show":"HELLO","ms":3000}
//   [ ...several of the above... ]

enum CmdSource : uint8_t { SRC_MQTT, SRC_WEB, SRC_SERIAL };

struct CmdResult {
  bool ok = true;
  String msg;
};

CmdResult commandExecute(const char* payload, size_t len, CmdSource src);
const char* cmdSourceName(CmdSource src);
// Command text safe for logs / MQTT echo (secret values replaced)
String commandRedact(const String& cmd);
