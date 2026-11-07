#pragma once
#include "melon_config.hpp"
namespace penchat_scan {
inline CaptureProfile s6Config(CaptureProfile p, const nlohmann::json &keys,
                               const nlohmann::json &leds) {
  const auto &k = keys.at("Plum_V0");
  const auto &l = leds.at("Plum_V0");
  if (!p.cvi || k.at("head_key_id") != "platform-led_control-event")
    throw std::runtime_error("S6 scan key identity mismatch");
  p.keyCode = configNumber(k.at("head_key_code"), 767);
  p.homeName = "gpio-keys";
  p.homeCode = configNumber(k.at("home_key_code"), 767);
  // Paper capture needs visible illumination, not the separate IR emitter.
  // Keep the pin in the lease so normal exit and crash recovery restore it.
  p.gpio = configNumber(l.at("white_gpio_number"), 4095);
  if (p.gpio == configNumber(l.at("ir_gpio_number"), 4095))
    throw std::runtime_error("S6 scan light configuration mismatch");
  return p;
}
} // namespace penchat_scan
