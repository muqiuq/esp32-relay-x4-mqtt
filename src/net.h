#pragma once
#include <Arduino.h>

// WiFi station management with a captive-portal access point fallback.
//  - No SSID configured           -> AP (portal) stays open
//  - STA down for apFallbackSec   -> AP opens, STA keeps retrying in background
//  - STA back for 60 s, no client -> AP closes again
void netInit();
void netLoop();

bool netStaConnected();
bool netApActive();
uint8_t netApClients();
// Opens the config AP for durationMs (also when WiFi is connected)
void netStartPortal(uint32_t durationMs);
uint32_t netWifiDownSince();  // millis() when the STA connection was lost (or boot)
uint32_t netReconnects();
uint32_t netConnectedSince();       // millis() of the last successful connect
uint8_t netLastDisconnectReason();  // WIFI_REASON_* of the last disconnect
// Async WiFi scan for the config page: {"scanning":true} or {"networks":[...]}
String netScanJson();
