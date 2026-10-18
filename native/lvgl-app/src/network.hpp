#pragma once
#include "common.hpp"
namespace penchat {
class SseParser {
  std::string buffer_, data_, event_;
  std::function<void(const std::string &, const std::string &)> callback_;
  void line(std::string value);

public:
  explicit SseParser(
      std::function<void(const std::string &, const std::string &)> callback)
      : callback_(std::move(callback)) {}
  void feed(const char *data, size_t n);
  void finish();
};
struct HttpResult {
  long status = 0;
  std::string body, error;
};
HttpResult http(const std::string &url, const std::string &method,
                const Json &headers, const std::string &body,
                std::atomic<bool> &cancel,
                std::function<void(const char *, size_t)> chunks = {});
Json requestBody(const Json &model, Json input, bool streaming);
std::string apiRoot(const Json &model);
Json apiHeaders(const Json &model);
void streamChat(const Json &model, Json input, std::atomic<bool> &cancel,
                Events &events, int generation);
std::string generateTitle(Json model, const std::string &question,
                          const std::string &answer, int maxCharacters,
                          std::atomic<bool> &cancel);
} // namespace penchat
