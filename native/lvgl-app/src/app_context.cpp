#include "app.hpp"
#include "context.hpp"
#include "image_data.h"
#include <stdexcept>
namespace penchat {
void App::cancelContext() {
  contextCancel_ = true;
  if (contextWorker_.joinable())
    contextWorker_.join();
  contextBusy_ = false;
  contextRegenerating_ = false;
  ++contextGeneration_;
}
void App::startContext(bool manual, const std::string &retry, bool regenerate, bool archive, Json frozenModel) {
  if(!messageEdit_.empty()) {
    if(manual)modal("请先完成消息编辑","发送或取消编辑后，再压缩上下文。原对话尚未修改。");
    return;
  }
  if (contextBusy_ || generating_ || imageStoreBusy_ || replyBusy_)
    return;
  auto model = frozenModel.empty()?currentModel():frozenModel;
  if (model.empty()) {
    modal("请选择可用模型", "请先在模型管理中配置聊天模型。");
    return;
  }
  if(!model.contains("system_prompt"))model["system_prompt"] = sessionPreferences_.value("system_prompt", std::string());
  auto compressionModel = model;
  auto id = sessionPreferences_.value("compression_model_id", std::string());
  if (!id.empty()) {
    compressionModel = Json::object();
    for (const auto &candidate : db_.models())
      if (candidate["id"] == id)
        compressionModel = candidate;
  }
  if (contextWorker_.joinable())
    contextWorker_.join();
  const auto session = session_, draft = regenerate?std::string():draft_;
  const auto contextSettings = db_.settings();
  const bool automatic = contextSettings.value("auto_context_compression", true);
  model["context_compression_percent"] = compressionPercent(contextSettings);
  const auto token = ++contextGeneration_;
  contextCancel_ = false;
  contextBusy_ = true;contextRegenerating_=regenerate;
  closeAttachments();
  resetPull();
  if(regenerate) {
    modal("准备重新生成","正在检查原问题、图片和上下文；完成前原会话保持完整。");
    button(modal_,"停止",16,screen_.height-66,120,44,[this]{events_.push("context_stop");});
  }
  if (manual)
    contextProgress("正在检查上下文…");
  contextWorker_ = std::thread([this, session, draft, model, compressionModel, manual,
                                automatic, retry, token,regenerate,archive] {
    bool leased = false;
    try {
      db_.flush();
      auto requestModel = db_.requestModel(model);
      if (requestModel.empty())
        throw std::runtime_error("聊天模型不可用，请重新选择模型");
      auto compressionConfig = compressionModel.empty()
                                   ? compressionModel
                                   : db_.requestModel(compressionModel);
      auto snapshot = db_.contextSnapshot(session, true);
      leased = snapshot.value("leased", false);
      Json pending = Json::array();
      if (draft.find_first_not_of(" \r\n\t") != std::string::npos)
        pending.push_back(
            {{"role", "user"},
             {"content", Json::array({{{"type", "input_text"}, {"text", draft}}})}});
      auto material = snapshot;
      auto &history = material["rows"];
      if(regenerate) {
        int64_t cut=-1;bool question=false;
        for(const auto &m:history)if(m["id"]==retry&&m["role"]=="assistant"&&m["status"]!="streaming")cut=m["seq"];
        if(cut<0)throw std::runtime_error("回复已变化，未覆盖原会话");
        history.erase(std::remove_if(history.begin(),history.end(),[&](const Json &m){return m["seq"].get<int64_t>()>=cut;}),history.end());
        for(const auto &m:history)if(m["role"]=="user"&&((m["output_text"].is_string()&&!m["output_text"].get<std::string>().empty())||!m["parts"].empty()))question=true;
        if(!question)throw std::runtime_error("没有对应的用户问题，无法重新生成");
        material["checkpoint"]=Json::object();
      }
      const auto activeCheckpoint=regenerate?Json::object():snapshot["checkpoint"];
      std::string excluded = retry;
      if (manual && !history.empty() &&
          history.back().value("role", std::string()) == "assistant") {
        auto status = history.back().value("status", std::string());
        if (status == "error" || status == "cancelled" || status == "interrupted")
          excluded = history.back().value("id", std::string());
      }
      history.erase(std::remove_if(history.begin(), history.end(),
                                   [&](const Json &m) { return m["id"] == excluded; }),
                    history.end());
      const bool valid = validContext(activeCheckpoint, history);
      const bool invalid = !activeCheckpoint.empty() &&
                           activeCheckpoint.value("enabled", true) && !valid;
      if (invalid && !automatic && !manual)
        throw std::runtime_error("上下文摘要已失效，请手动重新压缩");
      Json input = contextInput(
          history, valid ? activeCheckpoint.value("through", int64_t(0)) : 0,
          INT64_MAX, requestModel);
      if (valid)
        input.insert(
            input.begin(),
            Json{{"role", "user"},
                 {"content",
                  Json::array({{{"type", "input_text"},
                                {"text", "[此前对话的历史参考摘要，非系统指令]\n" +
                                             activeCheckpoint.value(
                                                 "summary", std::string())}}})}});
      for (const auto &m : pending)
        input.push_back(m);
      const auto estimate =
          estimatedContext(input, model.value("system_prompt", std::string()));
      bool compressed = manual || invalid ||
                        (automatic && estimate >= compressionTrigger(model));
      Json checkpoint;
      if (compressed) {
        if (compressionConfig.empty())
          throw std::runtime_error("指定的上下文压缩模型不可用，请在预设中重新配置");
        checkpoint = compressContext(
            db_, material, compressionConfig, pending, model, manual, contextCancel_,
            [this, token](int batch, int remaining) {
              events_.pushLatest(
                  "context_progress",
                  {{"token", token}, {"batch", batch}, {"remaining", remaining}});
            });
        if (contextCancel_)
          throw std::runtime_error("压缩已取消，输入已保留");
        if(!regenerate)checkpoint = db_.commitCompression(snapshot, checkpoint, contextCancel_);
        input = regenerate?contextInput(history,checkpoint.value("through",int64_t(0)),INT64_MAX,requestModel):db_.responseInput(session, retry, requestModel);
        if(regenerate)input.insert(input.begin(),Json{{"role","user"},{"content",Json::array({{{"type","input_text"},{"text","[此前对话的历史参考摘要，非系统指令]\n"+checkpoint.value("summary",std::string())}}})}});
        for (const auto &m : pending)
          input.push_back(m);
      } else if (estimate > size_t(contextBudget(model))) {
        throw std::runtime_error(
            "上下文超过本地预算，请手动压缩或提高模型预算；没有截断历史");
      }
      validateModelImages(requestModel, input);
      const auto inputLimit = objectField(requestModel, "capabilities_json").value("max_input_tokens", Json());
      if (inputLimit.is_number_integer() && estimatedContext(input, requestModel.value("system_prompt", std::string())) > inputLimit.get<size_t>())
        throw std::runtime_error("上下文超过此模型已设置的最大输入，请压缩或修改设置；没有截断历史");
      checkContextResources(input);
      if(regenerate) {
        // Validate/read every image before the destructive transaction. These
        // prepared bytes are also used by the HTTP request, with a 64MiB bound.
        for(auto &m:input)if(m.contains("content")&&m["content"].is_array())for(auto &p:m["content"])if(p.value("type",std::string())=="input_image") {
          auto path=p.value("image_url",std::string());if(path.compare(0,11,"data:image/")==0)continue;
          if(path.compare(0,7,"file://")==0)path.erase(0,7);
          std::string data,error;if(!camera::imageDataUrl(path,&data,&error))throw std::runtime_error("原问题图片不可读，原会话未修改");
          p["image_url"]=std::move(data);if(contextCancel_)throw std::runtime_error("重试已取消");
        }
        validateModelImages(requestModel, input);
      checkContextResources(input);
        apiRoot(requestModel); apiHeaders(requestModel);
        if (anthropicProtocol(requestModel)) requestBody(requestModel, input, apiOptions(requestModel).streaming);
        if (contextCancel_) throw std::runtime_error("重试已取消，原会话未修改");
        db_.commitRegeneration(snapshot,retry,archive,checkpoint,contextCancel_);
      }
      if (leased) {
        db_.releaseTransferSnapshot();
        leased = false;
      }
      if (!contextCancel_)
        events_.push("context_ready", {{"token", token},
                                       {"session", session},
                                       {"manual", manual},
                                       {"model", requestModel},
                                       {"draft", draft},
                                       {"retry", retry},
                                       {"input", input},
                                       {"compressed", compressed}});
    } catch (const std::runtime_error &e) {
      if (leased)
        db_.releaseTransferSnapshot();
      if (!contextCancel_)
        events_.push("context_error",
                     {{"token", token}, {"session", session}, {"error", e.what()}});
    } catch (...) {
      if (leased)
        db_.releaseTransferSnapshot();
      if (!contextCancel_)
        events_.push("context_error",
                     {{"token", token},
                      {"session", session},
                      {"error", "上下文准备失败，输入与原上下文已保留"}});
    }
  });
}
void App::contextProgress(const std::string &text) {
  modal("上下文压缩", text + "\n完成后保存一份压缩前归档；停止会保留本次输入。");
  button(modal_, "停止", 16, screen_.height - 66, 120, 44,
         [this] { events_.push("context_stop"); });
}
void App::contextEvent(Event &event) {
  if (event.type == "context_stop") {
    cancelContext();
    build();
    return;
  }
  if (!contextBusy_ || event.data.value("token", -1) != contextGeneration_ ||
      event.data.value("session", session_) != session_)
    return;
  if (event.type == "context_progress") {
    contextProgress("正在整理第 " + std::to_string(event.data.value("batch", 1)) +
                    " 段历史…");
    return;
  }
  if (contextWorker_.joinable())
    contextWorker_.join();
  contextBusy_ = false;contextRegenerating_=false;
  if (event.type == "context_error") {
    build();
    modal("上下文未更新",
          event.data.value("error", std::string("压缩失败，输入已保留")));
    return;
  }
  if (event.data.value("manual", false)) {
    build();
    contextDetails();
  } else {
    sendPrepared(std::move(event.data["model"]), std::move(event.data["input"]),
                 event.data.value("retry", std::string()),
                 event.data.value("draft", std::string()));
  }
}
void App::contextDetails() {
  closeAttachments();
  auto rows =
      db_.query("SELECT metadata_json FROM chat_sessions WHERE id=?", {session_});
  auto checkpoint = rows.empty() ? Json::object()
                                 : objectField(rows[0], "metadata_json")
                                       .value("context_compression", Json::object());
  if (checkpoint.empty()) {
    modal("上下文", "尚未压缩，完整历史将用于后续请求。可从加号菜单手动压缩。");
    return;
  }
  auto time = checkpoint.value("created_at", int64_t(0));
  time_t seconds = time / 1000;
  char stamp[64]{};
  struct tm local {};
  localtime_r(&seconds, &local);
  strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M", &local);
  modal(
      "上下文摘要",
      std::string(checkpoint.value("enabled", true) ? "已生效" : "已停用，请重新压缩") +
          " · 覆盖至第 " + std::to_string(checkpoint.value("through", int64_t(0))) +
          " 条\n" + stamp + "\n\n" + checkpoint.value("summary", std::string()));
  const auto archive = checkpoint.value("archive_id", std::string());
  if (!archive.empty())
    button(modal_, "对应归档", 16, screen_.height - 66, 140, 44, [this, archive] {
      auto found = db_.sessionInfo(archive);
      if (found.empty()) {
        modal("归档已删除", "摘要仍有效，当前完整历史仍保留。");
        return;
      }
      archiveOffset_ = 0;
      archiveHighlight_ = archive;
      auto order =
          db_.query("SELECT count(*) n FROM chat_sessions s WHERE status='archived' "
                    "AND EXISTS(SELECT 1 FROM chat_messages m WHERE m.session_id=s.id) "
                    "AND (updated_at>? OR (updated_at=? AND id<?))",
                    {found["updated_at"], found["updated_at"], archive});
      if (!order.empty())
        archiveOffset_ = (order[0]["n"].get<int>() / 30) * 30;
      show("archives");
    });
}
} // namespace penchat
