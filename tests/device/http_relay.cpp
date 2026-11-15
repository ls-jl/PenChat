// Test-only loopback TCP transport over one authenticated adb-forward stream.
// No HTTP interpretation, process creation, shell, or access to application data.
#include <arpa/inet.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
static volatile sig_atomic_t stopped=0;
static void stop(int){stopped=1;}
static void require(bool ok){if(!ok)throw std::runtime_error("relay transport failure");}
static int listener(unsigned &port){
  int fd=socket(AF_INET,SOCK_STREAM|SOCK_CLOEXEC,0);require(fd>=0);
  sockaddr_in a{};a.sin_family=AF_INET;a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
  require(bind(fd,reinterpret_cast<sockaddr*>(&a),sizeof(a))==0&&listen(fd,32)==0);
  socklen_t n=sizeof(a);require(getsockname(fd,reinterpret_cast<sockaddr*>(&a),&n)==0);port=ntohs(a.sin_port);return fd;
}
static void nonblock(int fd){require(fcntl(fd,F_SETFL,fcntl(fd,F_GETFL)|O_NONBLOCK)==0);}
static void frame(std::string &out,unsigned id,unsigned char type,const char *bytes=nullptr,size_t n=0){
  require(n<=32768&&out.size()+9+n<=4*1024*1024);
  uint32_t a=htonl(id),b=htonl(unsigned(n));out.append(reinterpret_cast<char*>(&a),4);out.append(reinterpret_cast<char*>(&b),4);out.push_back(type);if(n)out.append(bytes,n);
}
static bool flush(int fd,std::string &out){
  if(out.empty())return true;
  auto n=send(fd,out.data(),out.size(),MSG_NOSIGNAL);
  if(n>0)out.erase(0,size_t(n));
  return n>=0||errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR;
}
struct Client {int fd;std::string out;bool closing=false;};
int main(int argc,char **argv){
  int control=-1,acceptor=-1,front=-1;std::map<unsigned,Client> clients;
  try {
    require(argc==2&&std::strlen(argv[1])==64);signal(SIGTERM,stop);signal(SIGINT,stop);signal(SIGPIPE,SIG_IGN);
    unsigned appPort,controlPort;front=listener(appPort);acceptor=listener(controlPort);
    std::printf("{\"pid\":%d,\"port\":%u,\"control\":%u}\n",int(getpid()),appPort,controlPort);std::fflush(stdout);
    pollfd gate{acceptor,POLLIN,0};require(poll(&gate,1,15000)>0);
    control=accept4(acceptor,nullptr,nullptr,SOCK_CLOEXEC);require(control>=0);close(acceptor);acceptor=-1;
    timeval timeout{5,0};setsockopt(control,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
    char token[64];size_t have=0;while(have<sizeof(token)){auto n=recv(control,token+have,sizeof(token)-have,0);require(n>0);have+=size_t(n);}
    require(std::memcmp(token,argv[1],64)==0);nonblock(control);nonblock(front);
    std::string incoming,outgoing;unsigned next=1;char bytes[32768];
    while(!stopped){
      std::vector<pollfd> fds{{control,short(POLLIN|(outgoing.empty()?0:POLLOUT)),0},{front,short(clients.size()<32?POLLIN:0),0}};
      std::vector<unsigned> ids;
      for(auto &entry:clients){ids.push_back(entry.first);fds.push_back({entry.second.fd,short((outgoing.size()<2*1024*1024&&!entry.second.closing?POLLIN:0)|(entry.second.out.empty()?0:POLLOUT)),0});}
      int polled=poll(fds.data(),fds.size(),1000);if(polled<0&&errno==EINTR)continue;require(polled>=0);
      if(fds[0].revents&POLLOUT)require(flush(control,outgoing));
      if(fds[0].revents&(POLLIN|POLLHUP|POLLERR)){
        auto n=recv(control,bytes,sizeof(bytes),0);if(n==0)break;
        require(n>0||errno==EAGAIN||errno==EINTR);if(n>0)incoming.append(bytes,size_t(n));
        require(incoming.size()<=1024*1024);
        while(incoming.size()>=9){
          uint32_t id,len;std::memcpy(&id,incoming.data(),4);std::memcpy(&len,incoming.data()+4,4);id=ntohl(id);len=ntohl(len);
          auto type=static_cast<unsigned char>(incoming[8]);require(len<=32768&&(type==2||type==3));
          if(incoming.size()<9+len)break;
          auto found=clients.find(id);if(found!=clients.end()){
            if(type==2){require(found->second.out.size()+len<=1024*1024);found->second.out.append(incoming.data()+9,len);}
            else {require(len==0);found->second.closing=true;}
          }
          incoming.erase(0,9+len);
        }
      }
      if(fds[1].revents&POLLIN){int fd=accept4(front,nullptr,nullptr,SOCK_NONBLOCK|SOCK_CLOEXEC);if(fd>=0){require(next!=0);clients.emplace(next,Client{fd,{},false});frame(outgoing,next++,1);}}
      for(size_t i=0;i<ids.size();++i){
        auto found=clients.find(ids[i]);if(found==clients.end())continue;auto &c=found->second;bool dead=false;
        if(fds[i+2].revents&POLLOUT)dead=!flush(c.fd,c.out);
        if(!dead&&(fds[i+2].revents&(POLLIN|POLLHUP|POLLERR))&&!c.closing){
          auto n=recv(c.fd,bytes,sizeof(bytes),0);
          if(n>0)frame(outgoing,found->first,2,bytes,size_t(n));
          else if(n==0||(errno!=EAGAIN&&errno!=EWOULDBLOCK&&errno!=EINTR))dead=true;
        }
        if(dead||(c.closing&&c.out.empty())){if(!c.closing)frame(outgoing,found->first,3);close(c.fd);clients.erase(found);}
      }
    }
  }catch(const std::exception &e){std::fprintf(stderr,"%s\n",e.what());stopped=2;}
  for(auto &entry:clients)close(entry.second.fd);
  if(control>=0)close(control);if(acceptor>=0)close(acceptor);if(front>=0)close(front);
  return stopped==2?1:0;
}
