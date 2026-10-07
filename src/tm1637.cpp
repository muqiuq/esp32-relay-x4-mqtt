#include "tm1637.h"

void TM1637::begin(uint8_t clkPin, uint8_t dioPin, uint32_t bitDelayUs) {
  clk_ = clkPin;
  dio_ = dioPin;
  delay_ = bitDelayUs;
  // Open drain: HIGH = released (pulled up by the module), LOW = driven
  digitalWrite(clk_, HIGH);
  digitalWrite(dio_, HIGH);
  pinMode(clk_, OUTPUT_OPEN_DRAIN | PULLUP);
  pinMode(dio_, OUTPUT_OPEN_DRAIN | PULLUP);
}

void TM1637::waitHigh(uint8_t pin) {
  uint32_t t0 = micros();
  while (digitalRead(pin) == LOW && micros() - t0 < 1000) {
  }
}

uint32_t TM1637::riseTimeUs(uint8_t pin) {
  digitalWrite(pin, LOW);
  delayMicroseconds(200);
  uint32_t t0 = micros();
  digitalWrite(pin, HIGH);
  while (digitalRead(pin) == LOW && micros() - t0 < 5000) {
  }
  return micros() - t0;
}

void TM1637::start() {
  dio(true);
  clk(true);
  wait();
  dio(false);
  wait();
}

void TM1637::stop() {
  clk(false);
  wait();
  dio(false);
  wait();
  clk(true);
  wait();
  dio(true);
  wait();
}

bool TM1637::writeByte(uint8_t b) {
  for (uint8_t i = 0; i < 8; i++) {  // LSB first
    clk(false);
    wait();
    dio(b & 0x01);
    wait();
    clk(true);
    wait();
    b >>= 1;
  }
  // 9th clock: the chip pulls DIO low as acknowledge. Release DIO without
  // waitHigh(): it is expected to stay low.
  clk(false);
  digitalWrite(dio_, HIGH);
  wait();
  clk(true);
  wait();
  bool ack = digitalRead(dio_) == LOW;
  clk(false);
  wait();
  return ack;
}

bool TM1637::write(const uint8_t raw[DIGITS], uint8_t brightness, bool on) {
  bool ok = true;
  start();
  ok &= writeByte(0x40);  // data command: write, auto-increment address
  stop();

  start();
  ok &= writeByte(0xC0);  // start at address 0
  for (uint8_t i = 0; i < DIGITS; i++) ok &= writeByte(raw[i]);
  stop();

  start();
  ok &= writeByte(0x80 | (on ? 0x08 : 0x00) | (brightness & 0x07));  // display control
  stop();
  return ok;
}
