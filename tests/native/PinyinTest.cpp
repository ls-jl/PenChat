#include "TestFiles.hpp"
#include "pinyin.hpp"
#include "editor.hpp"
#include "database.hpp"
#include "pinyin_ime.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>
using namespace penchat;
struct Fixture {
  Events events;
  std::string root,resources;
  std::unique_ptr<PinyinService> service;
  uint64_t sequence=0, revision=0;
  Json query;
  Fixture(const std::string &r):resources(r) {
    char name[]="/tmp/penchat-pinyin-XXXXXX";assert(mkdtemp(name));root=name;
    service.reset(new PinyinService(events,resources,root));
  }
  Json await(uint64_t id) {
    const auto end=ticks()+30000;
    while(ticks()<end){for(const auto &e:events.drain())if(e.type=="pinyin.result"&&e.data.value("sequence",uint64_t(0))==id)return e.data;usleep(1000);}
    assert(false&&"local pinyin result timed out");return {};
  }
  Json search(const std::string &text,const std::string &layout="zh26",const std::string &fixed="",bool correction=true) {
    query={{"session","native-test"},{"revision",++revision},{"sequence",++sequence},{"operation","query"},{"text",text},{"fixed",fixed},{"layout",layout},{"correction",correction}};
    assert(service->request(query));return await(sequence);
  }
  Json choose(const Json &result,const std::string &word) {
    int index=-1;for(size_t i=0;i<result["candidates"].size();++i)if(result["candidates"][i]==word){index=int(i);break;}
    assert(index>=0&&"required real dictionary candidate missing");
    auto request=query;request["operation"]="choose";request["sequence"]=++sequence;request["candidate"]=index;request["candidate_version"]=result["candidate_version"];
    assert(service->request(request));return await(sequence);
  }
  void restart(){service.reset();service.reset(new PinyinService(events,resources,root));}
  ~Fixture(){service.reset();test::removeTree(root);}
};
int main(int argc,char **argv) {
  assert(argc==2);
  {
    EditorState e;e.globe(10000);assert(e.layout==1);e.globe(11000);assert(e.layout==2);e.globe(12000);assert(e.layout==0);
    e.preferredChinese=2;e.changeLayout(2);e.contentChanged();e.globe(13000);assert(e.layout==1);e.contentChanged();e.globe(13001);assert(e.layout==2);
    e.globe(14000);assert(e.layout==0);e.globe(16000);assert(e.layout==1);e.globe(18000);assert(e.layout==2);
    e.preedit="64426";e.fixed="ni";e.changeLayout(1);e.insert("English");e.changeLayout(0);e.preedit="nihao";e.changeLayout(2);
    assert(e.preedit=="64426"&&e.fixed=="ni");e.changeLayout(0);assert(e.preedit=="nihao"&&e.text=="English");
  }
  Fixture f(argv[1]);
  for(const auto &example:{std::make_pair("lve","略"),std::make_pair("lue","略"),std::make_pair("nve","虐"),std::make_pair("nue","虐")}) {
    auto result=f.search(example.first);f.choose(result,example.second);
  }
  { Database db(join(f.root,"preferences.db"));db.queueSetting("pinyin_chinese_layout","zh9");db.flush();assert(db.settings()["pinyin_chinese_layout"]=="zh9"); }

  {
    auto result=f.search("niao","zh26","",false);const auto first=result["candidates"][0],second=result["candidates"][1];
    result=f.search("niao");assert(result["candidates"][0]==first&&result["candidates"][1]==second);
    auto value=f.choose(result,"你好");assert(value["pinyin"]=="ni'hao"&&value["consumed"]==4&&value["remaining"]=="");
    // A valid spelling still retains its ordinary first choice for Space/Done.
    result=f.search("nihao","zh26","",false);auto top=result["candidates"][0];result=f.search("nihao");assert(result["candidates"][0]==top);
  }
  // Correction is candidate-only. Its consumed range is always ORIGINAL raw.
  for(const auto &example:{std::make_pair("nihoa",5),std::make_pair("nihxao",6),std::make_pair("nihqo",5),std::make_pair("nihoaq",5)}) {
    auto result=f.search(example.first);size_t index=0;while(index<result["candidates"].size()&&result["candidates"][index]!="你好")++index;
    assert(index<result["candidates"].size()&&result["candidate_details"][index]["corrected"]==true);
    auto value=f.choose(result,"你好");assert(value["consumed"]==example.second&&value["pinyin"]=="ni'hao");
    assert(value["remaining"]==std::string(example.first).substr(example.second));
  }
  for(const auto &example:{std::make_pair("zhngwen","中文"),std::make_pair("zhognwen","中文"),std::make_pair("zhongwne","中文"),std::make_pair("shruufa","输入法"),std::make_pair("zhogn","中"),std::make_pair("shagn","上")}) {
    auto result=f.search(example.first);auto value=f.choose(result,example.second);assert(value["remaining"]=="");
  }
  {
    auto result=f.search("nihoa");auto wrong=f.query;wrong["correction"]=false;wrong["operation"]="choose";wrong["candidate"]=0;wrong["sequence"]=++f.sequence;
    assert(f.service->request(wrong));assert(f.await(f.sequence)["ok"]==false);
    result=f.search("nihoa");wrong=f.query;wrong["operation"]="choose";wrong["candidate"]=0;wrong["candidate_version"]=result["candidate_version"].get<uint64_t>()-1;wrong["sequence"]=++f.sequence;
    assert(f.service->request(wrong));assert(f.await(f.sequence)["ok"]==false);
    result=f.search("nihoa","zh26","",false);for(const auto &v:result["candidate_details"])assert(v["corrected"]==false);
    auto e=pinyin_ime_init(join(argv[1],"pinyin.txt").c_str(),join(argv[1],"dictionary.data").c_str());assert(e);
    pinyin_ime_set_correction(e,1);
    for(const auto &raw:{std::string("ni"),std::string("nh"),std::string("nih'ao"),std::string(33,'x'),std::string(128,'a'),std::string("64426")}) {
      assert(pinyin_ime_input(e,raw.c_str())==PINYIN_IME_OK);assert(pinyin_ime_correction_states(e)<=16384);
      for(int i=0;i<pinyin_ime_get_candidate_count(e);++i)assert(!pinyin_ime_candidate_corrected(e,i));
    }
    uint32_t seed=28;
    for(size_t n=0;n<96;++n) {
      std::string raw;for(size_t i=0;i<4+n%29;++i){seed=seed*1664525u+1013904223u;raw+=char('a'+seed%26);}
      assert(pinyin_ime_input(e,raw.c_str())==PINYIN_IME_OK&&pinyin_ime_correction_states(e)<=16384);
      size_t corrected=0;
      for(int i=0;i<pinyin_ime_get_candidate_count(e);++i) {
        const auto consumed=pinyin_ime_candidate_consumed(e,i);assert(consumed>0&&size_t(consumed)<=raw.size());
        corrected+=pinyin_ime_candidate_corrected(e,i)!=0;
      }
      assert(corrected<=8);
    }
    assert(pinyin_ime_input(e,"nihoa")==PINYIN_IME_OK&&pinyin_ime_correction_states(e)>0);
    assert(pinyin_ime_correction_states(e)<=16384);pinyin_ime_destroy(e);
  }
  auto r=f.search("nihao");assert(r["ok"]==true);auto selected=f.choose(r,"你好");assert(selected["remaining"]==""&&selected["consumed"]==5);
  r=f.search("nihaoq");selected=f.choose(r,"你好");assert(selected["remaining"]=="q"&&selected["pinyin"]=="ni'hao");
  r=f.search("nh");selected=f.choose(r,"你好");assert(selected["remaining"]=="");
  for(const auto &example:{std::make_pair("shurufa","输入法"),std::make_pair("nihaoshijie","你好世界"),std::make_pair("womenxuexizhongwen","我们学习中文")}) {
    r=f.search(example.first);assert(r["candidates"][0]==example.second);
    selected=f.choose(r,example.second);assert(selected["remaining"]=="");
  }
  r=f.search("6442664","zh9");selected=f.choose(r,"你好");assert(selected["remaining"]=="64"&&selected["consumed"]==5);
  r=f.search("64426","zh9");bool exact=false;for(const auto &v:r["exact"])if(v=="ni")exact=true;assert(exact);
  r=f.search("64426","zh9","ni");assert(std::find(r["candidates"].begin(),r["candidates"].end(),Json("你好"))!=r["candidates"].end());selected=f.choose(r,"你");assert(selected["remaining"]=="426"&&selected["fixed"]=="");
  auto wrong=f.query;wrong["sequence"]=++f.sequence;wrong["operation"]="choose";wrong["revision"]=uint64_t(0);wrong["candidate"]=0;
  f.service->request(wrong);assert(f.await(f.sequence)["ok"]==false);
  r=f.search(std::string(129,'a'));assert(r["ok"]==false);
  r=f.search(std::string(120,'a')+"x");assert(r["ok"]==true);
  // Selection alone must not create or modify the private learning file.
  assert(access(join(f.root,"ime/user.json").c_str(),F_OK)!=0);
  r=f.search("shi");assert(r["candidates"].size()>2);const auto target=r["candidates"][1].get<std::string>();
  f.service->commit({{{"pinyin","shi"},{"word",target}},{{"pinyin","ce'ying"},{"word","测嘤"}}},target+"测嘤");
  r=f.search("shi");assert(r["candidates"][0]==target);
  r=f.search("ceying");f.choose(r,"测嘤");
  r=f.search("239464","zh9");f.choose(r,"测嘤");
  f.restart();r=f.search("ceying");f.choose(r,"测嘤");
  auto data=Json::parse(fileText(join(f.root,"ime/user.json")));assert(data["records"].size()==2);
  f.service->clear();r=f.search("ceying");for(const auto &v:r["candidates"])assert(v!="测嘤");
  data=Json::parse(fileText(join(f.root,"ime/user.json")));assert(data["records"].empty());
  f.service.reset();atomicText(join(f.root,"ime/user.json"),"broken-data");f.service.reset(new PinyinService(f.events,f.resources,f.root));
  r=f.search("nihao");f.choose(r,"你好");assert(access(join(f.root,"ime/user.json").c_str(),F_OK)!=0);
  // A failed atomic write retains the old public result and leaves the worker usable.
  atomicText(join(f.root,"ime/user.json"),"{\"version\":1,\"records\":[]}");
  const auto previous=fileText(join(f.root,"ime/user.json"));
  assert(!mkdir(join(f.root,"ime/user.json.part").c_str(),0700));
  f.service->commit({{{"pinyin","ce'ying"},{"word","测嘤"}}},"测嘤");r=f.search("ceying");f.choose(r,"测嘤");
  assert(fileText(join(f.root,"ime/user.json"))==previous);
  {
    Fixture bounded(argv[1]);Json records=Json::array();std::string text;
    for(int i=0;i<10010;++i){const auto word=utf8(0x4e00+i);records.push_back({{"pinyin","a"},{"word",word}});text+=word;}
    assert(bounded.service->request({{"operation","commit"},{"records",records},{"text",text}}));
    bounded.search("nihao");
    auto saved=Json::parse(fileText(join(bounded.root,"ime/user.json")));
    assert(saved["records"].size()==10000);
    assert(fileText(join(bounded.root,"ime/user.json")).size()<=2*1024*1024);
    // Large valid learned phrases exercise the byte ceiling independently.
    records=Json::array();text.clear();
    std::string py;for(int i=0;i<31;++i)py+="an'";py+="an";
    for(int i=0;i<10000;++i){std::string word=utf8(0x4e00+i);for(int j=0;j<31;++j)word+="安";records.push_back({{"pinyin",py},{"word",word}});text+=word;}
    assert(bounded.service->request({{"operation","commit"},{"records",records},{"text",text}}));
    bounded.search("nihao");saved=Json::parse(fileText(join(bounded.root,"ime/user.json")));
    assert(saved["records"].size()<10000 && fileText(join(bounded.root,"ime/user.json")).size()<=2*1024*1024);
  }
  {
    Fixture missing(join(argv[1],"not-installed"));auto result=missing.search("nihao");assert(result["ok"]==false&&result["text"]=="nihao");
    const auto empty=join(missing.root,"empty");ensureDirectory(empty);atomicText(join(empty,"pinyin.txt"),"");atomicText(join(empty,"dictionary.data"),"");
    Fixture invalid(empty);assert(invalid.search("nihao")["ok"]==false);
  }
  std::cout<<"Independent pinyin: real 26/9-key dictionary, raw consumption, exact filters, stale guards, deferred learning, persistence/corruption and write failure passed\n";
}
