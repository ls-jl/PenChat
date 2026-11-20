#pragma once
#include "md4c.h"
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>
namespace penchat {
namespace detail {
struct MarkdownRange { size_t begin=0,end=0; };
inline MarkdownRange tableTrim(const std::string &s,MarkdownRange r) {
  while(r.begin<r.end&&(s[r.begin]==' '||s[r.begin]=='\t'||s[r.begin]=='\r'))++r.begin;
  while(r.end>r.begin&&(s[r.end-1]==' '||s[r.end-1]=='\t'||s[r.end-1]=='\r'))--r.end;
  return r;
}
inline std::vector<MarkdownRange> tableCells(const std::string &s,MarkdownRange line) {
  // Four-space/tab-indented code and container prefixes are not repaired here.
  size_t at=line.begin;
  while(at<line.end&&s[at]==' ')++at;
  if(at-line.begin>=4||(at<line.end&&s[at]=='\t'))return {};
  line=tableTrim(s,line);
  std::vector<MarkdownRange> cells;
  size_t first=line.begin;bool pipe=false;
  for(size_t i=first;i<line.end;++i) {
    if(s[i]=='\\'&&i+1<line.end){++i;continue;}
    if(s[i]!='|')continue;
    pipe=true;cells.push_back(tableTrim(s,{first,i}));first=i+1;
    if(cells.size()>128)return {};
  }
  if(!pipe)return {};
  cells.push_back(tableTrim(s,{first,line.end}));
  if(!cells.empty()&&cells.front().begin==cells.front().end&&line.begin<line.end&&s[line.begin]=='|')cells.erase(cells.begin());
  if(!cells.empty()&&cells.back().begin==cells.back().end&&first==line.end)cells.pop_back();
  return cells;
}
inline bool tableDelimiter(const std::string &s,const std::vector<MarkdownRange> &cells) {
  if(cells.size()<2)return false;
  for(auto cell:cells) {
    if(cell.begin<cell.end&&s[cell.begin]==':')++cell.begin;
    if(cell.begin<cell.end&&s[cell.end-1]==':')--cell.end;
    if(cell.end-cell.begin<3)return false;
    for(size_t i=cell.begin;i<cell.end;++i)if(s[i]!='-')return false;
  }
  return true;
}
struct TableBoundaryProbe {
  struct Candidate {size_t header,body,end;bool paragraph=false,protectedSpan=false;};
  const std::string &source;
  std::vector<Candidate> candidates;
  unsigned container=0,code=0;
  bool paragraph=false;
  size_t first=std::string::npos,last=0,codeFirst=std::string::npos,codeLast=0;
  explicit TableBoundaryProbe(const std::string &s):source(s){}
  void protect() {
    if(codeFirst==std::string::npos||source.find('\n',codeFirst)>=codeLast)return;
    auto it=std::lower_bound(candidates.begin(),candidates.end(),codeFirst,
      [](const Candidate &c,size_t begin){return c.end<=begin;});
    for(;it!=candidates.end()&&it->header<codeLast;++it)it->protectedSpan=true;
  }
  static bool containerBlock(MD_BLOCKTYPE t) {return t==MD_BLOCK_QUOTE||t==MD_BLOCK_UL||t==MD_BLOCK_OL;}
  static int enterBlock(MD_BLOCKTYPE t,void *,void *ctx) {
    auto &p=*static_cast<TableBoundaryProbe *>(ctx);
    if(containerBlock(t))++p.container;
    if(t==MD_BLOCK_P&&!p.container){p.paragraph=true;p.first=std::string::npos;p.last=0;}
    return 0;
  }
  static int leaveBlock(MD_BLOCKTYPE t,void *,void *ctx) {
    auto &p=*static_cast<TableBoundaryProbe *>(ctx);
    if(t==MD_BLOCK_P&&p.paragraph) {
      auto it=std::upper_bound(p.candidates.begin(),p.candidates.end(),p.first,
        [](size_t begin,const Candidate &c){return begin<c.header;});
      for(;it!=p.candidates.end()&&it->body<p.last;++it)it->paragraph=true;
      p.paragraph=false;
    }
    if(containerBlock(t))--p.container;
    return 0;
  }
  static bool protectedType(MD_SPANTYPE t) {return t==MD_SPAN_CODE||t==MD_SPAN_LATEXMATH||t==MD_SPAN_LATEXMATH_DISPLAY;}
  static int enterSpan(MD_SPANTYPE t,void *,void *ctx) {
    auto &p=*static_cast<TableBoundaryProbe *>(ctx);
    if(p.paragraph&&protectedType(t)) {
      if(!p.code){p.codeFirst=std::string::npos;p.codeLast=0;}
      ++p.code;
    }
    return 0;
  }
  static int leaveSpan(MD_SPANTYPE t,void *,void *ctx) {
    auto &p=*static_cast<TableBoundaryProbe *>(ctx);
    if(p.paragraph&&protectedType(t)&&p.code) {
      --p.code;if(!p.code)p.protect();
    }
    return 0;
  }
  static int text(MD_TEXTTYPE,const MD_CHAR *text,MD_SIZE length,void *ctx) {
    auto &p=*static_cast<TableBoundaryProbe *>(ctx);
    if(!p.paragraph)return 0;
    // MD4C also emits synthetic newline/space buffers. Do not subtract unrelated
    // pointers or treat those buffers as locations in the original document.
    const auto base=reinterpret_cast<uintptr_t>(p.source.data()),at=reinterpret_cast<uintptr_t>(text);
    if(at<base||at-base>p.source.size()||length>p.source.size()-(at-base))return 0;
    const size_t first=at-base,last=first+length;
    p.first=std::min(p.first,first);p.last=std::max(p.last,last);
    if(p.code){p.codeFirst=std::min(p.codeFirst,first);p.codeLast=std::max(p.codeLast,last);}
    return 0;
  }
};
// Only repair proven top-level paragraph boundaries. MD4C itself supplies code,
// math and container scope, so fence lookalikes cannot expose literal examples.
inline std::string normalizeTableBoundaries(std::string input) {
  TableBoundaryProbe probe(input);
  MarkdownRange previous{};
  bool beforeHeader=false;
  for(size_t start=0;start<input.size();) {
    const auto lf=input.find('\n',start);
    const size_t end=lf==std::string::npos?input.size():lf;
    const MarkdownRange current{start,end};
    if(beforeHeader&&lf!=std::string::npos) {
      const auto delimiter=tableCells(input,current);
      if(tableDelimiter(input,delimiter)) {
        const auto header=tableCells(input,previous);
        const size_t bodyStart=lf+1,bodyLf=input.find('\n',bodyStart);
        const MarkdownRange body{bodyStart,bodyLf==std::string::npos?input.size():bodyLf};
        const auto cells=tableCells(input,body);
        bool content=false;for(auto cell:cells)content|=cell.begin<cell.end;
        if(header.size()==delimiter.size()&&cells.size()==delimiter.size()&&content)
          probe.candidates.push_back({previous.begin,body.begin,body.end,false,false});
        if(probe.candidates.size()==4096)break;
      }
    }
    const auto trimmed=tableTrim(input,previous);
    beforeHeader=trimmed.begin<trimmed.end;
    previous=current;
    if(lf==std::string::npos)break;
    start=lf+1;
  }
  if(probe.candidates.empty())return input;
  MD_PARSER parser{};
  parser.flags=MD_DIALECT_GITHUB|MD_FLAG_LATEXMATHSPANS|MD_FLAG_NOHTML;
  parser.enter_block=TableBoundaryProbe::enterBlock;parser.leave_block=TableBoundaryProbe::leaveBlock;
  parser.enter_span=TableBoundaryProbe::enterSpan;parser.leave_span=TableBoundaryProbe::leaveSpan;parser.text=TableBoundaryProbe::text;
  if(md_parse(input.data(),input.size(),&parser,&probe))return input;
  std::string out;size_t begin=0;
  for(const auto &c:probe.candidates)if(c.paragraph&&!c.protectedSpan) {
    if(!begin)out.reserve(input.size()+probe.candidates.size());
    out.append(input,begin,c.header-begin);out+='\n';begin=c.header;
  }
  if(!begin)return input;
  out.append(input,begin,std::string::npos);return out;
}
} // namespace detail
} // namespace penchat
