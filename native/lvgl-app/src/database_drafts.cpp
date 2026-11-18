#include "database.hpp"
#include <stdexcept>
#include <dirent.h>
#include "util.h"
#include <sys/stat.h>
#include <unistd.h>
namespace penchat {
Json Database::draft(const std::string &key) {
  return perform([this,key] {
    execute("INSERT OR IGNORE INTO chat_drafts(draft_key) VALUES(?)",{key});
    const auto row=query("SELECT epoch,revision,text FROM chat_drafts WHERE draft_key=?",{key}).at(0);
    Json images=Json::array();
    for(const auto &entry:query("SELECT id,image_json FROM draft_attachments WHERE draft_key=? ORDER BY position,id",{key})) {
      auto image=Json::parse(entry.at("image_json").get<std::string>());
      image["id"]=entry.at("id");images.push_back(std::move(image));
    }
    return Json{{"key",key},{"epoch",row.at("epoch")},{"revision",row.at("revision")},
                {"text",row.at("text")},{"images",images}};
  });
}
void Database::queueDraft(const Json &snapshot,const std::string &text) {
  const auto key=snapshot.at("key").get<std::string>();
  {
    std::lock_guard<std::mutex> lock(jobsMutex_);
    const auto found=pendingDrafts_.find(key);
    if(found!=pendingDrafts_.end()&&
        (found->second.at("epoch")>snapshot.at("epoch")||
         (found->second.at("epoch")==snapshot.at("epoch")&&found->second.at("revision")>=snapshot.at("revision"))))return;
    pendingDrafts_[key]={{"epoch",snapshot.at("epoch")},{"revision",snapshot.at("revision")},{"text",text}};
    if(!scheduledDrafts_.insert(key).second)return;
    jobs_.push_back([this,key] {
      Json pending;
      {
        std::lock_guard<std::mutex> lock(jobsMutex_);
        pending=pendingDrafts_.at(key);pendingDrafts_.erase(key);scheduledDrafts_.erase(key);
      }
      try {
        execute("UPDATE chat_drafts SET text=?,revision=? WHERE draft_key=? AND epoch=? AND revision<?",
                {pending.at("text"),pending.at("revision"),key,pending.at("epoch"),pending.at("revision")});
      } catch(...) {saveEvents_.push("draft_error",{{"key",key}});}
    });
  }
  jobsReady_.notify_one();
}
Json Database::addDraftImage(const Json &snapshot,const Json &source) {
  return perform([this,snapshot,source] {
    const auto key=snapshot.at("key").get<std::string>();
    if(draft(key).at("epoch")!=snapshot.at("epoch"))throw std::runtime_error("草稿已变化，请重新加入图片");
    const auto directory=join(assetDirectory_+"-drafts",uid("image"));
    ensureDirectory(directory);
    Json image=source;
    std::vector<std::string> files;
    try {
      for(const auto &field:{"path","previewPath"}) {
        if(!source.contains(field))continue;
        const auto bytes=fileText(source.at(field).get<std::string>(),16*1024*1024);
        if(bytes.empty())throw std::runtime_error("无法读取草稿图片");
        auto extension=camera::extensionOf(source.at(field).get<std::string>());
        if(extension.empty())extension="png";
        const auto path=join(directory,std::string(field)+"."+extension);
        files.push_back(path);atomicText(path,bytes);image[field]=path;
      }
      const auto position=query("SELECT coalesce(max(position),0)+1 n FROM draft_attachments WHERE draft_key=?",{key}).at(0).at("n");
      const auto id=uid("draft-image");
      image["ephemeral"]=true;
      execute("INSERT INTO draft_attachments(id,draft_key,position,image_json) VALUES(?,?,?,?)",{id,key,position,image.dump()});
      image["id"]=id;
      return image;
    } catch(...) {for(const auto &path:files)unlink(path.c_str());rmdir(directory.c_str());throw;}
  });
}
bool Database::removeDraftImage(const Json &snapshot,const std::string &id) {
  return perform([this,snapshot,id] {
    const auto key=snapshot.at("key").get<std::string>();
    if(draft(key).at("epoch")!=snapshot.at("epoch"))return Json(false);
    const auto rows=query("SELECT image_json FROM draft_attachments WHERE id=? AND draft_key=?",{id,key});
    if(rows.empty())return Json(false);
    execute("DELETE FROM draft_attachments WHERE id=? AND draft_key=?",{id,key});
    const auto image=Json::parse(rows.at(0).at("image_json").get<std::string>());
    for(const auto &field:{"path","previewPath"})if(image.contains(field))unlink(image.at(field).get<std::string>().c_str());
    return Json(true);
  }).get<bool>();
}
void Database::clearDraft(const std::string &key) {
  perform([this,key] {
    execute("DELETE FROM draft_attachments WHERE draft_key=?",{key});
    execute("UPDATE chat_drafts SET text='',revision=0,epoch=epoch+1 WHERE draft_key=?",{key});
    {
      std::lock_guard<std::mutex> lock(jobsMutex_);
      jobs_.push_back([this]{try{collectDraftFiles();}catch(...){}});
    }
    jobsReady_.notify_one();
    return Json();
  });
}
void Database::moveDraft(const std::string &from,const std::string &to) {
  perform([this,from,to] {
    const auto original=draft(from);draft(to);
    execute("BEGIN IMMEDIATE");
    try {
      execute("UPDATE chat_drafts SET text=?,revision=revision+1 WHERE draft_key=?",{original.at("text"),to});
      execute("UPDATE draft_attachments SET draft_key=? WHERE draft_key=?",{to,from});
      clearDraft(from);execute("COMMIT");
    }catch(...){execute("ROLLBACK");throw;}
    return Json();
  });
}
Json Database::commitDraftSend(const Json &snapshot,const std::string &session,
    const std::string &text,const Json &preferences,const Json &model,int64_t sentAt,
    const Json &prepared,const Json &checkpoint) {
  return perform([=] {
    const bool edited=snapshot.value("edited",false);
    const auto key=edited?session:snapshot.at("key").get<std::string>();
    const auto current=draft(key);
    if(!edited&&(current.at("epoch")!=snapshot.at("epoch")||current.at("revision")!=snapshot.at("revision")||
       current.at("images")!=snapshot.at("images")))throw std::runtime_error("草稿已变化，请重新发送");
    const auto images=edited?Json::array():current.at("images");
    execute("BEGIN IMMEDIATE");
    try {
      std::string assistant;
      if(!prepared.empty())assistant=commitReplyContinuation(prepared,text,preferences,checkpoint,images,false).at("message");
      else {
        for(const auto &image:images)append(session,"user","",image,preferences);
        if(!text.empty())append(session,"user",text,Json::object(),preferences);
        assistant=append(session,"assistant","",Json::object(),preferences);
      }
      recordRequest(assistant,model,sentAt,nowMs());
      if(!edited)clearDraft(key);
      if(key!="new")clearDraft("new");
      execute("COMMIT");
      return Json{{"message",assistant}};
    }catch(...){execute("ROLLBACK");throw;}
  });
}
} // namespace penchat

