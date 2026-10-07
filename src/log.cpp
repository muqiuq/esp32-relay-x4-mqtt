#include "log.h"
#include <esp_timer.h>

namespace {

struct LogEntry {
  uint32_t seq;
  uint8_t level;
  char text[LOG_LINE_LEN];
};

LogEntry ring[LOG_LINES];
uint32_t nextSeq = 1;
SemaphoreHandle_t mtx = nullptr;
TaskHandle_t writer = nullptr;
volatile uint8_t serialLevel = LL_INFO;
volatile uint8_t recordLevel = LL_INFO;

const char LEVEL_CHARS[] = {'-', 'E', 'W', 'I', 'D'};

void writerTask(void*) {
  uint32_t seq = 0;
  char line[LOG_LINE_LEN];
  uint8_t level;
  for (;;) {
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
    uint32_t prev = seq;
    while (logGetNext(seq, line, sizeof(line), &level)) {
      if (seq != prev + 1 && prev != 0) {
        Serial.printf("[log] %u lines dropped\r\n", (unsigned)(seq - prev - 1));
      }
      prev = seq;
      if (level <= serialLevel) Serial.printf("%s\r\n", line);
    }
  }
}

}  // namespace

void logInit(unsigned long baud) {
  Serial.setTxBufferSize(2048);
  Serial.begin(baud);
  mtx = xSemaphoreCreateMutex();
  xTaskCreatePinnedToCore(writerTask, "log", 3072, nullptr, 1, &writer, tskNO_AFFINITY);
}

void logSetLevels(uint8_t serial, uint8_t record) {
  serialLevel = serial;
  // The ring buffer (web log) always keeps at least INFO
  uint8_t r = record > serial ? record : serial;
  recordLevel = r > LL_INFO ? r : LL_INFO;
}

void logPrintf(LogLevel level, const char* tag, const char* fmt, ...) {
  if (level == LL_OFF || level > recordLevel) return;

  char buf[LOG_LINE_LEN];
  uint64_t ms = esp_timer_get_time() / 1000ULL;
  int n = snprintf(buf, sizeof(buf), "[%7lu.%03u] %c %-5s ", (unsigned long)(ms / 1000), (unsigned)(ms % 1000),
                   LEVEL_CHARS[level], tag);
  if (n < 0) return;
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf + n, sizeof(buf) - n, fmt, ap);
  va_end(ap);

  if (!mtx) {  // before logInit()
    Serial.println(buf);
    return;
  }
  xSemaphoreTake(mtx, portMAX_DELAY);
  LogEntry& e = ring[nextSeq % LOG_LINES];
  e.seq = nextSeq++;
  e.level = level;
  strlcpy(e.text, buf, sizeof(e.text));
  xSemaphoreGive(mtx);
  if (writer) xTaskNotifyGive(writer);
}

bool logGetNext(uint32_t& seq, char* out, size_t outLen, uint8_t* level) {
  if (!mtx) return false;
  bool ok = false;
  xSemaphoreTake(mtx, portMAX_DELAY);
  uint32_t want = seq + 1;
  uint32_t oldest = nextSeq > LOG_LINES ? nextSeq - LOG_LINES : 1;
  if (want < oldest) want = oldest;
  if (want < nextSeq) {
    const LogEntry& e = ring[want % LOG_LINES];
    strlcpy(out, e.text, outLen);
    if (level) *level = e.level;
    seq = want;
    ok = true;
  }
  xSemaphoreGive(mtx);
  return ok;
}

uint32_t logLastSeq() {
  if (!mtx) return 0;
  xSemaphoreTake(mtx, portMAX_DELAY);
  uint32_t s = nextSeq - 1;
  xSemaphoreGive(mtx);
  return s;
}

void logRaw(const char* text) {
  Serial.write(reinterpret_cast<const uint8_t*>(text), strlen(text));
  Serial.write("\r\n");
}
