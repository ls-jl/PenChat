#pragma once
#include <cctype>
#include <stdexcept>
#include <string>
#include <map>
#include "latex_syntax.hpp"
#include <utility>
namespace penchat {
// Native, bounded mhchem-style input adapter. MicroTeX supplies the glyphs and
// math layout; this layer supplies chemical upright atoms, scripts and arrows.
// It deliberately keeps ordinary TeX outside \ce unchanged.
namespace chemistry {
inline bool group(const std::string &s,size_t &at,char open,char close,std::string &out){if(at>=s.size()||s[at]!=open)return false;out=latex::syntax::group(s,at,open,close);return true;}
inline std::string bond(const std::string &style){
  static const std::map<std::string,std::string> shapes={{"-","f"},{"1","f"},{"=","ff"},{"2","ff"},{"#","fff"},{"3","fff"},{"~","d"},{"~-","fd"},{"~--","ffd"},{"~=","dd"},{"-~-","fdf"},{"...","o"},{"....","o"}};
  if(style=="->")return "\\mathord{\\rightarrow}";if(style=="<-")return "\\mathord{\\leftarrow}";
  auto found=shapes.find(style);if(found==shapes.end())throw std::runtime_error("Unsupported chemical bond");return "\\penchatbond{"+found->second+"}";
}
inline std::string equation(const std::string &s, unsigned depth = 0) {
  if (depth > 32 || s.size() > 8192) throw std::runtime_error("Chemical formula too large");
  std::string out;
  bool atom = false,closed=false;unsigned elements=0;
  auto append = [&](const std::string &value) {
    if (out.size() + value.size() > 65536) throw std::runtime_error("Chemical expansion too large");
    out += value;
  };
  for (size_t i = 0; i < s.size();) {
    const auto c = static_cast<unsigned char>(s[i]);
    if (std::isspace(c)) { append("\\,"); ++i; atom = false;closed=false;elements=0; continue; }
    // Standalone mhchem gas/precipitate markers are arrows, not a missing
    // exponent or an element. Adjacent H^ and isotope/charge scripts retain
    // their existing interpretation.
    if ((c == '^' || c == 'v') && (i == 0 || std::isspace(static_cast<unsigned char>(s[i-1]))) &&
        (i + 1 == s.size() || std::isspace(static_cast<unsigned char>(s[i+1])))) {
      append(c == '^' ? "\\uparrow " : "\\downarrow "); ++i; atom = false; continue;
    }
    std::string arrow;
    size_t length = 0;
    for (const auto &p : {std::make_pair("<=>>", "\\overset{\\scalebox{1.6}[1]{\\rightharpoonup}}{\\leftharpoondown}"),
                          std::make_pair("<<=>", "\\overset{\\rightharpoonup}{\\scalebox{1.6}[1]{\\leftharpoondown}}"),
                          std::make_pair("<=>", "\\rightleftharpoons"),
                          std::make_pair("<-->", "\\longleftrightarrow"),
                          std::make_pair("<->", "\\leftrightarrow"),
                          std::make_pair("->", "\\longrightarrow"),
                          std::make_pair("<-", "\\longleftarrow")})
      if (s.compare(i, std::char_traits<char>::length(p.first), p.first) == 0) {
        arrow = p.second; length = std::char_traits<char>::length(p.first); break;
      }
    if (length) {
      i += length;
      std::string above, below;
      group(s, i, '[', ']', above); group(s, i, '[', ']', below);
      if (!above.empty() || !below.empty()) {
        arrow = "\\mathop{" + arrow + "}\\limits";
        if (!above.empty()) arrow += "^{" + equation(above, depth + 1) + "}";
        if (!below.empty()) arrow += "_{" + equation(below, depth + 1) + "}";
      }
      append("\\;" + arrow + "\\;"); atom = false;closed=false;elements=0; continue;
    }
    if (std::isalpha(c)) {
      const auto begin = i++;
      while (i < s.size() && std::isalpha(static_cast<unsigned char>(s[i])) &&
             std::islower(static_cast<unsigned char>(s[i]))) ++i;
      const auto word=s.substr(begin,i-begin);const bool variable=word=="x"||word=="y"||word=="z"||word=="n"||word=="m";
      append(variable?word:"\\mathrm{"+word+"}");atom=true;closed=false;++elements;continue;
    }
    if (std::isdigit(c)) {
      const auto begin = i++;
      while (i < s.size() && (std::isdigit(static_cast<unsigned char>(s[i])) || (!atom&&s[i] == '.'))) ++i;
      auto number = s.substr(begin, i - begin);
      if (!atom && i + 1 < s.size() && s[i] == '/' && std::isdigit(static_cast<unsigned char>(s[i+1]))) {
        const auto denominator = ++i;
        while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) ++i;
        append("\\frac{" + number + "}{" + s.substr(denominator, i - denominator) + "}\\,");
        continue;
      }
      if(atom&&i<s.size()&&(s[i]=='+'||s[i]=='-')&&(elements==1||closed)&&(i+1==s.size()||std::isspace(static_cast<unsigned char>(s[i+1])))){append("^{"+number+s[i]+"}");++i;}
      else if (atom) append("_{" + number + "}");
      else { append(number); append("\\,"); }
      continue;
    }
    if (c == '^' || c == '_') {
      const char script = s[i++]; std::string value;
      if (!group(s, i, '{', '}', value)) {
        const auto begin = i;
        while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) ++i;
        if (i < s.size() && (s[i] == '+' || s[i] == '-')) ++i;
        if(begin==i&&i<s.size()){++i;if(i<s.size()&&(s[i]=='+'||s[i]=='-'))++i;}
        value = s.substr(begin, i - begin);
      }
      if (value.empty()) throw std::runtime_error("Missing chemical script");
      if(!atom)append("{}");
      if(value.find_first_not_of("IVX") == std::string::npos)value="\\mathrm{"+value+"}";
      else {std::string fixed;for(char x:value)fixed+=x=='.'?"\\cdot ":x=='*'?"\\ast ":std::string(1,x);value=fixed;}
      append(std::string(1, script) + "{" + value + "}");
      atom = true; continue;
    }
    if (c == '{') {
      std::string text; group(s, i, '{', '}', text);
      // Braced text explicitly escapes chemical-number interpretation.
      append("\\text{" + text + "}"); atom = true; continue;
    }
    if (c == '\\') {
      const auto begin = i++;
      while (i < s.size() && std::isalpha(static_cast<unsigned char>(s[i]))) ++i;
      if (i == begin + 1 && i < s.size()) ++i;
      const auto command = s.substr(begin, i - begin);
      if (command == "\\ce") {
        std::string value;
        if (!group(s, i, '{', '}', value)) throw std::runtime_error("Missing chemical argument");
        append("{" + equation(value, depth + 1) + "}");
      } else if(command=="\\bond"){
        std::string value;if(!group(s,i,'{','}',value))throw std::runtime_error("Missing bond style");append(bond(value));
      } else append(command + " ");
      atom = true; continue;
    }
    if (c == '$') {
      const auto end = s.find('$', i + 1);
      if (end == std::string::npos) throw std::runtime_error("Unclosed chemical math");
      append("{" + s.substr(i + 1, end - i - 1) + "}"); i = end + 1; atom = true; continue;
    }
    if (c == '+' || c == '-') {
      const bool charge = atom && (i + 1 == s.size() || std::isspace(static_cast<unsigned char>(s[i + 1])) || s[i + 1] == ')');
      const bool bonded=c=='-'&&atom&&i+1<s.size()&&!std::isspace(static_cast<unsigned char>(s[i+1]))&&s[i+1]!='+';
      append(charge ? std::string("^{") + char(c) + "}" : bonded?bond("-"):c=='-'?" - ":" + ");
      ++i;if(!charge&&!bonded){atom=false;elements=0;}continue;
    }
    if (s.compare(i, 2, "·") == 0) { append("\\cdot "); i += 2; atom = false; continue; }
    if(c=='*'||(c=='.'&&atom)){append("\\cdot ");++i;atom=false;closed=false;elements=0;continue;}
    if(c=='#'||(c=='='&&atom&&i+1<s.size()&&!std::isspace(static_cast<unsigned char>(s[i+1])))){append(bond(std::string(1,c)));++i;atom=false;continue;}
    if (c == '~') { append("\\ "); ++i; atom = false; continue; }
    if (c == '%') { const auto end = s.find('\n', i); i = end == std::string::npos ? s.size() : end; continue; }
    append(s.substr(i++, 1));
    if (c == ')' || c == ']'){atom=true;closed=true;}
    else if (c == '(' || c == '[' || c == '=' || c == '/') atom = false;
  }
  return out;
}
} // namespace chemistry
inline std::string chemistryTex(const std::string &source,size_t inputLimit=8192) {
  if (source.size() > inputLimit) throw std::runtime_error("Formula too large");
  std::string out;
  for (size_t i = 0; i < source.size();) {
    if (source.compare(i, 3, "\\ce") == 0 &&
        (i + 3 == source.size() || !std::isalpha(static_cast<unsigned char>(source[i + 3])))) {
      size_t at = i + 3; while (at < source.size() && std::isspace(static_cast<unsigned char>(source[at]))) ++at;
      std::string value;
      if (!chemistry::group(source, at, '{', '}', value)) throw std::runtime_error("Missing chemical argument");
      out += "{" + chemistry::equation(value) + "}"; i = at;
    } else if (source[i] == '\\' && i + 1 < source.size()) {
      out += source.substr(i, 2); i += 2;
    } else out += source[i++];
    if (out.size() > 65536) throw std::runtime_error("Chemical expansion too large");
  }
  return out;
}
} // namespace penchat
