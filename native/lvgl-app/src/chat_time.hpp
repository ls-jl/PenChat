#pragma once
#include "common.hpp"
#include "reasoning.hpp"
#include <ctime>
namespace penchat {
inline int64_t chatTimestamp(const Json &o, const char *key) {
  if (!o.is_object()) return 0;
  auto i=o.find(key);
  if(i==o.end() || !i->is_number_integer())return 0;
  auto value=i->get<int64_t>();
  return value>=946684800000LL && value<=253402300799999LL?value:0;
}
inline Json publicTimeline(const Json &value) {
  Json out=Json::object();
  for(auto key:{"sent_at","request_started_at","completed_at","last_activity_at"}) {
    auto time=chatTimestamp(value,key);if(time)out[key]=time;
  }
  if(!out.empty())out["version"]=1;
  return out;
}
inline int64_t sessionChatTime(const Json &session) {
  auto meta=objectField(session,"metadata_json");
  auto stamp=chatTimestamp(meta.value("timeline",Json::object()),"last_activity_at");
  return stamp?stamp:chatTimestamp(session,"last_message_at");
}
inline std::string historyTime(int64_t stamp,int64_t now=nowMs(),bool full=false) {
  if(!chatTimestamp(Json{{"t",stamp}},"t"))return {};
  time_t seconds=stamp/1000,current=now/1000;tm date{},today{};
  if(!localtime_r(&seconds,&date) || !localtime_r(&current,&today))return {};
  const bool sameYear=date.tm_year==today.tm_year;
  const bool sameDay=sameYear && date.tm_yday==today.tm_yday;
  const char *format=full?"%Y年%m月%d日 %H:%M":sameDay?"%H:%M":sameYear?"%m月%d日 %H:%M":"%Y年%m月%d日 %H:%M";
  char output[80];strftime(output,sizeof(output),format,&date);return output;
}
inline int chatDayKey(int64_t stamp=nowMs()) {
  time_t seconds=stamp/1000;tm date{};
  return localtime_r(&seconds,&date)?date.tm_year*400+date.tm_yday:0;
}
} // namespace penchat
