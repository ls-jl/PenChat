#pragma once
#include "elf_slot.hpp"
namespace penchat_compositor {
struct Rect { int left, top, right, bottom; };
using Present = void (*)(void*, const Rect&, int, int);
}
