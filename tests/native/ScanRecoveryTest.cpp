#include "scanner_lease.hpp"
#include <cassert>
#include <fstream>
#include <iostream>
using namespace penchat;
template<class F>void rejects(F run){bool failed=false;try{run();}catch(...){failed=true;}assert(failed);}
int main() {
  char temporary[]="/tmp/penchat-scan-recovery-XXXXXX";assert(mkdtemp(temporary));
  const auto path=join(temporary,"scanner-lease.json");
  Json owner={{"pid",123},{"start","456"},{"paused",false}};
  Json lease={{"profile","test-cvi"},{"guardians",Json::array({owner})},{"captures",Json::array({owner})}};
  int restored=0,idle=0;
  auto restore=[&](const Json &saved){assert(saved==lease);++restored;};
  auto healthy=[&]{++idle;};
  assert(scan_recovery::recover(path,"test-cvi",restore,healthy).empty());
  assert(restored==0&&idle==0);
  scan_recovery::persist(path,lease);assert(Json::parse(fileText(path))==lease);
  assert(scan_recovery::recover(path,"test-cvi",restore,healthy).empty());
  assert(restored==1&&idle==0&&access(path.c_str(),F_OK)!=0);
  for(const std::string broken:{"","{","null","[]","{}","{\"profile\":\"test-cvi\",\"guardians\":[]}"}) {
    atomicText(path,broken);
    const auto saved=scan_recovery::recover(path,"test-cvi",restore,healthy);
    assert(!saved.empty()&&fileText(saved)==broken&&access(path.c_str(),F_OK)!=0);
    assert(restored==1);unlink(saved.c_str());
  }
  atomicText(path,"");const auto beforeIdle=idle;
  rejects([&]{scan_recovery::recover(path,"test-cvi",restore,[&]{++idle;throw std::runtime_error("owner paused");});});
  assert(idle==beforeIdle+1&&access(path.c_str(),F_OK)==0&&fileText(path).empty());
  scan_recovery::persist(path,lease);
  rejects([&]{scan_recovery::recover(path,"different-board",restore,healthy);});
  rejects([&]{scan_recovery::recover(path,"test-cvi",[](const Json &){throw std::runtime_error("restore failed");},healthy);});
  assert(Json::parse(fileText(path))==lease);
  rejects([&]{scan_recovery::persist(path,Json());});assert(Json::parse(fileText(path))==lease);
  unlink(path.c_str());
  const auto target=join(temporary,"other");atomicText(target,"do not follow");assert(symlink(target.c_str(),path.c_str())==0);
  rejects([&]{scan_recovery::recover(path,"test-cvi",restore,healthy);});assert(fileText(target)=="do not follow");
  rejects([&]{scan_recovery::persist(path,lease);});assert(fileText(target)=="do not follow");
  unlink(path.c_str());unlink(target.c_str());
  atomicText(path,std::string(scan_recovery::maxLeaseBytes+1,'x'));
  rejects([&]{scan_recovery::recover(path,"test-cvi",restore,healthy);});unlink(path.c_str());
  rmdir(temporary);
  std::cout<<"Durable scan leases, corrupt recovery quarantine, ownership refusal and safe paths passed\n";
}
