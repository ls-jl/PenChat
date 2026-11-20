#pragma once
#include "latex_syntax.hpp"
#include "latex_extensions.hpp"
#include "core/macro.h"
#include "core/formula.h"
#include "atom/atom_space.h"
#include "atom/atom_impl.h"
#include "atom/atom_matrix.h"
#include "core/formula.h"
#include <cmath>
namespace penchat { namespace latex {
// Fixed private commands only. User definitions never enter the engine registry.
class BracketBox final : public tex::Box {
  tex::sptr<tex::Box> base_;float thickness_,tip_,gap_;bool over_;
public:
  BracketBox(tex::sptr<tex::Box> base,float thickness,float tip,float gap,bool over)
      :base_(std::move(base)),thickness_(thickness),tip_(tip),gap_(gap),over_(over){
    _width=base_->_width;_height=base_->_height+(over?tip+gap+thickness:0);_depth=base_->_depth+(over?0:tip+gap+thickness);
  }
  void draw(tex::Graphics2D &g,float x,float y) override {
    base_->draw(g,x,y);const float line=over_?y-_height:y+_depth-thickness_;
    g.fillRect(x,line,_width,thickness_);
    const float top=over_?line:line-tip_;g.fillRect(x,top,thickness_,tip_+thickness_);g.fillRect(x+_width-thickness_,top,thickness_,tip_+thickness_);
  }
  std::vector<tex::sptr<tex::Box>> descendants()const override{return {base_};}
};
class BracketAtom final : public tex::Atom {
  tex::sptr<tex::Atom> base_;std::string thickness_,height_;bool over_;
public:
  static float length(const std::string &text,tex::Environment &env,float fallback,float maximum=64){
    if(text.empty())return fallback;size_t used=0;const float number=std::stof(text,&used);auto unit=syntax::trim(text.substr(used));
    static const std::vector<std::string> units={"pt","em","ex","mu","px","mm","cm","in"};
    if(!std::isfinite(number)||number<0||number>maximum||std::find(units.begin(),units.end(),unit)==units.end())throw std::runtime_error("Invalid bracket dimension");
    const std::map<std::string,tex::UnitType> types={{"pt",tex::UnitType::point},{"em",tex::UnitType::em},{"ex",tex::UnitType::ex},{"mu",tex::UnitType::mu},{"px",tex::UnitType::pixel},{"mm",tex::UnitType::mm},{"cm",tex::UnitType::cm},{"in",tex::UnitType::in}};
    return number*tex::SpaceAtom::getFactor(types.at(unit),env);
  }
public:
  BracketAtom(tex::sptr<tex::Atom> base,std::string thickness,std::string height,bool over):base_(std::move(base)),thickness_(std::move(thickness)),height_(std::move(height)),over_(over){_limitsType=tex::LimitsType::limits;_type=tex::AtomType::bigOperator;if(!base_)throw std::runtime_error("Empty bracket content");}
  tex::sptr<tex::Box> createBox(tex::Environment &env)override{
    auto b=base_->createBox(env);const float rule=env.getTeXFont()->getDefaultRuleThickness(env.getStyle());return tex::sptrOf<BracketBox>(b,length(thickness_,env,rule),length(height_,env,rule*3),rule*2,over_);
  }
  tex::sptr<tex::Atom> clone()const override{return tex::sptrOf<BracketAtom>(*this);}
};
class BracketMacro final : public tex::MacroInfo {
public:
  BracketMacro():MacroInfo(4){}
  tex::sptr<tex::Atom> invoke(tex::TeXParser &parser,std::vector<std::wstring> &args)override{
    const auto mode=tex::wide2utf8(args[1]);if(mode!="over"&&mode!="under")throw std::runtime_error("Invalid bracket mode");
    return tex::sptrOf<BracketAtom>(tex::Formula(parser,args[4])._root,tex::wide2utf8(args[2]),tex::wide2utf8(args[3]),mode=="over");
  }
};
class MultlinedAtom final : public tex::Atom {
  tex::sptr<tex::ArrayFormula> array_;std::string position_,width_;
public:
  MultlinedAtom(tex::sptr<tex::ArrayFormula> array,std::string position,std::string width):array_(std::move(array)),position_(std::move(position)),width_(std::move(width)){if(position_!="t"&&position_!="c"&&position_!="b")throw std::runtime_error("Invalid multlined position");}
  tex::sptr<tex::Box> createBox(tex::Environment &env)override{
    std::vector<tex::sptr<tex::Box>> rows;float width=0;
    for(size_t i=0;i<array_->rows();++i){if(array_->_array[i].size()!=1)throw std::runtime_error("multlined requires one column");auto b=array_->_array[i][0]?array_->_array[i][0]->createBox(env):tex::sptrOf<tex::StrutBox>(0,0,0,0);width=std::max(width,b->_width);rows.push_back(b);}
    if(rows.empty())throw std::runtime_error("Empty multlined");width=std::max(width,BracketAtom::length(width_,env,width,4096));
    auto box=tex::sptrOf<tex::VBox>();const float gap=tex::SpaceAtom::getFactor(tex::UnitType::ex,env)*.5f;
    for(size_t i=0;i<rows.size();++i){auto line=tex::sptrOf<tex::HBox>(rows[i],width,i==0?tex::Alignment::left:i+1==rows.size()?tex::Alignment::right:tex::Alignment::center);if(i)box->add(line,gap);else box->add(line);}
    const auto total=box->_height+box->_depth;
    box->_height=position_=="t"?rows.front()->_height:position_=="b"?total-rows.back()->_depth:total*.5f+env.getTeXFont()->getAxisHeight(env.getStyle());box->_depth=total-box->_height;return box;
  }
  tex::sptr<tex::Atom> clone()const override{return tex::sptrOf<MultlinedAtom>(*this);}
};
class MultlinedMacro final : public tex::MacroInfo {
public:MultlinedMacro():MacroInfo(3){}
  tex::sptr<tex::Atom> invoke(tex::TeXParser &,std::vector<std::wstring> &args)override{
    const auto body=tex::utf82wide(boundedMultlined(tex::wide2utf8(args[3])));auto array=tex::sptrOf<tex::ArrayFormula>();tex::TeXParser nested(false,body,array.get(),false);nested.parse();array->checkDimensions();return tex::sptrOf<MultlinedAtom>(array,tex::wide2utf8(args[1]),tex::wide2utf8(args[2]));
  }
};
class BondAtom final : public tex::Atom {
  std::string lines_;
  class BondBox final : public tex::Box {
    std::string lines_;float rule_,gap_;
  public:
    BondBox(std::string lines,float em,float ex,float rule):lines_(std::move(lines)),rule_(rule),gap_(ex*.3f){_width=em;_height=ex*.5f+gap_;_depth=gap_;}
    void draw(tex::Graphics2D &g,float x,float y)override{
      for(size_t i=0;i<lines_.size();++i){const float h=y-_height+i*gap_;if(lines_[i]=='f')g.fillRect(x,h,_width,rule_);
        else{const int count=lines_[i]=='d'?3:5;const float step=_width/count;for(int j=0;j<count;++j)g.fillRect(x+j*step,h,step*(lines_[i]=='d'?.65f:.25f),rule_);}}
    }
  };
public:
  explicit BondAtom(std::string lines):lines_(std::move(lines)){if(lines_.empty()||lines_.size()>3||lines_.find_first_not_of("fdo")!=std::string::npos)throw std::runtime_error("Invalid bond style");}
  tex::sptr<tex::Box> createBox(tex::Environment &env)override{return tex::sptrOf<BondBox>(lines_,tex::SpaceAtom::getFactor(tex::UnitType::em,env),tex::SpaceAtom::getFactor(tex::UnitType::ex,env),env.getTeXFont()->getDefaultRuleThickness(env.getStyle()));}
  tex::sptr<tex::Atom> clone()const override{return tex::sptrOf<BondAtom>(*this);}
};
class BondMacro final : public tex::MacroInfo {
public:BondMacro():MacroInfo(1){}
  tex::sptr<tex::Atom> invoke(tex::TeXParser &,std::vector<std::wstring> &args)override{return tex::sptrOf<BondAtom>(tex::wide2utf8(args[1]));}
};
inline void registerNative(){
  if(!tex::MacroInfo::_commands.count(L"penchatmultlined"))tex::MacroInfo::add(L"penchatmultlined",new MultlinedMacro);
  if(!tex::MacroInfo::_commands.count(L"penchatbracket"))tex::MacroInfo::add(L"penchatbracket",new BracketMacro);
  if(!tex::MacroInfo::_commands.count(L"penchatbond"))tex::MacroInfo::add(L"penchatbond",new BondMacro);
}
} }
