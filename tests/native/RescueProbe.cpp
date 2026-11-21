#include "Rescue.hpp"
#include <iostream>
#include <unistd.h>
int main(int argc,char **argv){
  using penchat::Json;
  if(argc>1&&std::string(argv[1])=="--hold"){for(;;)pause();}
  try{
    std::string line;std::getline(std::cin,line);const auto input=Json::parse(line);
    penchat::rescue::Config c;c.root=input.at("root");c.endpoint=input.value("endpoint",std::string());c.token="FAKE_RESCUE_TOKEN";
    c.fixture={{"platform",input.value("platform",std::string("rk3562"))},{"screen",{{"width",960},{"height",266}}},{"abi_bits",input.value("abi_bits",64)},{"catalog_compatible",input.value("abi_bits",64)==64},{"payload",{{"platform","rk3562"}}}};
    c.retryUnitMs=input.value("retry_unit_ms",1);
    penchat::rescue::Service service(input.at("workspace"),input.at("data"),input.at("public"),c);
    std::cout<<service.bootstrap().dump()<<std::endl;
    while(std::getline(std::cin,line)){
      auto a=Json::parse(line);const auto op=a.value("op",std::string());Json result;
      try{
        if(op=="quit")break;
        if(op=="state")result=service.state();
        else if(op=="cancel"){service.cancel();result=true;}
        else if(op=="report"){service.report(a,a.value("retry",false));result=true;}
        else if(op=="authorize")result=service.authorize(a.at("ticket"));
        else if(op=="verify")result=service.verifyInstalled(a.at("info"));
        else if(op=="callback"){penchat::rescue::recordInstallResult(input.at("data"),a.at("operation"),a.at("state"),a.value("code",0));result=true;}
        else result=service.request(op,a);
      }catch(const std::exception &e){result={{"error",e.what()}};}
      std::cout<<result.dump()<<std::endl;
    }
  }catch(const std::exception &e){std::cerr<<e.what()<<std::endl;return 1;}
}
