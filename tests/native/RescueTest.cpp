#include "Rescue.hpp"
#include <cassert>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>
int main(){
  char folder[]="/tmp/penchat-rescue-XXXXXX";assert(mkdtemp(folder));std::string root=folder;
  auto write=[](const std::string &path,const std::string &value){std::ofstream out(path,std::ios::binary);out<<value;};
  write(root+"/model","RK3562 Y07");write(root+"/screen",R"({"screen":{"width":568,"height":1210,"direction":180}})");
  auto e=penchat::rescue::environment(root,root+"/model",root+"/screen");assert(e["platform"]=="rk3562"&&!e["compatible"].get<bool>()&&e["screen"]["width"]==568);
  write(root+"/model","CV1826 Plum");write(root+"/screen",R"({"screen":{"width":260,"height":640,"direction":270}})");
  e=penchat::rescue::environment(root,root+"/model",root+"/screen");assert(e["platform"]=="cv1826-s"&&e["screen"]["width"]==640&&e["screen"]["height"]==260);
  write(root+"/model","unknown");e=penchat::rescue::environment(root,root+"/model",root+"/screen");assert(e["platform"]==""&&!e["compatible"].get<bool>());
  std::string elf(64,'\0');elf.replace(0,4,"\177ELF");elf[4]=2;elf[5]=1;elf[18]=char(183);write(root+"/elf",elf);
  penchat::rescue::verifyElf(root+"/elf","rk3562");bool rejected=false;try{penchat::rescue::verifyElf(root+"/elf","cv1826-s");}catch(...){rejected=true;}assert(rejected);
  assert(penchat::rescue::schema(root)==-1&&!penchat::rescue::nativeAlive(root));
  assert(penchat::rescue::installationFlags(PENCHAT_VERSION,"")==0);
  assert(penchat::rescue::installationFlags("9.0.0","")==0);
  assert(penchat::rescue::installationFlags("0.3.2","2baaeac4c3cd6d6b990a5694431a367d8946d6ce7631a6929e92de5d64f0f4f4")==0x4000);
  assert(penchat::rescue::installationFlags("0.3.2","91e23dcef405d849e17757656cd2603ecc21cc9d20437bf38937c6b13b664a58")==0x4000);
  assert(penchat::rescue::installationFlags("0.3.2","5a1017889987de01315d685b28fb832e197359aad48b2e7b9b585eb8f3995e26")==0x4000);
  assert(penchat::rescue::installationFlags("0.3.2","11e46687d3c7301d3049d02d84144ee37e5a9994ca5ec1e9f5eaf278db92519b")==0);
  rejected=false;try{penchat::rescue::installationFlags("0.3.2",std::string(64,'0'));}catch(...){rejected=true;}assert(rejected);
  for(auto file:{"model","screen","elf"})unlink((root+"/"+file).c_str());rmdir(root.c_str());
  std::cout<<"RESCUE_ENVIRONMENT_GEOMETRY_AND_WRONG_ABI_REJECTION_OK\n";
}
