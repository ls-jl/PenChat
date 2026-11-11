#pragma once
#include "profile.hpp"
#include <algorithm>
#include <nlohmann/json.hpp>

namespace penchat_scan {
inline unsigned configNumber(const nlohmann::json &value, unsigned maximum) {
  const auto text=value.is_string()?value.get<std::string>():value.is_number_integer()?value.dump():std::string();
  if (text.empty() || text.size()>10 || text.find_first_not_of("0123456789")!=std::string::npos)
    throw std::runtime_error("invalid scan hardware configuration");
  const auto n=std::stoull(text);
  if (n>maximum) throw std::runtime_error("invalid scan hardware configuration");
  return unsigned(n);
}
inline CaptureProfile boardConfig(CaptureProfile p, const nlohmann::json &keys,
                                  const nlohmann::json &leds) {
  const auto &spec=p.boardIo;
  if (spec.section.empty()) return p;
  const auto &k=keys.at(spec.section), &l=leds.at(spec.section);
  const auto member=[](const std::vector<std::string> &names,const std::string &name) {
    return std::find(names.begin(),names.end(),name)!=names.end();
  };
  if (!member(spec.headNames,k.at("head_key_id").get<std::string>()) ||
      (!spec.homeNames.empty() && !member(spec.homeNames,k.at("home_key_id").get<std::string>())))
    throw std::runtime_error("scan key identity mismatch");
  p.keyCode=configNumber(k.at("head_key_code"),767);
  p.homeCode=configNumber(k.at("home_key_code"),767);
  if ((spec.expectedHeadCode>=0 && p.keyCode!=unsigned(spec.expectedHeadCode)) ||
      (spec.expectedHomeCode>=0 && p.homeCode!=unsigned(spec.expectedHomeCode)))
    throw std::runtime_error("scan key configuration mismatch");
  p.gpio=configNumber(l.at(spec.lampField),4095);
  if (p.gpio==configNumber(l.at(spec.otherLampField),4095))
    throw std::runtime_error("scan light configuration mismatch");
  validateProfile(p);
  return p;
}
} // namespace penchat_scan
