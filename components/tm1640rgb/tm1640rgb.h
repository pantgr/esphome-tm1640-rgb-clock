#pragma once
// TM1640B driver for the CAI-274 RGB LED clock kit (ESPHome external component).
// The TM1640B switches each LED on or off and has one brightness (pulse width) for the whole display; it scans its
// 16 grids by itself on an internal RC clock. On the kit every big digit is three grids: red, green, blue.
// This driver rewrites the chip's RAM from its own FreeRTOS task. set_digit_rgb() gives every segment its own hard
// colour, always steady. set_digit() also takes 0..255 levels and dithers them over frames, but any level other than
// 0 or 255 flickers on this chip: the dithering beats against the chip's own scan (see the README).
// Bus facts (TM1640B datasheet p3-4): SCLK max 1 MHz, clock pulse >= 400 ns, setup/hold >= 100 ns; DIN changes only
// while SCLK is low; START = DIN falls while SCLK is high, STOP = DIN rises while SCLK is high; LSB first.
// Commands (p5-7): 0x40 auto-increment, 0xC0 + address, 0x88 | b = display on with pulse width b (1/16 .. 14/16).

#include "esphome/core/component.h"

namespace esphome::tm1640rgb {

class TM1640RGB final : public Component {
 public:
  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::PROCESSOR; }

  void set_pins(uint8_t clk, uint8_t dio) {
    this->clk_ = clk;
    this->dio_ = dio;
  }
  void set_half_bit_us(uint32_t us) { this->half_us_ = us; }
  // 0..7 = pulse width 1/16, 2/16, 4/16, 10/16 .. 14/16 for the whole display.
  void brightness(uint8_t b) { this->brightness_ = b & 0x07; }
  // Big digit d (0..3): segments (bit0..6 = a..g, bit7 = the mark next to it) and its colour, 0..255 per channel.
  void set_digit(uint8_t d, uint8_t segs, uint8_t r, uint8_t g, uint8_t b);
  // Big digit d (0..3), one segment mask per colour (same bits), each fully on: every segment its own one of the 7
  // hard colours, steady, no dimming. set_digit and set_digit_rgb each clear what the other one set for the digit.
  void set_digit_rgb(uint8_t d, uint8_t r, uint8_t g, uint8_t b);
  // GRID13..15 (i = 0..2): the small display's single-colour LEDs, plain on/off bits.
  void set_small(uint8_t i, uint8_t bits);

 protected:
  static void task_(void *arg);
  void frame_();
  void start_();
  void stop_();
  void send_byte_(uint8_t b);
  void command_(uint8_t cmd);
  inline void half_bit_();

  uint8_t clk_{0};
  uint8_t dio_{0};
  uint32_t half_us_{2};
  // Written by the yaml lambdas (loop task), read by the refresh task; single bytes, no lock needed.
  volatile uint8_t brightness_{0};
  volatile uint8_t segs_[4]{};
  volatile uint8_t level_[4][3]{};
  volatile uint8_t mask_[4][3]{};  // set_digit_rgb: segments fully on, per colour
  volatile uint8_t small_[3]{};
  // Refresh task only.
  uint32_t rng_{0x2545F491};  // xorshift32 state, never 0
  uint8_t sent_brightness_{0xFF};  // last display command sent; 0xFF = none yet
  volatile uint32_t frames_{0};
};

}  // namespace esphome::tm1640rgb
