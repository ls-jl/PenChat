#pragma once
#include <string>
#include <vector>
namespace penchat {
struct Document;
namespace latex {
// Pure bounded normalization. Never modifies source or process-global TeX macros.
std::string expand(const std::string &source);
void prepare(Document &document);
std::string boundedMultlined(const std::string &source);
size_t environmentEnd(const std::string &source,size_t at);
}
}
