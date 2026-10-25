#pragma once
#include "common.hpp"
#include <deque>
namespace penchat {
// Owned by the native process. No Falcon/system input-method state is shared.
class PinyinService {
  struct Impl;
  std::unique_ptr<Impl> impl_;
public:
  PinyinService(Events &, const std::string &resources, const std::string &data);
  ~PinyinService();
  bool request(Json request);
  void commit(const Json &records, const std::string &finalText);
  void clear();
  void stop();
};
} // namespace penchat
