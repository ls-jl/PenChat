#include "transfer_format.hpp"
#include <stdexcept>
namespace penchat {
namespace {
#include "transfer_format_resources.inc"
}
const std::vector<FormatResource> &transferFormatResources() {
  static const std::vector<FormatResource> resources(std::begin(formatFiles), std::end(formatFiles));
  return resources;
}
Json transferFormatDocument(const std::string &topic) {
  std::string name;
  if (topic == "chats") name = "聊天格式.md";
  else if (topic == "presets") name = "预设格式.md";
  else if (topic == "markdown") name = "Markdown导入.md";
  else throw std::runtime_error("未知格式规范，请返回目录重新选择");
  for (const auto &file : transferFormatResources())
    if (name == file.name)
      return {{"topic", topic}, {"title", name.substr(0,name.size()-3)},
              {"format_version", TransferFormatVersion},
              {"text", std::string(reinterpret_cast<const char *>(file.bytes), file.size)}};
  throw std::runtime_error("格式规范资源不可用");
}
} // namespace penchat
