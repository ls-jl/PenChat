#include "network.hpp"
#include <iostream>
#include <thread>
using namespace penchat;
int main() {
  Json request; std::cin >> request;
  auto model=request.at("model"); std::atomic<bool> cancel{false};
  std::thread stopper;
  const int delay=request.value("cancel_ms",0);
  if(delay)stopper=std::thread([&]{std::this_thread::sleep_for(std::chrono::milliseconds(delay));cancel=true;});
  Events events(true);
  try {
    if(request.value("task",std::string())=="title") {
      const auto result=generateTitle(model,"question","answer",20,cancel);
      events.push("title",{{"text",result}});
    } else if(request.value("task",std::string())=="models") {
      const auto result=apiRequest(model,"/models","GET","",cancel);
      events.push("http",{{"status",result.status},{"error",result.error},{"body",result.body}});
    } else if(request.value("task",std::string())=="timeout") {
      ApiOptions options;options.timeout=1;
      const auto result=http(apiRoot(model)+"/timeout","GET",apiHeaders(model),"",cancel,{},options);
      events.push("http",{{"status",result.status},{"error",result.error}});
    } else streamChat(model,request.value("input",Json::array({{{"role","user"},{"content",Json::array({{{"type","input_text"},{"text","hello"}}})}}})),cancel,events,1);
  } catch(const std::exception &e) {events.push("exception",{{"error",e.what()}});}
  if(stopper.joinable())stopper.join();
  Json output=Json::array();for(const auto &e:events.drain())output.push_back({{"type",e.type},{"data",e.data}});
  std::cout<<output.dump()<<'\n';
}
