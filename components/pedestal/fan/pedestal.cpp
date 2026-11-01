#include "pedestal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

namespace esphome {
namespace pedestal {

static const char *const TAG = "pedestal.fan";

static const uint16_t TGL_POWER =  0b110110000001;
static const uint16_t TGL_OSC =    0b110110001000;
static const uint16_t SPEED_JUMP = 0b110110000100;
static const uint16_t SPEED_UP   = 0b110110010000;
static const uint16_t SPEED_DOWN = 0b110110100000;

// Upper duty-cycle bound (%) of each speed 0..12 on the speed line. Calibrated on the master bedroom pedestal fan
// (2026-11-01, measured: 1=18.4 2=25.6 3=33.6 4=39.6 5=47.6 6=54.4 7=61.6 8=68.4 9=75.6 10=83.6 11=90.4 12=100 %),
// each bound midway between neighbours. ssieb's original {5, 13, 20, ... 78, 90, 100} read this fan one step high.
static const float duty_to_speed[13] = {9.0f, 22.0f, 29.6f, 36.6f, 43.6f, 51.0f, 58.0f, 65.0f, 72.0f, 79.6f, 87.0f, 95.2f, 100.0f};

void PedestalFan::setup() {
  this->traits_ = fan::FanTraits(true, true, false, 12);
  this->pulse_sensor_->add_on_state_callback([this](float state) {
    this->update_speed_(state);
  });
  this->osc_pin_->setup();
}

void PedestalFan::loop() {
  uint32_t now = millis();
  if (this->waiting_) {
    if (now < this->wait_until_)
      return;
    this->waiting_ = false;
  }
  if (!this->to_send_.empty()) {
    this->waiting_ = true;
    this->wait_until_ = now + 100;
    this->transmit_data_(this->to_send_.front());
    this->to_send_.pop_front();
    return;
  }
  if (this->changing_) {
    if (now - this->last_change_ < 1000)
      return;
    this->changing_ = false;
  }
  bool changed = false;
  int speed = this->measured_speed_;
  if ((speed > 0) != this->state) {
    this->state = (speed > 0);
    changed = true;
  }
  if (speed != this->speed) {
    this->speed = speed;
    changed = true;
  }
  bool osc_state = this->osc_pin_->digital_read();
  if (osc_state != this->oscillating) {
    this->oscillating = osc_state;
    changed = true;
  }
  if (changed)
    this->publish_state();
}

void PedestalFan::dump_config() {
  LOG_FAN("", "Pedestal Fan", this);
  LOG_PIN("  Osc Pin: ", this->osc_pin_);
}

void PedestalFan::control(const fan::FanCall &call) {
  bool changed = false;
  if (call.get_state().has_value()) {
    bool new_state = *call.get_state();
    if (this->state != new_state) {
      this->state = new_state;
      this->to_send_.push_back(TGL_POWER);
      changed = true;
    }
  }
  if (call.get_speed().has_value() && !changed) {
    int new_speed = *call.get_speed();
    if (this->speed != new_speed) {
      if (new_speed > this->speed) {
        for (int i = 0; i < new_speed - this->speed; i++)
          this->to_send_.push_back(SPEED_UP);
      } else {
        for (int i = 0; i < this->speed - new_speed ; i++)
          this->to_send_.push_back(SPEED_DOWN);
      }
      this->speed = new_speed;
      changed = true;
    }
  }
  if (call.get_oscillating().has_value()) {
    bool new_osc = *call.get_oscillating();
    if (this->oscillating != new_osc) {
      this->oscillating = new_osc;
      this->to_send_.push_back(TGL_OSC);
      changed = true;
    }
  }
  if (changed) {
    this->changing_ = true;
    this->last_change_ = millis();
    this->publish_state();
  }
}

void PedestalFan::update_speed_(float value) {
  value = clamp(value, 0.0f, 100.0f);
  int speed;
  for (speed = 0; value > duty_to_speed[speed]; speed++);
  ESP_LOGD(TAG, "detected speed is %d", speed);
  if (speed != this->measured_speed_) {
    this->measured_speed_ = speed;
    this->last_change_ = millis();
  }
}

void PedestalFan::transmit_data_(uint16_t msg) {
  ESP_LOGD(TAG, "transmitting code %02x", msg);
  auto transmit = this->transmitter_->transmit();
  auto *data = transmit.get_data();

  data->set_carrier_frequency(0);
  data->reserve(24);
  for (int b = 0x800; b; b >>= 1) {
    if (msg & b )
      data->item(1296, 406);
    else
      data->item(442, 1252);
  }
  transmit.perform();
}

}  // namespace pedestal
}  // namespace esphome
