#pragma once
#include "latex_syntax.hpp"
namespace penchat { namespace latex {
bool physicsCommand(const std::string &name,const std::string &source,size_t &position,std::string &output,const syntax::Expand &expand);
} }
