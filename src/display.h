#pragma once
#include <Arduino.h>

// TM1637 6-digit status display.
//
// Priority (highest first):
//   FAc  8     BOOT button held: seconds until factory reset
//   rESEt      restart pending
//   UP 512     firmware update running (kB received)
//   messages   events such as "1uP Lo", "r1 on", "c Err", "SAuEd", SHOW command text
//   on 13      relays currently energized (if no event is shown)
//   rotation   pages depending on the display mode, with "Err nn" after every page
//              while errors are active (codes: see health.h)
//
// Rotation pages (mode "Status rotation"):
//   SEtuP / AP on, AP name, 192.168. / 4.1   only while the config AP is open
//   14.25.30   clock (once NTP is synced)
//   1 oPEn     shutter states: oPEn cLoS uP dn StoP ----
//   192.168. / 1. 50                         IP address (2 pages)
//   -58 db     WiFi signal
//   u 3d14     uptime (days+hours, or "u 14.25" hours.minutes)
//   rb 9.59    time until the connection watchdog reboots
void displayInit();
void displayLoop();

// Shows a temporary message; '|' separates frames shown one after another.
void displayShow(const String& text, uint32_t ms);
// Short notification for user actions (only if "Show commands" is enabled)
void displayEvent(const String& text);

bool displayPresent();
String displayCurrentText();
