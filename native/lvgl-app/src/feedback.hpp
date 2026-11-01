#pragma once
#include "network.hpp"

namespace penchat {
// This is an allowlisted diagnostic stream, never the UI diagnostics/raw stderr.
void beginDiagnostics(const std::string &directory) noexcept;
void diagnosticRecord(const std::string &category, const std::string &event,
                      const Json &fields = Json::object()) noexcept;
void flushDiagnostics() noexcept;
Json diagnosticRecords();
Json feedbackScreen(const Json &configuration, const Screen &effective);
Json feedbackHardware(bool native);
std::string feedbackUuid();
void feedbackWrite(const std::string &path, const Json &value);
Json feedbackRead(const std::string &path, size_t limit);

struct FeedbackConfig {
  std::string endpoint, token;
  std::vector<std::string> nodes;
  int retryUnitMs = 1000; // Test acceleration is available only in host builds.
  bool enabled() const { return !endpoint.empty() && !token.empty(); }
  static FeedbackConfig build();
};

// A step-scoped read-only observer. It never grabs or synthesizes an input event.
class FeedbackKeys {
  struct Impl;
  std::unique_ptr<Impl> p_;
public:
  explicit FeedbackKeys(std::function<void(Json)> completed);
  ~FeedbackKeys();
  void start(const std::string &step, uint64_t generation);
  void cancel();
  void join();
};

class Feedback {
  struct Impl;
  std::unique_ptr<Impl> p_;
public:
  Feedback(Events &, const std::string &data, const Json &screen, bool native,
           FeedbackConfig config = FeedbackConfig::build());
  ~Feedback();
  void request(const std::string &operation, Json args = Json::object());
  Json state() const;
  std::string previewPage(size_t page) const;
  void background();
  void stop();
};
} // namespace penchat
