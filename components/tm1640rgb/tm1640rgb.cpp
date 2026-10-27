#include "tm1640rgb.h"
#include "esphome/core/log.h"

#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace esphome::tm1640rgb {

static const char *const TAG = "tm1640rgb";

static constexpr uint8_t CMD_DATA_AUTO = 0x40;
static constexpr uint8_t CMD_ADDR = 0xC0;
static constexpr uint8_t CMD_DISP_ON = 0x88;

inline void TM1640RGB::half_bit_() { esp_rom_delay_us(this->half_us_); }

void TM1640RGB::setup() {
  gpio_config_t io{};
  io.pin_bit_mask = (1ULL << this->clk_) | (1ULL << this->dio_);
  io.mode = GPIO_MODE_OUTPUT;
  gpio_config(&io);
  // Idle = both lines high.
  gpio_set_level((gpio_num_t) this->dio_, 1);
  gpio_set_level((gpio_num_t) this->clk_, 1);
  esp_rom_delay_us(20);
  // Core 0 at priority 1: the Wi-Fi/lwIP tasks there preempt it (the bus is clocked, a stretched bit is harmless);
  // ESPHome's loop task is on core 1 and is not slowed. The task never ends.
  xTaskCreatePinnedToCore(TM1640RGB::task_, "tm1640rgb", 3072, this, 1, nullptr, 0);
}

void TM1640RGB::dump_config() {
  ESP_LOGCONFIG(TAG, "TM1640RGB:");
  ESP_LOGCONFIG(TAG, "  CLK Pin: GPIO%u", this->clk_);
  ESP_LOGCONFIG(TAG, "  DIO Pin: GPIO%u", this->dio_);
  ESP_LOGCONFIG(TAG, "  Half bit: %u us", (unsigned) this->half_us_);
  ESP_LOGCONFIG(TAG, "  Frames since boot: %u", (unsigned) this->frames_);
}

void TM1640RGB::set_digit(uint8_t d, uint8_t segs, uint8_t r, uint8_t g, uint8_t b) {
  if (d > 3)
    return;
  this->segs_[d] = segs;
  this->level_[d][0] = r;
  this->level_[d][1] = g;
  this->level_[d][2] = b;
  for (int k = 0; k < 3; k++)
    this->mask_[d][k] = 0;
}

void TM1640RGB::set_digit_rgb(uint8_t d, uint8_t r, uint8_t g, uint8_t b) {
  if (d > 3)
    return;
  for (int k = 0; k < 3; k++)
    this->level_[d][k] = 0;
  this->mask_[d][0] = r;
  this->mask_[d][1] = g;
  this->mask_[d][2] = b;
}

void TM1640RGB::set_small(uint8_t i, uint8_t bits) {
  if (i < 3)
    this->small_[i] = bits;
}

void TM1640RGB::task_(void *arg) {
  auto *self = static_cast<TM1640RGB *>(arg);
  // The data command (auto-increment) once: it stays set (p5). The display command only when the brightness
  // changes: sent with every frame, the display flickers.
  self->command_(CMD_DATA_AUTO);
  for (;;) {
    for (int i = 0; i < 64; i++)
      self->frame_();
    vTaskDelay(1);  // the idle task must run now and then (task watchdog)
  }
}

void TM1640RGB::frame_() {
  // One frame: each colour channel of each digit is on with probability level/255 (xorshift32), so on average a level
  // of L/255 is on in L of every 255 frames. Random, because an evenly spread pattern beats against the chip's own
  // scan clock and flickers in phases; random flickers too, only less regularly. 0 and 255 are steady.
  // RAM 3*d + k = digit d, colour k (R, G, B); 12..14 = the small display. set_digit_rgb's masks are always on.
  uint8_t ram[16] = {0};
  for (int d = 0; d < 4; d++) {
    uint8_t segs = this->segs_[d];
    for (int k = 0; k < 3; k++) {
      ram[3 * d + k] = this->mask_[d][k];
      uint8_t level = this->level_[d][k];
      if (level == 0)
        continue;
      this->rng_ ^= this->rng_ << 13;
      this->rng_ ^= this->rng_ >> 17;
      this->rng_ ^= this->rng_ << 5;
      if (level == 255 || (this->rng_ % 255) < level)
        ram[3 * d + k] |= segs;
    }
  }
  for (int i = 0; i < 3; i++)
    ram[12 + i] = this->small_[i];
  this->start_();
  this->send_byte_(CMD_ADDR);
  for (uint8_t v : ram)
    this->send_byte_(v);
  this->stop_();
  uint8_t b = this->brightness_;
  if (b != this->sent_brightness_) {
    this->command_(CMD_DISP_ON | b);
    this->sent_brightness_ = b;
  }
  this->frames_ = this->frames_ + 1;
}

void TM1640RGB::start_() {
  // START = DIN falls while SCLK is high (lines idle high).
  gpio_set_level((gpio_num_t) this->dio_, 0);
  this->half_bit_();
}

void TM1640RGB::stop_() {
  // STOP = DIN rises while SCLK is high; DIN may only change while SCLK is low.
  gpio_set_level((gpio_num_t) this->clk_, 0);
  this->half_bit_();
  gpio_set_level((gpio_num_t) this->dio_, 0);
  this->half_bit_();
  gpio_set_level((gpio_num_t) this->clk_, 1);
  this->half_bit_();
  gpio_set_level((gpio_num_t) this->dio_, 1);
  this->half_bit_();
}

void TM1640RGB::send_byte_(uint8_t b) {
  // LSB first, latched on the rising SCLK edge.
  for (uint8_t i = 0; i < 8; i++) {
    gpio_set_level((gpio_num_t) this->clk_, 0);
    this->half_bit_();
    gpio_set_level((gpio_num_t) this->dio_, b & 0x01);
    this->half_bit_();
    gpio_set_level((gpio_num_t) this->clk_, 1);
    this->half_bit_();
    b >>= 1;
  }
}

void TM1640RGB::command_(uint8_t cmd) {
  this->start_();
  this->send_byte_(cmd);
  this->stop_();
}

}  // namespace esphome::tm1640rgb
