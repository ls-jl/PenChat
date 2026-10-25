#pragma once
#include <cassert>
#include <cstdio>
#include <ftw.h>
#include <string>

namespace test {
// Only mkdtemp-created test fixtures are accepted. FTW_PHYS unlinks symlinks
// without traversing their targets; filenames are never interpreted as code.
inline void removeTree(const std::string &path) {
  assert(path.compare(0, 13, "/tmp/penchat-") == 0);
  assert(path.find("/../") == std::string::npos);
  const int result = nftw(path.c_str(),
                         [](const char *file, const struct stat *, int,
                            struct FTW *) { return std::remove(file); },
                         16, FTW_DEPTH | FTW_PHYS);
  assert(result == 0);
}
} // namespace test
