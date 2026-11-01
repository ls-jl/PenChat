#include "feedback.hpp"
#include <iostream>
#include <unistd.h>
using namespace penchat;
int main(int argc,char **argv) {
  if(argc!=2)return 2;
  try {
    if(std::string(argv[1])=="--synthetic-once") {
      const auto config=FeedbackConfig::build();if(!config.enabled())return 3;
      Json body={{"event_id",feedbackUuid()},{"synthetic_test",true},{"app",{{"name",PENCHAT_NAME},{"version",PENCHAT_VERSION}}},
        {"description","PenChat developer synthetic integration test; no user data, hardware identifiers or runtime logs."}};
      std::atomic<bool> cancel{false};ApiOptions options;options.timeout=30;
      const auto response=http(config.endpoint,"POST",{{"Authorization","Bearer "+config.token},{"Content-Type","application/json; charset=utf-8"}},
        body.dump(),cancel,{},options,{}, {"penchat-feedback",config.endpoint,"",""});
      const auto json=Json::parse(response.body,nullptr,false);
      const bool confirmed=response.status==201&&response.curlCode==0&&response.error.empty()&&json.is_object()&&json.value("ok",Json())==true;
      std::cout<<Json{{"event_id",body["event_id"]},{"status",response.status},{"confirmed",confirmed},{"curl_code",response.curlCode},{"requests",1}}.dump()<<std::endl;
      shutdownHttp();return confirmed?0:1;
    }
    if(std::string(argv[1])=="--hardware") {
      const auto cfg=Json::parse(fileText("/etc/miniapp/resources/cfg.json",65536));
      std::cout<<Json{{"screen",feedbackScreen(cfg,Screen::fromConfig(cfg))},{"hardware",feedbackHardware(true)}}.dump()<<std::endl;
      return 0;
    }
    ensureDirectory(argv[1]);beginDiagnostics(argv[1]);
    // Simulate sensitive fields reaching a logging call: none are allowlisted.
    diagnosticRecord("network","request_end",{{"status",401},{"dns_ms",3},{"secret","SECRET_BODY_KEY"},{"url","https://SECRET_URL"},{"code","SECRET_TEXT"}});
    diagnosticRecord("SECRET_BODY_KEY","request_end",{{"count",1}});
    Events events;Screen screen;screen.width=936;screen.height=280;screen.rotation=270;
    Feedback feedback(events,argv[1],feedbackScreen({{"screen",{{"width",936},{"height",280},{"direction",270},{"tp_direction",270},{"dbuffer",true},{"serial","SECRET_SERIAL"}}}},screen),false);
    std::string line;
    while(std::getline(std::cin,line)) {
      const auto request=Json::parse(line);
      const auto op=request.value("op",std::string());
      if(op=="quit"){feedback.stop();shutdownHttp();break;}
      if(op=="crash")_exit(0);
      if(op=="logstress") {
        for(int i=0;i<1000;++i)diagnosticRecord("network","request_end",{{"status",200},{"count",i},{"secret","NEVER_LOG"}});
        flushDiagnostics();std::cout<<"{}"<<std::endl;continue;
      }
      if(op=="preview")std::cout<<Json{{"text",feedback.previewPage(request.value("page",size_t(0)))}}.dump()<<std::endl;
      else if(op=="state")std::cout<<feedback.state().dump()<<std::endl;
      else {feedback.request(op,request.value("args",Json::object()));std::cout<<"{}"<<std::endl;}
    }
    feedback.stop();shutdownHttp();return 0;
  }catch(...){std::cerr<<"Feedback probe failed\n";return 1;}
}
