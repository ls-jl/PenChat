#include "chat_text.hpp"
#include <cassert>
#include <iostream>
using namespace penchat;
int main() {
  const std::string draft="\n你好\n\n第二段\r\n";
  assert(chatDraftPreview(draft)=="你好 第二段");
  assert(draft=="\n你好\n\n第二段\r\n");
  assert(chatDraftPreview("\n你好")=="你好");
  assert(chatDraftPreview("\r\n\n hello\tworld \n")=="hello world");
  assert(chatDraftPreview("\xe2\x80\xa8中文\xe2\x80\xa9文字")=="中文 文字");
  assert(chatDraftPreview("\n　\t\r\n").empty());
  assert(chatDraftPreview("👩‍💻 好")=="👩‍💻 好");
  std::cout<<"CHAT_DRAFT_SINGLE_LINE_PREVIEW_PRESERVES_ORIGINAL_PARAGRAPHS_OK\n";
}
