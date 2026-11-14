#pragma once
#include "common.hpp"
#include <algorithm>
namespace penchat {
inline int savedUiScale(const Json &settings) {
  const auto value=settings.value("ui_scale_percent",Json());
  if(!value.is_number_integer())return 100;
  return int(std::max(75.,std::min(150.,value.get<double>())));
}
} // namespace penchat
