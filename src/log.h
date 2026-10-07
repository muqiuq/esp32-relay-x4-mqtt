#pragma once
#include <Arduino.h>

// Thread-safe logger.
// Lines are formatted by the caller into a ring buffer and written to Serial by
// a separate low-priority task, so logging never blocks time-critical code
// (e.g. the relay task) on the UART. The ring buffer is also exposed to the
// web UI and the optional MQTT log topic.

enum LogLevel : uint8_t { LL_OFF = 0, LL_ERROR = 1, LL_WARN = 2, LL_INFO = 3, LL_DEBUG = 4 };

static constexpr size_t LOG_LINE_LEN = 160;
static constexpr size_t LOG_LINES    = 64;

void logInit(unsigned long baud);
// serialLevel: what goes to the UART; recordLevel: what is kept in the ring buffer
// (at least INFO, so the web log works even with the serial log switched off)
void logSetLevels(uint8_t serialLevel, uint8_t recordLevel);
void logPrintf(LogLevel level, const char* tag, const char* fmt, ...) __attribute__((format(printf, 3, 4)));

// Fetches the next buffered line after `seq`. On success `seq` is updated to the
// returned line's sequence number. If lines were overwritten, skips to the oldest.
bool logGetNext(uint32_t& seq, char* out, size_t outLen, uint8_t* level = nullptr);
uint32_t logLastSeq();

// Writes raw text to the UART (for multi-line command output on the serial console).
void logRaw(const char* text);

#define LOGE(tag, ...) logPrintf(LL_ERROR, tag, __VA_ARGS__)
#define LOGW(tag, ...) logPrintf(LL_WARN, tag, __VA_ARGS__)
#define LOGI(tag, ...) logPrintf(LL_INFO, tag, __VA_ARGS__)
#define LOGD(tag, ...) logPrintf(LL_DEBUG, tag, __VA_ARGS__)
