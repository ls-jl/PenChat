// Isolated loopback HTTP/TLS fixture; never shipped or linked into PenChat.
#include <nlohmann/json.hpp>
#include <openssl/ssl.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <csignal>
#include <cstdio>
#include <atomic>
#include <iostream>
#include <mutex>
#include <set>
#include <thread>
#include <vector>
using Json=nlohmann::json;
std::mutex outputMutex,socketsMutex;
std::set<int> sockets;
std::atomic<bool> running{true};
std::atomic<int> streamPhase{0};
bool streamMode=false;
void report(const Json &j){std::lock_guard<std::mutex> l(outputMutex);std::cout<<j.dump()<<std::endl;}
struct Connection {
  int fd;SSL *ssl;int id;
  ~Connection(){
    if(ssl)SSL_free(ssl);
    {std::lock_guard<std::mutex> l(socketsMutex);sockets.erase(fd);}
    close(fd);report({{"event","closed"},{"connection",id}});
  }
  int read(char *p,int n){return ssl?SSL_read(ssl,p,n):recv(fd,p,n,0);}
  bool line(std::string &s){s.clear();char c;while(s.size()<65536){if(read(&c,1)!=1)return false;if(c=='\n')return true;if(c!='\r')s+=c;}return false;}
  bool write(const std::string &s){size_t i=0;while(i<s.size()){int n=ssl?SSL_write(ssl,s.data()+i,int(s.size()-i)):send(fd,s.data()+i,s.size()-i,MSG_NOSIGNAL);if(n<=0)return false;i+=n;}return true;}
};
void serve(int fd,SSL_CTX *ctx,int id){
  Connection c{fd,ctx?SSL_new(ctx):nullptr,id};
  if(ctx){if(!c.ssl)return;SSL_set_fd(c.ssl,fd);if(SSL_accept(c.ssl)!=1)return;}
  std::string line;
  while(running&&c.line(line)){
    if(line.empty())continue;
    auto first=line.find(' '),last=line.rfind(' ');if(first==std::string::npos||last==first)return;
    const auto path=line.substr(first+1,last-first-1);size_t length=0,headerBytes=0;
    try{
      while(c.line(line)&&!line.empty()){
        headerBytes+=line.size();if(headerBytes>65536)return;
        auto at=line.find(':');if(at==std::string::npos)continue;auto key=line.substr(0,at);
        for(auto &ch:key)ch=std::tolower(static_cast<unsigned char>(ch));
        if(key=="content-length")length=std::stoull(line.substr(at+1));
      }
      if(length>32*1024*1024)return;
      char buf[8192];while(length){const auto n=c.read(buf,int(std::min(length,sizeof(buf))));if(n<=0)return;length-=n;}
      report({{"event","request"},{"path",path},{"connection",id}});
      if(streamMode) {
        if(!c.write("HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nTransfer-Encoding: chunked\r\n\r\n"))return;
        auto chunk=[&](const std::string &wire){char length[32];snprintf(length,sizeof(length),"%zx\r\n",wire.size());return c.write(std::string(length)+wire+"\r\n");};
        const bool chat=path.find("chat/completions")!=std::string::npos;
        auto delta=[&](const std::string &text){
          auto value=chat?Json{{"choices",Json::array({{{"delta",{{"content",text}}}}})}}:
              Json{{"type","response.output_text.delta"},{"output_index",1},{"delta",text}};
          return chunk("data: "+value.dump()+"\n\n");
        };
        std::string text;for(int i=0;i<28;++i)text+="Live paragraph "+std::to_string(i)+": reading should remain under the user's control.\n\n";
        if(!delta(text))return;
        for(int phase=1;phase<=2;++phase) {
          while(running&&streamPhase<phase)usleep(20000);
          if(!running)return;
          if(!delta(text))return;
        }
        while(running&&streamPhase<3)usleep(20000);
        if(!running)return;
        const auto done=chat?Json{{"choices",Json::array({{{"delta",Json::object()},{"finish_reason","stop"}}})}}:Json{{"type","response.completed"}};
        if(!chunk("data: "+done.dump()+"\n\n")||(chat&&!chunk("data: [DONE]\n\n"))||!c.write("0\r\n\r\n"))return;
        continue;
      }
      std::string data;std::string media="text/event-stream";
      if(path.find("chat/completions")!=std::string::npos){
        data="data: "+Json({{"choices",Json::array({{{"index",0},{"delta",{{"content","device answer"}}},{"finish_reason","stop"}}})},{"usage",{{"prompt_tokens",10},{"completion_tokens",2},{"total_tokens",12}}}}).dump()+"\n\ndata: [DONE]\n\n";
      }else if(path.find("responses")!=std::string::npos){
        data="data: "+Json({{"type","response.completed"},{"response",{{"status","completed"},{"output",Json::array({{{"type","message"},{"role","assistant"},{"status","completed"},{"content",Json::array({{{"type","output_text"},{"text","device answer"}}})}}})},{"usage",{{"input_tokens",10},{"output_tokens",2},{"total_tokens",12}}}}}}).dump()+"\n\n";
      }else if(path.find("messages")!=std::string::npos){
        const auto frames=Json::array({
          {{"type","message_start"},{"message",{{"role","assistant"},{"content",Json::array()},{"usage",{{"input_tokens",10},{"output_tokens",0}}}}}},
          {{"type","content_block_start"},{"index",0},{"content_block",{{"type","text"},{"text",""}}}},
          {{"type","content_block_delta"},{"index",0},{"delta",{{"type","text_delta"},{"text","device answer"}}}},
          {{"type","content_block_stop"},{"index",0}},
          {{"type","message_delta"},{"delta",{{"stop_reason","end_turn"}}},{"usage",{{"output_tokens",2}}}},
          {{"type","message_stop"}}});
        for(const auto &frame:frames)data+="event: "+frame.at("type").get<std::string>()+"\ndata: "+frame.dump()+"\n\n";
      }else{media="application/json";data=Json({{"connection",id}}).dump();}
      if(!c.write("HTTP/1.1 200 OK\r\nContent-Type: "+media+"\r\nContent-Length: "+std::to_string(data.size())+"\r\n\r\n"+data))return;
    }catch(...){return;}
  }
}
int main(int argc,char **argv){
  signal(SIGPIPE,SIG_IGN);SSL_library_init();SSL_CTX *ctx=nullptr;
  streamMode=argc==2&&std::string(argv[1])=="--stream";
  if(argc==3){ctx=SSL_CTX_new(TLS_server_method());if(!ctx||SSL_CTX_use_certificate_chain_file(ctx,argv[1])!=1||SSL_CTX_use_PrivateKey_file(ctx,argv[2],SSL_FILETYPE_PEM)!=1)return 2;}
  int listener=socket(AF_INET,SOCK_STREAM|SOCK_CLOEXEC,0);sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
  if(listener<0||bind(listener,reinterpret_cast<sockaddr*>(&address),sizeof(address))||listen(listener,16))return 3;
  socklen_t size=sizeof(address);getsockname(listener,reinterpret_cast<sockaddr*>(&address),&size);report({{"event","ready"},{"port",ntohs(address.sin_port)},{"pid",getpid()}});
  std::vector<std::thread> clients;
  std::thread accepter([&]{int id=0;while(running){int fd=accept(listener,nullptr,nullptr);if(fd<0)break;{std::lock_guard<std::mutex> l(socketsMutex);sockets.insert(fd);}clients.emplace_back(serve,fd,ctx,++id);}});
  std::string command;while(std::getline(std::cin,command)){
    if(!command.empty()&&command.back()=='\r')command.pop_back();
    if(command=="stop")break;
    if(command=="more"&&streamPhase<2)++streamPhase;
    if(command=="finish")streamPhase=3;
  }
  running=false;shutdown(listener,SHUT_RDWR);close(listener);accepter.join();
  {std::lock_guard<std::mutex> l(socketsMutex);for(int fd:sockets)shutdown(fd,SHUT_RDWR);}
  for(auto &client:clients)client.join();if(ctx)SSL_CTX_free(ctx);
}
