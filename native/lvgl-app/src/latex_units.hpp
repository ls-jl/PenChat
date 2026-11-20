#pragma once
#include "latex_syntax.hpp"
namespace penchat { namespace latex {
struct UnitOptions {
  std::string decimal=".",separator="\\,",group="false",per="reciprocal",product="\\,",round="none",range=" to ";
  int precision=2;bool separate=false,repeat=true;
};
bool unitCommand(const std::string &name,const std::string &source,size_t &position,std::string &output,UnitOptions &options);
} }
