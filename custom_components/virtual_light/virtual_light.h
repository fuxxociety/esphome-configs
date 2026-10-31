#pragma once

#include "esphome/core/component.h"
#include "esphome/components/light/light_output.h"

namespace esphome {
namespace virtual_light {

class VirtualLight : public light::LightOutput, public Component {
 public:
  // Required by LightOutput
  void setup_state(light::LightState *state) override { state_ = state; }
  light::LightTraits get_traits() override {
    light::LightTraits traits;
    traits.set_supports_brightness(true);
    traits.set_supports_rgb(true);
    return traits;
  }
  void write_state(light::LightState *state) override;

 protected:
  light::LightState *state_{nullptr};
  float red_{0}, green_{0}, blue_{0};
};

}  // namespace virtual_light
}  // namespace esphome
