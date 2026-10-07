#pragma once
#include <Arduino.h>

// Error / warning codes. Shown on the display as "Err 06" and published in
// the status JSON ("errors"). Several can be active at the same time.
enum HealthCode : uint8_t {
  H_NO_WIFI_CONFIG = 1,     // no WiFi configured, setup access point open
  H_WIFI_DOWN = 2,          // WiFi not connected (connecting / lost)
  H_WIFI_AUTH = 3,          // WiFi password wrong
  H_WIFI_NO_SSID = 4,       // WiFi network not found
  H_MQTT_NO_CONFIG = 5,     // no MQTT broker configured
  H_MQTT_UNREACHABLE = 6,   // broker not reachable (host/port/network)
  H_MQTT_AUTH = 7,          // broker rejected user/password
  H_MQTT_REFUSED = 8,       // broker refused connection (client id / protocol / unavailable)
  H_NO_TIME = 9,            // time not synchronized (NTP)
  H_WEAK_SIGNAL = 10,       // WiFi signal below -80 dBm
  H_LOW_MEMORY = 11,        // free heap below 32 KB
  H_RESET_CRASH = 12,       // last reset: crash (shown for 1 h after boot)
  H_RESET_WATCHDOG = 13,    // last reset: watchdog (1 h)
  H_RESET_BROWNOUT = 14,    // last reset: brownout - power supply too weak (1 h)
  H_RESET_CONNECTION = 15,  // last restart: connection watchdog (1 h)
};

// Fills codes with the currently active conditions (ascending). Returns the count.
size_t healthCollect(uint8_t* codes, size_t max);
const char* healthText(uint8_t code);
