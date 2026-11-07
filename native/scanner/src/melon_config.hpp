#pragma once
#include "profile.hpp"
#include <nlohmann/json.hpp>
#include <stdexcept>
namespace penchat_scan {
inline unsigned configNumber(const nlohmann::json &value, unsigned maximum) {
  std::string s;
  if (value.is_string())
    s = value.get<std::string>();
  else if (value.is_number_unsigned() || value.is_number_integer())
    s = value.dump();
  if (s.empty() || s.size() > 10 ||
      s.find_first_not_of("0123456789") != std::string::npos)
    throw std::runtime_error("invalid MELON hardware configuration");
  const auto n = std::stoull(s);
  if (n > maximum)
    throw std::runtime_error("invalid MELON hardware configuration");
  return unsigned(n);
}
inline CaptureProfile melonConfig(CaptureProfile p, const nlohmann::json &keys,
                                  const nlohmann::json &leds) {
  const auto &k = keys.at("RK3562_Melon_V0");
  const auto &l = leds.at("RK3562_Melon_V0");
  const auto head = k.at("head_key_id").get<std::string>();
  const auto home = k.at("home_key_id").get<std::string>();
  if ((head != "led_control" && head != "platform-led_control-event") ||
      (home != "adc-keys" && home != "platform-adc-keys-event") ||
      configNumber(k.at("head_key_code"), 767) != 134 ||
      configNumber(k.at("home_key_code"), 767) != 102)
    throw std::runtime_error("MELON key configuration mismatch");
  p.keyCode = 134;
  p.homeName = "adc-keys";
  p.homeCode = 102;
  p.gpio = configNumber(l.at("ir_gpio_number"), 4095);
  if (p.gpio == configNumber(l.at("white_gpio_number"), 4095))
    throw std::runtime_error("MELON scan light configuration mismatch");
  return p;
}
} // namespace penchat_scan
