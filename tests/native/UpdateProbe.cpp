#include "update.hpp"
#include <iostream>
using namespace penchat;
int main(int argc,char **argv) {
  if(argc==6&&std::string(argv[1])=="--validate") {
    std::atomic<bool> cancel{false};
    try{std::cout<<updates::validatePackage(argv[2],argv[3],argv[4],argv[5],cancel).dump()<<std::endl;return 0;}
    catch(const std::exception &e){std::cout<<Json{{"error",e.what()}}.dump()<<std::endl;return 1;}
  }
  if(argc!=3)return 2;
  try {
    const auto json=Json::parse(fileText(argv[1],16384));updates::Config config;
    config.root=json.at("root");config.nodes=json.value("nodes",std::vector<std::string>{});
    config.current=json.value("current",std::string("0.2.20"));config.appId=json.value("appid",std::string(PENCHAT_APP_ID));config.autoCheck=json.value("auto",false);
    ensureDirectory(argv[2]);Events events;Updater updater(events,argv[2],config);Json faults=Json::array();
    std::string line;
    while(std::getline(std::cin,line)) {
      const auto request=Json::parse(line);const auto op=request.value("op",std::string());
      for(const auto &event:events.drain())if(event.type=="update_release_fault")faults.push_back(event.data);
      if(op=="quit"){updater.stop();break;}
      if(op=="state"){auto value=updater.state();value["faults"]=faults;std::cout<<value.dump()<<std::endl;}
      else if(op=="cancel"){updater.cancel(request.value("decline",false));std::cout<<"{}"<<std::endl;}
      else{updater.request(op,request.value("args",Json::object()));std::cout<<"{}"<<std::endl;}
    }
    updater.stop();return 0;
  }catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
}
