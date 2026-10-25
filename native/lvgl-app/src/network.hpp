#pragma once
#include "common.hpp"
#include "api_config.hpp"
namespace penchat {
class SseParser {
  std::string buffer_, data_, event_;
  bool carriage_ = false, first_ = true;
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
  std::string body, error, contentType, requestId;
  int64_t elapsedMs = 0;
};
HttpResult http(const std::string &url, const std::string &method,
                const Json &headers, const std::string &body,
                std::atomic<bool> &cancel,
                std::function<void(const char *, size_t)> chunks = {},
                const ApiOptions &options = {},
                std::function<bool()> complete = {});
Json requestBody(const Json &model, Json input, bool streaming);
Json anthropicRequestBody(const Json &model, const Json &input, bool streaming);
void streamAnthropic(const Json &model, Json input, std::atomic<bool> &cancel,
                     Events &events, int generation);
std::string redactApiError(const std::string &error, const Json &model);
Json fetchModelList(const Json &model, std::atomic<bool> &cancel);
std::string apiRoot(const Json &model);
Json apiHeaders(const Json &model);
HttpResult apiRequest(const Json &model, const std::string &resource,
                      const std::string &method, const std::string &body,
                      std::atomic<bool> &cancel,
                      std::function<void(const char *, size_t)> chunks = {},
                      std::function<bool()> complete = {});
void streamChat(const Json &model, Json input, std::atomic<bool> &cancel,
                Events &events, int generation);
std::string generateTitle(Json model, const std::string &question,
                          const std::string &answer, int maxCharacters,
                          std::atomic<bool> &cancel);
} // namespace penchat
