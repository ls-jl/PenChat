#include "chemistry.hpp"
#include "markdown.hpp"
#include "stream_progress.hpp"
#include "titles.hpp"
#include <cassert>
#include <iostream>
using namespace penchat;
int main(int argc, char **argv) {
  assert(argc == 3);
  RichText rich(argv[1], argv[2]);
  const std::pair<const char *, const char *> cases[] = {
    {R"(\ce{H2O})", R"({\mathrm{H}_{2}\mathrm{O}})"},
    {R"(\ce{Na+})", R"({\mathrm{Na}^{+}})"},
    {R"(\ce{SO4^2-})", R"({\mathrm{S}\mathrm{O}_{4}^{2-}})"},
    {R"(\ce{(NH4)2SO4})", R"({(\mathrm{N}\mathrm{H}_{4})_{2}\mathrm{S}\mathrm{O}_{4}})"},
  };
  for (const auto &c : cases) assert(chemistryTex(c.first) == c.second);
  assert(chemistryTex(R"(\frac{1}{2}+x_2)") == R"(\frac{1}{2}+x_2)");
  assert(chemistryTex(R"(\cellcolor{red})") == R"(\cellcolor{red})");
  const char *examples[] = {
    R"(\ce{H2O})", R"(\ce{2 H2 + O2 -> 2 H2O})", R"(\ce{Na+ + Cl- -> NaCl})",
    R"(\ce{SO4^2-})", R"(\ce{Ca(OH)2})", R"(\ce{[Fe(CN)6]^3-})",
    R"(\ce{NH4+ <=> NH3 + H+})", R"(\ce{A ->[\Delta][H2O] B})",
    R"(\ce{CuSO4 · 5 H2O})", R"(\ce{^{14}_{6}C})", R"(\ce{H2(g) + O2(g) -> H2O(l)})",
    R"(\ce{CH3-CH2-OH})", R"(\ce{A <-> B})", R"(\ce{A <- B})",
    R"(x + \ce{H2O} + \frac{1}{2})", R"(\ce{2 H2 + O2 ->[{heat}] 2 H2O})",
    R"(\ce{CaCO3 ->[\Delta] CaO + CO2 ^})", R"(\ce{Ag+ + Cl- -> AgCl v})",
    R"(\ce{H2 + 1/2 O2 -> H2O})"
  };
  for (const auto *source : examples) {
    for (uint32_t color : {0xd5d7deu, 0x30343bu}) {
      const auto bitmap = rich.formula(source, 21, color);
      assert(bitmap.width > 0 && bitmap.height > 0 && bitmap.baseline > 0);
    }
    const auto doc = parseMarkdown(std::string("$") + source + "$");
    assert(doc.blocks.size() == 1 && doc.blocks[0].runs.size() == 1);
    assert(doc.blocks[0].runs[0].style & Math);
    assert(doc.blocks[0].runs[0].text == source);
    assert(blockPlainText(doc.blocks[0]) == std::string("$") + source + "$");
  }
  assert(chemistryTex(R"(\ce{CO2 ^})").find("\\uparrow") != std::string::npos);
  assert(chemistryTex(R"(\ce{AgCl v})").find("\\downarrow") != std::string::npos);
  assert(chemistryTex(R"(\ce{1/2 O2})").find("\\frac{1}{2}") != std::string::npos);
  for (const auto *source : {R"(\ce{H2O)", R"(\ce H2O)", R"(\ce{H^})"}) {
    bool rejected = false; try { chemistryTex(source); } catch (...) { rejected = true; }
    assert(rejected);
  }
  for (const auto &source : {std::string("\\ce{") + std::string(8200, 'H') + "}",
                             std::string("\\ce{") + std::string(33, '{') + "H" + std::string(34, '}')}) {
    bool rejected = false; try { chemistryTex(source); } catch (...) { rejected = true; }
    assert(rejected);
  }
  const auto bare = parseMarkdown(R"(\ce{H2O})");
  assert(bare.blocks[0].runs[0].style & Math);
  for (const auto &source : {std::string("`\\ce{H2O}`"), std::string("```tex\n\\ce{H2O}\n```\n")}) {
    const auto literal = parseMarkdown(source);
    assert(!(literal.blocks[0].runs[0].style & Math));
  }
  assert(genericImageTitle("图片") && genericImageTitle("[图片]") && genericImageTitle("IMAGE"));
  assert(!genericImageTitle("水的电解反应"));
  assert(titleExcerpt("中文 A", 0).empty());
  assert(titleExcerpt("中文 A", 2) == "中文");
  assert(titleExcerpt("A😀中文", 2) == "A😀");
  assert(imageAnswerTitle("# 水的电解反应\n进一步解释", 20) == "水的电解反应");
  assert(characters(imageAnswerTitle("图片", 2)).size() <= 2);
  StreamProgress progress;
  assert(!progress.stalled(10000000) && !progress.waiting(10000000));
  progress.observe(1000);
  assert(!progress.waiting(15999) && progress.waiting(16000));
  assert(!progress.stalled(120999) && progress.stalled(121000));
  progress.observe(120999); assert(!progress.waiting(121000));
  assert(meaningfulStreamChunk({{"choices", Json::array({{{"delta", {{"reasoning_content", "thinking"}}}}})}}));
  assert(!meaningfulStreamChunk({{"choices", Json::array({{{"delta", Json::object()}}})}}));
  assert(!meaningfulStreamChunk({{"usage", {{"total_tokens", 10}}}}));
  std::cout << "Chemical native rendering, theme colors, source offsets, image titles and stream stall clock passed\n";
}
