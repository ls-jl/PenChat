#include "util.h"
#include <fcntl.h>
#include <unistd.h>
#include <cstdlib>
#include <string>
#include <iostream>

int main() {
  char path[] = "/tmp/penchat-camera-read-XXXXXX";
  const int fd = mkstemp(path);
  if (fd < 0) return 1;
  std::string content(9000, 'x');
  content[0] = '\0'; content[4096] = '\0';
  bool ok = write(fd, content.data(), content.size()) == ssize_t(content.size());
  close(fd);
  for (size_t limit : {size_t(0), size_t(1), size_t(4095), size_t(4096),
                       size_t(4097), size_t(9000), size_t(10000)}) {
    ok = ok && camera::readFile(path, limit) == content.substr(0, limit);
  }
  unlink(path);
  ok = ok && camera::readFile(path, 100).empty();
  if (!ok) { std::cerr << "bounded binary read failed\n"; return 1; }
  std::cout << "Camera bounded reads: zero, binary, short block, exact block, EOF passed\n";
}
