#include <cstdlib>
extern "C" int vendorCommand(const char *text) { return std::system(text); }
