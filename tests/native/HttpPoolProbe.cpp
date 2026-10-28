#include "network.hpp"
#include <iostream>
#include <map>
#include <thread>
using namespace penchat;
std::mutex outputMutex;
void output(Json j){std::lock_guard<std::mutex> lock(outputMutex);std::cout<<j.dump()<<std::endl;}
int main(){
  std::vector<std::thread> threads;
  std::map<std::string,std::shared_ptr<std::atomic<bool>>> cancels;
  std::string line;
  while(std::getline(std::cin,line)){
    if(line.empty()||line=="\r")continue;
    const auto in=Json::parse(line);const auto op=in.value("op",std::string("request"));const auto id=in.value("id",std::string());
    if(op=="exit")break;
    if(op=="configure"){setHttpIdleSeconds(in.at("seconds"));output({{"id",id},{"pool",httpDiagnostics()}});continue;}
    if(op=="diag"){output({{"id",id},{"pool",httpDiagnostics()}});continue;}
    if(op=="invalidate"){invalidateHttpProvider(in.at("provider"));output({{"id",id},{"ok",true}});continue;}
    if(op=="cancel"){if(cancels.count(in.at("target"))) *cancels[in.at("target")]=true;output({{"id",id},{"ok",true}});continue;}
    if(op=="shutdown"){shutdownHttp();output({{"id",id},{"pool",httpDiagnostics()}});continue;}
    auto cancel=std::make_shared<std::atomic<bool>>(false);cancels[id]=cancel;
    threads.emplace_back([in,id,cancel]{
      const auto start=ticks();Json result;
      try{
        if(in.value("op",std::string())=="stream"){
          Events events;streamChat(in.at("model"),in.value("input",Json::array({{{"role","user"},{"content",Json::array({{{"type","input_text"},{"text","fixture"}}})}}})),*cancel,events,1);
          Json rows=Json::array();for(auto &e:events.drain())rows.push_back({{"type",e.type},{"data",e.data}});
          result={{"events",rows}};
        } else {
          HttpResult r;std::string wire;bool ended=false;
          const auto marker=in.value("marker",std::string());
          const auto url=in.at("url").get<std::string>();ApiOptions opts;opts.timeout=in.value("timeout",40);opts.http1=in.value("http1",false);
          auto chunks=marker.empty()?std::function<void(const char*,size_t)>():[&](const char*p,size_t n){wire.append(p,n);ended=wire.find(marker)!=std::string::npos;};
          auto complete=marker.empty()?std::function<bool()>():[&]{return ended;};
          r=http(url,in.value("method",std::string("GET")),in.value("headers",Json::object()),in.value("body",std::string()),*cancel,
                 chunks,opts,complete,{in.value("provider",std::string("p")),in.value("endpoint",std::string("fixture")),in.value("revision",std::string()),""});
          result={{"status",r.status},{"error",r.error},{"body",marker.empty()?r.body:wire},{"timings",r.timings}};
        }
      }catch(const std::exception &e){result={{"error",e.what()}};}
      result["id"]=id;result["returned_ms"]=ticks()-start;output(std::move(result));
    });
  }
  for(auto &t:threads)t.join();shutdownHttp();
}
