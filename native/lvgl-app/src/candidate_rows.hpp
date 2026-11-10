#pragma once
#include <algorithm>
#include <cstddef>
#include <vector>

namespace penchat {
struct CandidateCell {
  int x, y, width;
  bool separator;
};

// Fill each visible row/page while retaining candidate order and measured text
// widths. Shared extra space becomes padding around text, never a trailing gap.
inline std::vector<CandidateCell> candidateRows(const std::vector<int> &measured,
                                               int available, bool expanded) {
  std::vector<CandidateCell> cells;
  if(available<=0)return cells;
  cells.reserve(measured.size());
  std::size_t begin=0;
  int row=0;
  while(begin<measured.size()) {
    std::size_t end=begin;
    int used=0;
    while(end<measured.size()) {
      const int width=std::min(available,std::max(44,measured[end]));
      if(end>begin&&width>available-used)break;
      used+=width;++end;
    }
    const int count=int(end-begin),extra=available-used;
    int x=expanded?0:row*available;
    for(std::size_t i=begin;i<end;++i) {
      const int width=std::min(available,std::max(44,measured[i]))+
          extra/count+(int(i-begin)<extra%count?1:0);
      cells.push_back({x,expanded?row*48:0,width,expanded?i+1<end:i+1<measured.size()});
      x+=width;
    }
    begin=end;++row;
  }
  return cells;
}
} // namespace penchat
