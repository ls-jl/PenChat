#pragma once
#include <array>
#include <string>
namespace penchat_compositor {
struct FirmwareProfile {
  const char *id,*module,*sha256,*symbol;
  unsigned bits,machine;
};
// Each entry describes a verified calling/pixel/rotation contract. Reference
// hardware is provenance, never a device-name admission gate. A new firmware
// needs evidence and a new entry; exported-symbol similarity alone is not ABI proof.
inline const std::array<FirmwareProfile,1> &firmwareProfiles(){
  static const std::array<FirmwareProfile,1> profiles{{
    {"cvi-falcon-postfb-v1","libfalcon.so","11e46687d3c7301d3049d02d84144ee37e5a9994ca5ec1e9f5eaf278db92519b",
     "_ZN7greenui7post_fbEPvRKNS_4RectEii",32,40}}};
  return profiles;
}
inline const FirmwareProfile *firmwareProfile(const std::string &sha,unsigned bits,unsigned machine){
  const FirmwareProfile *found=nullptr;
  for(const auto &entry:firmwareProfiles())if(sha==entry.sha256&&bits==entry.bits&&machine==entry.machine){if(found)return nullptr;found=&entry;}
  return found;
}
} // namespace penchat_compositor
