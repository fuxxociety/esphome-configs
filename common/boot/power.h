// Why did this chip start? Utility power returning (or a relay energising a bulb) must behave differently from a
// software/watchdog/OTA/Wi-Fi reboot: after power loss, switch relays open, bulbs come up at 100 % and fans stay off;
// after a software reboot everything restores exactly (BEHAVIOR.md §0.5).
//
// Usage in YAML lambdas:  if (fleet::power_on_reset()) { ... }
#pragma once

#include "esphome/core/defines.h"

#if defined(USE_ESP8266)
#include <Esp.h>
#elif defined(USE_ESP32)
#include <esp_system.h>
#elif defined(USE_LIBRETINY)
#include <Arduino.h>
#endif

namespace fleet {

inline bool power_on_reset() {
#if defined(USE_ESP8266)
  // REASON_DEFAULT_RST = power on, REASON_EXT_SYS_RST = reset pin (e.g. supply brown-out supervisor)
  const auto reason = ESP.getResetInfoPtr()->reason;
  return reason == REASON_DEFAULT_RST || reason == REASON_EXT_SYS_RST;
#elif defined(USE_ESP32)
  const auto reason = esp_reset_reason();
  return reason == ESP_RST_POWERON || reason == ESP_RST_BROWNOUT || reason == ESP_RST_EXT;
#elif defined(USE_LIBRETINY)
  const auto reason = lt_get_reboot_reason();
  return reason == REBOOT_REASON_POWER || reason == REBOOT_REASON_BROWNOUT;
#else
  return false;  // unknown platform: treat as a software reboot (restore state)
#endif
}

}  // namespace fleet