namespace penchat {
void Database::collectDraftFiles() {
  std::set<std::string> retained;
  for(const auto &row:query("SELECT image_json FROM draft_attachments")) {
    const auto image=Json::parse(row.at("image_json").get<std::string>());
    for(const auto &field:{"path","previewPath"})if(image.contains(field))retained.insert(image.at(field));
  }
  const auto root=assetDirectory_+"-drafts";
  struct stat st{};
  if(lstat(root.c_str(),&st)||!S_ISDIR(st.st_mode)||S_ISLNK(st.st_mode))return;
  auto directory=opendir(root.c_str());if(!directory)return;
  while(auto entry=readdir(directory)) {
    if(entry->d_name[0]=='.')continue;
    const auto folder=join(root,entry->d_name);
    if(lstat(folder.c_str(),&st)||!S_ISDIR(st.st_mode)||S_ISLNK(st.st_mode))continue;
    auto files=opendir(folder.c_str());if(!files)continue;
    while(auto file=readdir(files)) {
      if(file->d_name[0]=='.')continue;
      const auto path=join(folder,file->d_name);
      if(!retained.count(path)&&!lstat(path.c_str(),&st)&&S_ISREG(st.st_mode))unlink(path.c_str());
    }
    closedir(files);rmdir(folder.c_str());
  }
  closedir(directory);
}
}
