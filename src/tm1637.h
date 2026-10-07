#pragma once
#include <Arduino.h>

// Minimal bit-banged TM1637 driver (open-drain, with ACK check).
// Blocking for a few ms per update; call it from a low-priority task.
class TM1637 {
 public:
  static constexpr uint8_t DIGITS = 6;

  void begin(uint8_t clkPin, uint8_t dioPin, uint32_t bitDelayUs);
  // Writes raw segment bytes for grid addresses 0..5. Returns false if the
  // chip did not acknowledge (not connected / wiring problem).
  bool write(const uint8_t raw[DIGITS], uint8_t brightness, bool on);
  // Time (us) a released line needs to read HIGH; long times mean big
  // capacitors on the module (max. 5000)
  static uint32_t riseTimeUs(uint8_t pin);

 private:
  // Released lines are pulled up by resistors only. Modules with filter
  // capacitors rise slowly, so wait until the line really is high.
  static void waitHigh(uint8_t pin);
  void clk(bool high) {
    digitalWrite(clk_, high ? HIGH : LOW);
    if (high) waitHigh(clk_);
  }
  void dio(bool high) {
    digitalWrite(dio_, high ? HIGH : LOW);
    if (high) waitHigh(dio_);
  }
  void wait() { delayMicroseconds(delay_); }
  void start();
  void stop();
  bool writeByte(uint8_t b);

  uint8_t clk_ = 0, dio_ = 0;
  uint32_t delay_ = 50;
};
