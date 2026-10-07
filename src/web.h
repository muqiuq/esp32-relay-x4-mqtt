#pragma once
#include <Arduino.h>

// Web UI and HTTP API
//   /            dashboard (shutters, relays, status, live log, command line)
//   /config      settings form             /update      firmware upload (OTA)
//   /help        manual (generated from README.md at build time)
//   /api/status  status JSON               /api/cmd     POST command (text or JSON)
//   /api/config  GET / POST config JSON    /api/log     log lines (?after=<seq>)
//   /api/scan    WiFi scan                 /log         log as plain text
void webInit();
void webLoop();
int32_t webOtaProgressKB();  // -1 if no firmware update is running
