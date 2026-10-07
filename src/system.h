#pragma once
#include <Arduino.h>

// Watchdogs, restart handling, status LED and BOOT button.
void systemInit();
void systemLoop();
// Applies config values that can change at runtime (relay timings, log levels)
void systemApplyConfig();

// Restarts after delayMs, waiting (max. 15 s) until no relay is active
void systemRequestRestart(const char* reason, uint32_t delayMs = 500);
void systemFactoryReset();

uint64_t systemUptimeSec();
String systemUptimeStr();
bool systemTimeStr(char* buf, size_t len);  // false until NTP synced
const char* systemResetReason();
const char* systemLastRestart();            // reason of the last firmware-initiated restart
uint32_t systemBootCount();
bool systemRestartPending();
int32_t systemRebootCountdownSec();  // connection watchdog countdown, -1 if not running
uint32_t systemButtonHeldMs();       // how long the BOOT button is held (0 = released)
