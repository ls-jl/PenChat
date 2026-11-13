#include "markdown.hpp"
#include <cassert>
#include <fstream>
#include <iostream>
#include <set>
using namespace penchat;
static size_t tables(const Document &doc) {
  std::set<int> ids;for(const auto &b:doc.blocks)if(b.kind=="table")ids.insert(b.tableId);
  return ids.size();
}
static std::string text(const Document &doc,const std::string &kind="") {
  std::string result;for(const auto &b:doc.blocks)if(kind.empty()||b.kind==kind)result+=blockPlainText(b)+"\n";
  return result;
}
static void check(const std::string &source,size_t expected) {
  for(bool streaming:{false,true}) {
    const auto doc=parseMarkdown(source,streaming);
    assert(doc.source==source);assert(tables(doc)==expected);
  }
}
int main(int argc,char **argv) {
  const std::string table="| Name | Purpose | Language |\n|---|:---|---:|\n| [demo](https://example.invalid) | useful | C++ |\n";
  check("**Projects**\n"+table,1);
  check("A preceding paragraph.\n"+table,1);
  check("**Projects**\n\n"+table,1);
  check(table,1);
  check("## Projects\n"+table,1);
  check("**First**\n"+table+"\n**Second**\n"+table,2);
  check("Summary\nA | B\n--- | ---\n1 | 2\n",1);
  check("Summary\n | `key` | Value |\n | :--- | ---: |\n | **one** | `two` |\n",1);
  check("Summary\n| A\\|B | C |\n|---|---|\n| x\\|y | z |\n",1);
  check(u8"**仓库列表**\n| 仓库 | 说明 | 语言 |\n|---|---|---|\n| 示例 | 中文说明 | C++ |\n",1);
  check("Summary\r\n| A | B |\r\n|---|---|\r\n| 1 | 2 |\r\n",1);
  // Fences with differing lengths/characters and fence-like inner lines stay
  // literal. Only a valid closing fence can expose later tables.
  for(auto fence:{std::string("```"),std::string("~~~~"),std::string("`````"),std::string("   ```")}) {
    const auto source=fence+"text\n**Literal**\n"+table+fence+"\n";
    check(source,0);
    const auto doc=parseMarkdown(source);
    assert(text(doc,"code").find("|---|:---|---:|")!=std::string::npos);
  }
  check("````text\nSummary\n```\n"+table+"````\n",0);
  check("~~~text\nSummary\n```\n"+table+"~~~\n",0);
  check("```text\nSummary\n"+table,0);
  check("Paragraph\n\n    Summary\n    | A | B |\n    |---|---|\n    | 1 | 2 |\n",0);
  check("Summary `literal\n| A | B |\n|---|---|\n| 1 | 2 |\nend`\n",0);
  check("Summary ``literal ` backtick\n| A | B |\n|---|---|\n| 1 | 2 |\nend``\n",0);
  check("Summary $literal\n| A | B |\n|---|---|\n| 1 | 2 |\nend$\n",0);
  // Ordinary pipes, malformed delimiters, missing/mismatched body cells and
  // single-column pseudo-tables are never repaired.
  for(const std::string source:{
      "Summary\nA | B\nC | D\n",
      "Summary\n| A | B |\n|---|---|\n",
      "Summary\n| A | B |\n|---|---|\ntext without a pipe\n",
      "Summary\n| A | B |\n|---|---|---|\n| 1 | 2 | 3 |\n",
      "Summary\n| A | B |\n|---|---|\n| 1 | 2 | 3 |\n",
      "Summary\n| A | B |\n|--|--|\n| 1 | 2 |\n",
      "Summary\n| A | B |\n|---x|---|\n| 1 | 2 |\n",
      "Summary\n| A |\n|---|\n| 1 |\n",
      "Summary\n\\| A \\| B \\|\n\\|---\\|---\\|\n\\| 1 \\| 2 \\|\n"}) {
    check(source,0);
    assert(text(parseMarkdown(source)).find("|")!=std::string::npos);
  }
  // Containers retain their authored block structure instead of being made
  // loose or broken out into top-level tables by the compatibility pass.
  const auto quote=parseMarkdown("> Summary\n> | A | B |\n> |---|---|\n> | 1 | 2 |\n");
  for(const auto &b:quote.blocks)assert(b.quote==1);
  const auto list=parseMarkdown("- Summary\n  | A | B |\n  |---|---|\n  | 1 | 2 |\n");
  for(const auto &b:list.blocks)assert(b.indent==1);
  // A later real table is still repaired after literal examples have closed.
  check("```text\nSummary\n"+table+"```\n\nSummary\n"+table,1);
  for(size_t n=0;n<table.size();++n) {
    const auto source="Summary\n"+table.substr(0,n);
    const auto doc=parseMarkdown(source,true);assert(doc.source==source);
  }
  if(argc>1) {
    std::ifstream file(argv[1]);const std::string source((std::istreambuf_iterator<char>(file)),{});
    assert(file.good()||file.eof());
    std::cout<<"Private reproduction tables="<<tables(parseMarkdown(source))<<"\n";
  }
  std::cout<<"Table boundaries, fences, indentation, inline code/math, literal pipes, CRLF and streaming prefixes passed\n";
}
