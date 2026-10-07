#pragma once
#include <Arduino.h>

// Firmware identification
#define FW_NAME    "esp32-relay-x4-mqtt"
#define FW_VERSION "1.0.0"
#define FW_AUTHOR  "Philipp Albrecht <philipp@uisa.ch>"
#define FW_URL     "https://uisa.ch"
#define FW_DATE    "2026-10-07"

// ---------------------------------------------------------------------------
// Hardware: ESP32_Relay_X4 v1.1
// Pins are compile-time constants on purpose: a wrong pin (e.g. a flash pin)
// configured at runtime could brick the device.
// ---------------------------------------------------------------------------
static constexpr uint8_t RELAY_COUNT = 4;
static constexpr uint8_t RELAY_PINS[RELAY_COUNT] = {32, 33, 25, 26};

// Two relays form one shutter: relay 1/2 -> shutter 1, relay 3/4 -> shutter 2.
// The first relay of a pair is "up", the second "down" (can be swapped per
// shutter in the config).
static constexpr uint8_t SHUTTER_COUNT = 2;

// Status LED (-1 to disable)
#define LED_PIN        23
#define LED_ACTIVE_LOW false

// BOOT button (-1 to disable).
//   short press  (< 2 s): open the config access point for 10 minutes
//   hold         (>= 10 s): factory reset
#define BUTTON_PIN 0

// TM1637 6-digit 7-segment display (set CLK to -1 to disable).
// Power the module from 3.3 V: its pull-ups go to VCC and ESP32 pins are not 5 V tolerant.
#define DISPLAY_CLK_PIN      18
#define DISPLAY_DIO_PIN      19
#define DISPLAY_BIT_DELAY_US 50  // slow enough for modules with filter capacitors

// ---------------------------------------------------------------------------
// Tunables that are not worth exposing in the web UI
// ---------------------------------------------------------------------------
static constexpr uint32_t WDT_TIMEOUT_S         = 45;        // task watchdog for loop()
static constexpr uint32_t LOW_HEAP_BYTES        = 16 * 1024; // reboot if below this ...
static constexpr uint32_t LOW_HEAP_GRACE_MS     = 60000;     // ... for this long
static constexpr uint32_t PORTAL_BUTTON_MS      = 10 * 60000;
static constexpr uint32_t FACTORY_RESET_HOLD_MS = 10000;
static constexpr uint32_t CMD_MAX_LEN           = 1024;
static constexpr uint16_t MQTT_BUFFER_SIZE      = 2048;
