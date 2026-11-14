#pragma once
#include "common.hpp"
#include "refresh_policy.hpp"
namespace penchat {
// Zero is an absent/invalid override, not a third user-selectable setting.
inline int configuredRefreshRate(const Json &settings) {
  const auto value=settings.value("ui_refresh_hz",Json());
  if(!value.is_number_integer())return 0;
  if(value==30)return 30;
  if(value==60)return 60;
  return 0;
}
inline int savedRefreshRate(const Json &settings,int width,int height) {
  return refresh::resolveRate(configuredRefreshRate(settings),width,height);
}
} // namespace penchat
