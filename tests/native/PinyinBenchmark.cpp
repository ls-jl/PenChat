#include "pinyin_ime.h"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <string>
#include <sys/resource.h>

int main(int argc, char **argv) {
  if (argc < 3 || argc > 4) return 2;
  auto engine = pinyin_ime_init(argv[1], argv[2]);
  if (!engine) return 1;
  pinyin_ime_set_correction(engine, argc > 3 && std::string(argv[3]) == "--correction");
  uint32_t maxStates = 0;
  std::string query;
  while (std::getline(std::cin, query)) {
    const auto start = std::chrono::steady_clock::now();
    pinyin_ime_input(engine, query.c_str());
    const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    std::cout << query << "\t" << elapsed;
    for (int i = 0; i < 48 && i < pinyin_ime_get_candidate_count(engine); ++i) {
      std::cout << "\t" << pinyin_ime_get_candidate(engine, i) << ":"
                << pinyin_ime_candidate_pinyin(engine, i) << ":"
                << pinyin_ime_candidate_consumed(engine, i) << ":"
                << pinyin_ime_candidate_corrected(engine, i);
    }
    maxStates = std::max(maxStates, pinyin_ime_correction_states(engine));
    std::cout << "\n";
  }
  pinyin_ime_destroy(engine);
  struct rusage usage{};
  getrusage(RUSAGE_SELF, &usage);
  std::cout << "#correction_states\t" << maxStates << "\n";
  std::cout << "#rss_kib\t" << usage.ru_maxrss << "\n";
}
