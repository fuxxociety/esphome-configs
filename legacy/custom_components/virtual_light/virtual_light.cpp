#include "virtual_light.h"
#include "esphome/core/log.h"

namespace esphome {
namespace virtual_light {

static const char *const TAG = "virtual_light";

void VirtualLight::write_state(light::LightState *state) {
  float brightness;
  state->current_values_as_rgb(&red_, &green_, &blue_, &brightness);

  ESP_LOGD(TAG, "VirtualLight updated -> R: %.2f G: %.2f B: %.2f Bright: %.2f",
           red_, green_, blue_, brightness);

  // At this point, you can store to globals, or just keep them in members
  // Example: you could expose red_, green_, blue_ later via sensors
}

}  // namespace virtual_light
}  // namespace esphome
