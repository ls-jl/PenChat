// Worker-only adapter for modern libstdc++ headers with the board's GCC 6 CRT.
// This checked array-allocation failure entry was added after that CRT. All
// ordinary allocation/unwind symbols continue to come from the target SDK.
#include <new>
namespace std {
__attribute__((visibility("hidden"), noreturn)) void __throw_bad_array_new_length() {
  throw bad_array_new_length();
}
} // namespace std
