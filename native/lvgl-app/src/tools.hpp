#pragma once
#include "common.hpp"
namespace penchat {
bool webEnabled(const Json &preferences, const Json &settings);
Json webToolDefinitions();
Json wireToolDefinitions(const Json &model);
void validateToolModel(const Json &model);
void streamToolChat(Json model, Json input, std::atomic<bool> &cancel, Events &events,
                    int generation);
void streamModelRound(const Json &model, Json input, std::atomic<bool> &cancel, Events &events,
                      int generation);
// Every call is executed outside LVGL. One caller per instance.
class WebProcess {
  int fd_ = -1, child_ = -1;
  uint64_t sequence_ = 0;
  std::string binary_;
  void start(std::atomic<bool> &cancel);
  void exchange(void *data, size_t size, bool write, std::atomic<bool> &cancel, int64_t deadline);

public:
  explicit WebProcess(std::string binary) : binary_(std::move(binary)) {}
  ~WebProcess();
  void close();
  Json call(const std::string &name, const Json &arguments, std::atomic<bool> &cancel,
            int timeoutMs = 15000);
};
Json publicWebMetadata(const Json &metadata);
} // namespace penchat
