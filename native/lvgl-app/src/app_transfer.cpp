#include "app.hpp"
namespace penchat {
void App::openTransfer(const std::string &screen, const std::string &session) {
  closeHistoryContext();
  transferScreen_ = screen;
  transferError_.clear();
  transferData_ = Json::object();
  transferSelected_.clear();
  transferOffset_ = 0;
  transferArchived_ = false;
  if (!session.empty())
    transferSelected_.insert(session);
  show("transfer");
  if (screen == "export_chats") {
    transferKind_ = "chats";
    transferRequest("choices", {{"kind", "chats"}});
  }
}
void App::transferRequest(const std::string &op, Json args) {
  if (transfer_->busy())
    return;
  if (op == "import") {
    if (args.value("indices", Json::array()).empty()) {
      modal("请选择内容", "请至少勾选一项内容。");
      return;
    }
    if (transferPreview_.contains("default_diff") &&
        transferDefaultAction_ == "replace" && !transferDefaultConfirmed_) {
      bool selected = false;
      for (const auto &row : transferPreview_["items"])
        if (row.value("is_default", false) &&
            transferIndices_.count(row.at("index").get<size_t>()))
          selected = true;
      if (selected) {
        modal("确认默认预设替换",
              "请查看差异，并勾选确认替换；也可以改为导入普通预设。");
        return;
      }
    }
  }
  if (op == "export") {
    bool includes = args.value("all", false);
    for (const auto &id : args.value("ids", Json::array()))
      if (id == session_)
        includes = true;
    if (args.value("kind", std::string()) == "chats" && includes &&
        (contextBusy_ || generating_ || persistDirty_ || saveInFlight_ ||
         imageStoreBusy_)) {
      modal("会话尚未就绪", "请等待回复和图片保存完成，或停止生成后再导出。");
      return;
    }
    transferExportArgs_ = args;
  }
  transferError_.clear();
  transferOp_ = op;
  if (!transfer_->start(op, args))
    return;
  if (view_ == "transfer")
    build();
}
void App::transferEvent(const Event &e) {
  if (e.type == "transfer_open_session") {
    openTransfer("export_chats", e.data.at("id"));
    return;
  }
  if (e.data.contains("token") && e.data["token"] != transfer_->token())
    return;
  if (e.type == "transfer_progress") {
    if (transferProgress_)
      lv_label_set_text(transferProgress_,
                        (e.data.value("label", std::string("正在处理")) + " · " +
                         std::to_string(e.data.value("done", 0)))
                            .c_str());
    return;
  }
  auto op = e.data.value("op", std::string());
  if (e.type == "transfer_error") {
    transferError_ = e.data.value("error", std::string("操作失败"));
    if (op == "init") {
      notice_ = "导入导出目录不可用：" + transferError_;
      if (view_ == "chat")
        modal("导入导出目录不可用", transferError_);
      return;
    }
    if (view_ == "transfer")
      build();
    return;
  }
  if (e.type != "transfer_result")
    return;
  if (op == "init") {
    if (e.data.at("data").value("cleanup_warning", false)) {
      notice_ = "上次退出时未能清理导出目录，请检查目录权限。";
      if (view_ == "chat" && !editor_.active)
        modal("导出目录清理未完成", notice_);
    }
    return;
  }
  const auto &data = e.data.at("data");
  if (op == "choices" || op == "list")
    transferData_ = data;
  else if (op == "preview") {
    transferPreview_ = data;
    transferIndices_.clear();
    transferMapping_ = Json::object();
    transferDefaultAction_ = "replace";
    transferDefaultConfirmed_ = false;
    transferMissing_ = false;
    transferCopy_ = false;
    for (const auto &row : data.at("items"))
      if (!row.value("duplicate", false))
        transferIndices_.insert(row.at("index").get<size_t>());
    for (const auto &m : data.at("models"))
      transferMapping_[m.at("key").get<std::string>()] = m.at("id");
    transferScreen_ = "preview";
  } else if (op == "export" && data.value("needs_missing", false)) {
    modal("部分附件不可用", "可取消并检查原图，也可明确选择仅导出可用内容。");
    button(modal_, "仅导出可用内容", 20, 200, 260, 44, [this] {
      auto args = transferExportArgs_;
      args["allow_missing"] = true;
      lv_obj_delete(modal_);
      transferRequest("export", args);
    });
    return;
  } else {
    transferData_ = data;
    transferScreen_ = "done";
    historyDirty_ = true;
  }
  if (view_ == "transfer")
    build();
}
void App::transferBack() {
  if (transfer_->busy()) {
    transfer_->cancel();
    return;
  }
  transferError_.clear();
  if (transferScreen_ == "home") {
    show("settings");
    return;
  }
  if (transferScreen_ == "mapping") {
    transferScreen_ = "preview";
    build();
    return;
  }
  if (transferScreen_ == "preview") {
    transferScreen_ = transferKind_ == "presets" ? "import_presets" : "import_chats";
    transferRequest("list", {{"kind", transferKind_}});
    return;
  }
  transferScreen_ = "home";
  build();
}
void App::transferPage() {
  const int width = screen_.width - 118, column = (width - 14) / 2,
            right = 10 + column + 14;
  pageHeader(transferScreen_ == "home"             ? "导入与导出"
             : transferScreen_ == "preview"        ? "导入预览"
             : transferScreen_ == "mapping"        ? "选择本地模型"
             : transferScreen_ == "done"           ? "操作完成"
             : transferScreen_.find("export") == 0 ? "选择导出内容"
                                                   : "选择导入文件");
  auto action = [&](const std::string &name, int x, int y, std::function<void()> fn,
                    int w = 0) {
    return button(body_, name, x, y, w ? w : column, 48, std::move(fn), theme::surface,
                  LV_TEXT_ALIGN_LEFT);
  };
  if (transfer_->busy()) {
    transferProgress_ = label(body_,
                              transferOp_ == "preview" ? "正在验证文件与附件…"
                              : transferOp_ == "import" ? "正在保存导入内容…"
                              : transferOp_ == "export" ? "正在导出…"
                                                        : "正在读取…",
                              12, 12, width, 23);
    label(body_, "正在处理文件，请稍候。", 12, 58, width, 19, theme::muted);
    action("取消", 10, 114, [this] { transfer_->cancel(); });
    return;
  }
  int top = 0;
  if (!transferError_.empty()) {
    auto hint = label(body_, transferError_, 12, 4, column, 19, theme::muted);
    lv_obj_set_height(hint, 44);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_MODE_DOTS);
    action("查看原因 / 关闭提示", right, 0, [this] {
      auto message = transferError_;
      transferError_.clear();
      build();
      modal("操作未完成", message);
    });
    top = 60;
  }
  if (transferScreen_ == "home") {
    label(body_, transfer_->root(), 12, top + 4, width, 18, theme::muted);
    auto choose = [this](const std::string &screen, const std::string &kind,
                         const std::string &op) {
      transferScreen_ = screen;
      transferKind_ = kind;
      transferSelected_.clear();
      transferOffset_ = 0;
      transferArchived_ = false;
      transferData_ = Json::object();
      transferRequest(op, {{"kind", kind}});
    };
    action("聊天导出", 10, top + 36,
           [choose] { choose("export_chats", "chats", "choices"); });
    action("聊天导入", right, top + 36,
           [choose] { choose("import_chats", "chats", "list"); });
    action("预设导出", 10, top + 96,
           [choose] { choose("export_presets", "presets", "choices"); });
    action("预设导入", right, top + 96,
           [choose] { choose("import_presets", "presets", "list"); });
    label(body_, "将文件放入对应的导入目录，刷新后选择。", 12, top + 158, width, 18,
          theme::muted);
    return;
  }
  if (transferScreen_ == "done") {
    label(body_,
          transferData_.contains("path")
              ? "导出文件已保存"
              : "导入完成：新增 " + std::to_string(transferData_.value("added", 0)) +
                    " 项",
          12, top + 4, width, 24);
    label(body_,
          transferData_.value("path", std::string("现有会话和全局默认配置已保留。")),
          12, top + 44, width, 19, theme::muted);
    action("返回导入与导出", 10, top + 140, [this] {
      transferScreen_ = "home";
      build();
    });
    return;
  }
  if (transferScreen_ == "mapping") {
    action(
        "待配置（保留原模型名称）", 10, top + 4,
        [this] {
          transferMapping_[transferMapKey_] = "";
          transferScreen_ = "preview";
          build();
        },
        width);
    int y = top + 62;
    for (const auto &m : db_.models()) {
      auto id = m.at("id").get<std::string>();
      action(
          m.value("provider_name", std::string()) + " / " +
              m.value("name", std::string()),
          10, y,
          [this, id] {
            transferMapping_[transferMapKey_] = id;
            transferScreen_ = "preview";
            build();
          },
          width);
      y += 56;
    }
    return;
  }
  if (transferScreen_ == "preview") {
    auto &p = transferPreview_;
    auto filename = button(
        body_, p.value("file", std::string()), 10, top, width, 44,
        [this] { modal("完整文件名", transferPreview_.value("file", std::string())); },
        theme::background, LV_TEXT_ALIGN_LEFT);
    lv_obj_set_style_text_font(lv_obj_get_child(filename, 0), font(18), 0);
    label(body_,
          std::to_string(p.at("items").size()) + " 项 / " +
              std::to_string(p.value("pictures", 0)) + " 张图片 / 约 " +
              std::to_string((p.value("bytes", uint64_t(0)) + 1023) / 1024) + " KiB",
          12, top + 48, width, 20);
    action("导入所选（" + std::to_string(transferIndices_.size()) + "）", 10, top + 84,
           [this] {
             Json indices = Json::array();
             for (auto i : transferIndices_)
               indices.push_back(i);
             transferRequest("import",
                             {{"indices", indices},
                              {"mapping", transferMapping_},
                              {"copy", transferCopy_},
                              {"allow_missing", transferMissing_},
                              {"default_action", transferDefaultAction_},
                              {"default_confirm", transferDefaultConfirmed_}});
           });
    action(transferCopy_ ? "重复内容：另存副本" : "重复内容：跳过", right, top + 84,
           [this] {
             transferCopy_ = !transferCopy_;
             build();
           });
    int y = top + 144;
    if (!p.at("missing").empty()) {
      action("查看缺失附件（" + std::to_string(p["missing"].size()) + "）", 10, y,
             [this] {
               std::string text;
               for (const auto &file : transferPreview_["missing"])
                 text += file.get<std::string>() + "\n";
               modal("缺失附件", text);
             });
      action(transferMissing_ ? "✓ 仅导入可用附件" : "□ 允许仅导入可用附件", right, y,
             [this] {
               transferMissing_ = !transferMissing_;
               build();
             });
      y += 56;
    }
    if (p.contains("default_diff")) {
      auto mode = transferDefaultAction_ == "replace" ? "替换默认预设"
                  : transferDefaultAction_ == "copy"  ? "导为普通预设"
                                                      : "跳过默认预设";
      action(mode, 10, y, [this] {
        transferDefaultAction_ = transferDefaultAction_ == "replace" ? "copy"
                                 : transferDefaultAction_ == "copy"  ? "skip"
                                                                     : "replace";
        transferDefaultConfirmed_ = false;
        build();
      });
      action("查看默认预设差异", right, y, [this] {
        auto before = transferPreview_["default_diff"]["before"],
             after = transferPreview_["default_diff"]["after"];
        modal("默认预设差异", "当前：\n" + presetSummary(before) + "\n" +
                                  before.value("prompt", std::string()) +
                                  "\n\n导入：\n" +
                                  after.value("model", Json::object())
                                      .value("name", std::string("沿用模型")) +
                                  " · " + presetSummary(after) + "\n" +
                                  after.value("prompt", std::string()));
      });
      y += 56;
      if (transferDefaultAction_ == "replace") {
        action(
            transferDefaultConfirmed_ ? "✓ 已确认替换默认预设"
                                      : "□ 确认替换当前默认预设",
            10, y,
            [this] {
              transferDefaultConfirmed_ = !transferDefaultConfirmed_;
              build();
            },
            width);
        y += 56;
      }
    }
    for (const auto &m : p.at("models")) {
      auto key = m.at("key").get<std::string>();
      auto id = transferMapping_.value(key, std::string());
      std::string local = "待配置";
      for (const auto &model : db_.models())
        if (model["id"] == id)
          local = model.value("name", std::string());
      auto title = m["descriptor"].value("provider", std::string()) + " / " +
                   m["descriptor"].value("name", std::string()) + " → " + local;
      action(
          title, 10, y,
          [this, key] {
            transferMapKey_ = key;
            transferScreen_ = "mapping";
            build();
          },
          width);
      y += 56;
    }
    for (const auto &row : p.at("items")) {
      auto i = row.at("index").get<size_t>();
      auto title = std::string(transferIndices_.count(i) ? "✓ " : "□ ") +
                   row.at("name").get<std::string>() +
                   (row.value("duplicate", false) ? " · 已导入" : "") +
                   (row.value("warning", std::string()).empty()
                        ? ""
                        : " · " + row.at("warning").get<std::string>());
      action(
          title, 10, y,
          [this, i] {
            if (transferIndices_.count(i))
              transferIndices_.erase(i);
            else
              transferIndices_.insert(i);
            build();
          },
          width);
      y += 56;
    }
    return;
  }
  const bool exporting = transferScreen_.find("export_") == 0;
  const auto rows = transferData_.value("items", Json::array());
  int y = top;
  if (exporting) {
    auto send = [this](bool all) {
      Json ids = Json::array();
      for (const auto &id : transferSelected_)
        ids.push_back(id);
      transferRequest("export", {{"kind", transferKind_}, {"ids", ids}, {"all", all}});
    };
    action("导出所选（" + std::to_string(transferSelected_.size()) + "）", 10, y,
           [send] { send(false); });
    action(transferKind_ == "chats" ? "导出全部（含归档）" : "导出全部预设", right, y,
           [send] { send(true); });
    y += 56;
    if (transferKind_ == "chats") {
      action(transferArchived_ ? "归档会话 · 点击切换" : "活动会话 · 点击切换", 10, y,
             [this] {
               transferArchived_ = !transferArchived_;
               transferOffset_ = 0;
               transferRequest("choices", {{"kind", transferKind_},
                                           {"offset", 0},
                                           {"archived", transferArchived_}});
             });
      action("全选当前页", right, y, [this, rows] {
        for (const auto &r : rows)
          transferSelected_.insert(r.at("id").get<std::string>());
        build();
      });
      y += 56;
    }
  } else {
    action("刷新文件列表", 10, y,
           [this] { transferRequest("list", {{"kind", transferKind_}}); });
    action("目录说明", right, y, [this] {
      modal("导入目录",
            transferData_.value("path", std::string()) + "\n支持 PenChat 文件" +
                (transferKind_ == "chats" ? std::string("和 Markdown；原文件保留。")
                                          : std::string("；原文件保留。")));
    });
    y += 56;
  }
  if (rows.empty()) {
    label(body_, "暂无可选内容", 12, y + 8, width, 22, theme::muted);
    y += 48;
  }
  for (const auto &row : rows) {
    auto id = row.at(exporting ? "id" : "name").get<std::string>();
    auto text = exporting ? row.value(transferKind_ == "presets" ? "name" : "title",
                                      std::string())
                          : id;
    action((exporting ? std::string(transferSelected_.count(id) ? "✓ " : "□ ") : "") +
               text,
           10, y,
           [this, id, exporting] {
             if (exporting) {
               if (transferSelected_.count(id))
                 transferSelected_.erase(id);
               else
                 transferSelected_.insert(id);
               build();
             } else
               transferRequest("preview", {{"kind", transferKind_}, {"name", id}});
           },
           width);
    y += 56;
  }
  if (exporting && transferKind_ == "chats") {
    if (transferOffset_ > 0)
      action("上一页", 10, y, [this] {
        transferOffset_ = std::max(0, transferOffset_ - 30);
        transferRequest("choices", {{"kind", "chats"},
                                    {"offset", transferOffset_},
                                    {"archived", transferArchived_}});
      });
    if (rows.size() == 30)
      action("下一页", right, y, [this] {
        transferOffset_ += 30;
        transferRequest("choices", {{"kind", "chats"},
                                    {"offset", transferOffset_},
                                    {"archived", transferArchived_}});
      });
  }
}
} // namespace penchat
