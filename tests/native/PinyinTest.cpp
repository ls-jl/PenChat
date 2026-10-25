#include "TestFiles.hpp"
#include "pinyin.hpp"
#include "editor.hpp"
#include "database.hpp"
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
  Json search(const std::string &text,const std::string &layout="zh26",const std::string &fixed="") {
    query={{"session","native-test"},{"revision",++revision},{"sequence",++sequence},{"operation","query"},{"text",text},{"fixed",fixed},{"layout",layout}};
    assert(service->request(query));return await(sequence);
  }
  Json choose(const Json &result,const std::string &word) {
    int index=-1;for(size_t i=0;i<result["candidates"].size();++i)if(result["candidates"][i]==word){index=int(i);break;}
    assert(index>=0&&"required real dictionary candidate missing");
    auto request=query;request["operation"]="choose";request["sequence"]=++sequence;request["candidate"]=index;
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
  { Database db(join(f.root,"preferences.db"));db.queueSetting("pinyin_chinese_layout","zh9");db.flush();assert(db.settings()["pinyin_chinese_layout"]=="zh9"); }

  auto r=f.search("nihao");assert(r["ok"]==true);auto selected=f.choose(r,"你好");assert(selected["remaining"]==""&&selected["consumed"]==5);
  r=f.search("nihaoq");selected=f.choose(r,"你好");assert(selected["remaining"]=="q"&&selected["pinyin"]=="ni'hao");
  r=f.search("nh");selected=f.choose(r,"你好");assert(selected["remaining"]=="");
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
