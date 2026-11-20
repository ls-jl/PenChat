#include "HeadKeyRoute.hpp"
#include <cassert>
#include <map>
#include <set>
#include <iostream>
using penchat::HeadKeyRoute;
int main() {
  using Mask=HeadKeyRoute::Mask;
  std::map<int,Mask> masks;std::map<int,int> duplicates;std::set<int> closed;
  int next=100,fail=-1;std::vector<int> clients{4,8};
  Mask a(96,255),b(96,255);b[102/8]&=~(1u<<(102%8));masks[4]=a;masks[8]=b;
  HeadKeyRoute::System system{
    [&]{return clients;},[&](int fd){duplicates[++next]=fd;return next;},
    [&](int fd,Mask &m){m=masks[duplicates.at(fd)];return true;},
    [&](int fd,const Mask &m){int source=duplicates.at(fd);if(source==fail)return false;masks[source]=m;return true;},
    [&](int fd){closed.insert(fd);}
  };
  {HeadKeyRoute route(system);assert(route.start());assert(duplicates.size()==2);
    assert(!(masks[4][134/8]&(1u<<(134%8))));
    for(int bit=0;bit<768;++bit)if(bit!=134)assert((masks[4][bit/8]&(1u<<(bit%8)))==(a[bit/8]&(1u<<(bit%8))));
    assert(route.start());assert(duplicates.size()==2);route.stop();assert(masks[4]==a&&masks[8]==b);assert(closed.size()==2);
    route.stop();assert(route.start());}
  assert(masks[4]==a&&masks[8]==b);
  fail=8;{HeadKeyRoute route(system);assert(!route.start());}assert(masks[4]==a&&masks[8]==b);
  clients.clear();{HeadKeyRoute route(system);assert(!route.start());}
  system.read=[](int,Mask &){return false;};clients.assign(1,4);{HeadKeyRoute route(system);assert(!route.start());}
  std::cout<<"Head-key route: per-client mask, Home/other bits, idempotence, rollback, destruction and unavailable device passed\n";
}
