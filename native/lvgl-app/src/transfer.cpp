#include "transfer.hpp"
#include "chat_time.hpp"
#include "context.hpp"
#include "presets.hpp"
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <iomanip>
#include <miniz.h>
#include <regex>
#include <set>
#include <sqlite3.h>
#include <sstream>
#include <stdexcept>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
namespace penchat {
namespace {
constexpr uint64_t MaxBytes = 512ull * 1024 * 1024;
struct Failure : std::runtime_error {
  using std::runtime_error::runtime_error;
};
void require(bool ok, const char *why) {
  if (!ok)
    throw Failure(why);
}
bool ends(const std::string &v, const std::string &s) {
  return v.size() >= s.size() && v.compare(v.size() - s.size(), s.size(), s) == 0;
}
bool relative(const std::string &p) {
  if (p.empty() || p[0] == '/' || p.find('\\') != std::string::npos ||
      p.find('\0') != std::string::npos || p.size() > 240)
    return false;
  std::istringstream in(p);
  std::string s;
  while (std::getline(in, s, '/'))
    if (s.empty() || s == "." || s == ".." || s.find(':') != std::string::npos)
      return false;
  return true;
}
void safePath(const std::string &p, bool file = false) {
  require(!p.empty() && p[0] == '/', "文件路径无效");
  for (size_t at = 1;;) {
    auto end = p.find('/', at);
    auto part = end == std::string::npos ? p : p.substr(0, end);
    struct stat s {};
    require(lstat(part.c_str(), &s) == 0 && !S_ISLNK(s.st_mode),
            "目录或文件不可用，不能使用符号链接");
    require(end == std::string::npos ? (file ? S_ISREG(s.st_mode) : S_ISDIR(s.st_mode))
                                     : S_ISDIR(s.st_mode),
            "文件类型不支持");
    if (end == std::string::npos)
      break;
    at = end + 1;
  }
}
void directory(const std::string &p) {
  require(!p.empty() && p[0] == '/', "目录无效");
  for (size_t at = 1;;) {
    auto end = p.find('/', at);
    auto part = end == std::string::npos ? p : p.substr(0, end);
    struct stat s {};
    if (lstat(part.c_str(), &s) != 0)
      require(mkdir(part.c_str(), 0700) == 0, "无法创建目录，请检查存储权限和空间");
    safePath(part);
    if (end == std::string::npos)
      break;
    at = end + 1;
  }
}
void removeTree(const std::string &p) {
  struct stat s {};
  if (lstat(p.c_str(), &s))
    return;
  if (S_ISDIR(s.st_mode) && !S_ISLNK(s.st_mode)) {
    auto d = opendir(p.c_str());
    if (d) {
      while (auto e = readdir(d)) {
        std::string n = e->d_name;
        if (n != "." && n != "..")
          removeTree(join(p, n));
      }
      closedir(d);
    }
    rmdir(p.c_str());
  } else
    unlink(p.c_str());
}
uint64_t sizeOf(const std::string &p) {
  safePath(p, true);
  struct stat s {};
  require(stat(p.c_str(), &s) == 0 && s.st_size >= 0, "无法读取文件大小");
  return uint64_t(s.st_size);
}
void space(const std::string &p, uint64_t n) {
  struct statvfs s {};
  require(statvfs(p.c_str(), &s) == 0 &&
              uint64_t(s.f_bavail) * s.f_frsize >= n + 8 * 1024 * 1024,
          "剩余空间不足，请释放空间后重试");
}
std::string digest(const std::string &p) {
  safePath(p, true);
  FILE *f = fopen(p.c_str(), "rb");
  require(f, "无法读取校验文件");
  uint32_t crc = MZ_CRC32_INIT;
  char b[65536];
  size_t n;
  while ((n = fread(b, 1, sizeof(b), f)))
    crc = mz_crc32(crc, reinterpret_cast<const unsigned char *>(b), n);
  bool ok = !ferror(f);
  fclose(f);
  require(ok, "读取文件失败");
  std::ostringstream out;
  out << std::hex << std::setfill('0') << std::setw(8) << crc;
  return out.str();
}
std::string digestText(const std::string &s) {
  auto crc = mz_crc32(MZ_CRC32_INIT, reinterpret_cast<const unsigned char *>(s.data()),
                      s.size());
  std::ostringstream out;
  out << std::hex << std::setfill('0') << std::setw(8) << crc;
  return out.str();
}
void copyFile(const std::string &from, const std::string &to,
              const std::function<void()> &check) {
  auto size = sizeOf(from);
  require(size <= MaxBytes, "文件超过 512MiB 限制");
  int in = open(from.c_str(), O_RDONLY | O_NOFOLLOW),
      out = open(to.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
  if (in < 0 || out < 0) {
    if (in >= 0)
      close(in);
    if (out >= 0)
      close(out);
    throw Failure("无法准备文件");
  }
  try {
    char b[65536];
    uint64_t total = 0;
    for (;;) {
      check();
      auto n = read(in, b, sizeof(b));
      require(n >= 0, "读取文件失败");
      if (!n)
        break;
      total += n;
      require(total <= MaxBytes, "文件过大");
      ssize_t at = 0;
      while (at < n) {
        auto k = write(out, b + at, n - at);
        require(k > 0, "写入失败，请检查剩余空间");
        at += k;
      }
    }
    require(total == size && fsync(out) == 0, "文件复制不完整");
    close(in);
    close(out);
  } catch (...) {
    close(in);
    close(out);
    unlink(to.c_str());
    throw;
  }
}
void writeExclusive(const std::string &path, const std::string &text) {
  int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
  require(fd >= 0, "无法创建文件，目标可能已存在");
  try {
    size_t at = 0;
    while (at < text.size()) {
      auto n = write(fd, text.data() + at, text.size() - at);
      require(n > 0, "写入文件失败");
      at += size_t(n);
    }
    require(fsync(fd) == 0, "文件同步失败");
    close(fd);
  } catch (...) {
    close(fd);
    unlink(path.c_str());
    throw;
  }
}
std::string safeName(const std::string &raw) {
  std::string out;
  int n = 0;
  for (auto c : characters(raw)) {
    if (n++ >= 24)
      break;
    if (c.size() == 1 && (static_cast<unsigned char>(c[0]) < 32 ||
                          std::string("/\\:*?\"<>|.").find(c[0]) != std::string::npos))
      out += "_";
    else
      out += c;
  }
  return out.empty() ? "聊天备份" : out;
}
Json parseFile(const std::string &p, size_t *remaining = nullptr) {
  size_t local = 1000000;
  if (!remaining)
    remaining = &local;
  try {
    return Json::parse(
        fileText(p, 32 * 1024 * 1024), [&](int depth, Json::parse_event_t, Json &) {
          require(depth <= 64 && *remaining > 0, "文件结构过于复杂，请分批导入");
          --*remaining;
          return true;
        });
  } catch (const Failure &) {
    throw;
  } catch (...) {
    throw Failure("文件内容无效或文本超过 32MiB");
  }
}

std::string endpoint(std::string url) {
  auto scheme = url.find("://");
  if (scheme == std::string::npos)
    return "";
  auto authority = scheme + 3;
  auto slash = url.find('/', authority);
  auto at = url.find('@', authority);
  if (at != std::string::npos && (slash == std::string::npos || at < slash))
    url.erase(authority, at - authority + 1);
  auto query = url.find_first_of("?#");
  if (query != std::string::npos)
    url.resize(query);
  while (!url.empty() && url.back() == '/')
    url.pop_back();
  return url;
}
Json descriptor(const Json &m) {
  return {{"provider", m.value("provider_name", std::string())},
          {"endpoint", endpoint(m.value("base_url", std::string()))},
          {"api",
           objectField(m, "provider_metadata").value("api", std::string("responses"))},
          {"name", m.value("name", std::string())}};
}
Json kept(const Json &, std::initializer_list<const char *>);
Json cleanDescriptor(const Json &d) {
  if (d.empty())
    return Json::object();
  require(d.is_object() && d.at("endpoint").is_string() && d.at("api").is_string() &&
              d.at("name").is_string(),
          "模型描述无效");
  Json out = kept(d, {"provider", "endpoint", "api", "name"});
  out["endpoint"] = endpoint(d.at("endpoint").get<std::string>());
  require(out["api"] == "responses" || out["api"] == "chat", "模型接口类型无效");
  require(out["endpoint"].get<std::string>().size() <= 2048 &&
              out["name"].get<std::string>().size() <= 256,
          "模型描述过长");
  return out;
}
std::string modelKey(const Json &m) { return m.empty() ? "" : digestText(m.dump()); }
std::string match(const Json &d, const Json &models) {
  std::string id;
  int count = 0;
  for (const auto &m : models) {
    auto other = descriptor(m);
    if (other["endpoint"] == d.value("endpoint", Json()) &&
        other["api"] == d.value("api", Json()) &&
        other["name"] == d.value("name", Json())) {
      id = m.at("id");
      ++count;
    }
  }
  return count == 1 ? id : "";
}
Json kept(const Json &o, std::initializer_list<const char *> keys) {
  Json r = Json::object();
  for (auto k : keys)
    if (o.contains(k))
      r[k] = o[k];
  return r;
}
Json portableContextRows(const Json &conversation) {
  auto rows = conversation.at("messages");
  std::map<std::string, std::string> signatures;
  for (const auto &a : conversation.at("assets"))
    signatures[a.at("id").get<std::string>()] =
        Json::array({a.value("width", Json()), a.value("height", Json()),
                     a.value("size_bytes", Json())})
            .dump();
  for (auto &m : rows)
    for (auto &p : m["parts"])
      if (p.at("type") == "image")
        p["signature"] = signatures[p.at("asset").get<std::string>()];
  return rows;
}
Json publicMetadata(const Json &o) {
  auto out=kept(o, {"reasoning_observed", "reasoning_duration_ms", "reasoning_complete",
                  "reasoning_timing_source", "imported_reference"});
  auto time=publicTimeline(o.value("timeline",Json::object()));
  if(!time.empty())out["timeline"]=time;
  return out;
}
Json preferences(const Json &m) {
  return kept(m,
              {"system_prompt", "effort", "gauge", "preset_name", "preset_adjusted"});
}
struct ReadDB {
  sqlite3 *db = nullptr;
  explicit ReadDB(const std::string &p) {
    require(sqlite3_open_v2(p.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK,
            "无法读取数据快照");
  }
  ~ReadDB() {
    if (db)
      sqlite3_close(db);
  }
  Json q(const std::string &sql, const Json &args = Json::array()) {
    sqlite3_stmt *s = nullptr;
    require(sqlite3_prepare_v2(db, sql.c_str(), -1, &s, nullptr) == SQLITE_OK,
            "读取快照失败");
    for (size_t i = 0; i < args.size(); ++i) {
      auto v = args[i].get<std::string>();
      sqlite3_bind_text(s, i + 1, v.c_str(), -1, SQLITE_TRANSIENT);
    }
    Json out = Json::array();
    int rc;
    while ((rc = sqlite3_step(s)) == SQLITE_ROW) {
      Json row = Json::object();
      for (int i = 0; i < sqlite3_column_count(s); ++i) {
        std::string key = sqlite3_column_name(s, i);
        auto t = sqlite3_column_type(s, i);
        if (t == SQLITE_INTEGER)
          row[key] = sqlite3_column_int64(s, i);
        else if (t == SQLITE_NULL)
          row[key] = nullptr;
        else
          row[key] =
              std::string(reinterpret_cast<const char *>(sqlite3_column_text(s, i)),
                          sqlite3_column_bytes(s, i));
      }
      out.push_back(row);
    }
    sqlite3_finalize(s);
    require(rc == SQLITE_DONE, "读取快照失败");
    return out;
  }
};
struct Snapshot {
  Database &db;
  std::string path;
  Snapshot(Database &d, const std::string &p) : db(d), path(p) {
    db.transferSnapshot(path);
  }
  ~Snapshot() {
    db.releaseTransferSnapshot();
    unlink(path.c_str());
  }
};
struct Zip {
  FILE *stream = nullptr;
  mz_zip_archive z{};
  bool writer = false;
  ~Zip() {
    if (writer)
      mz_zip_writer_end(&z);
    else
      mz_zip_reader_end(&z);
    if (stream)
      fclose(stream);
  }
};
Json fileRecord(const std::string &p) {
  return {{"size", sizeOf(p)}, {"crc32", digest(p)}};
}
std::vector<std::pair<std::string, std::string>> markdownImages(const std::string &md) {
  std::vector<std::pair<std::string, std::string>> out;
  size_t at = 0;
  while ((at = md.find("![", at)) != std::string::npos) {
    auto middle = md.find("](", at + 2);
    if (middle == std::string::npos)
      break;
    size_t end = middle + 2;
    unsigned depth = 1;
    for (; end < md.size(); ++end) {
      if (md[end] == '\\' && end + 1 < md.size()) {
        ++end;
        continue;
      }
      if (md[end] == '(')
        ++depth;
      if (md[end] == ')' && !--depth)
        break;
    }
    if (end == md.size())
      break;
    auto ref = md.substr(middle + 2, end - middle - 2);
    if (ref.size() >= 2 && ref.front() == '<' && ref.back() == '>')
      ref = ref.substr(1, ref.size() - 2);
    out.emplace_back(md.substr(at, end - at + 1), ref);
    at = end + 1;
    require(out.size() <= 10000, "Markdown 图片数量过多");
  }
  return out;
}
void validatePreset(const Json &p) {
  require(p.is_object(), "预设格式无效");
  require(p.contains("name") && p["name"].is_string() &&
              !p["name"].get<std::string>().empty() &&
              characters(p["name"]).size() <= 64,
          "预设名称无效");
  auto mode = p.value("prompt_mode", std::string("inherit"));
  require(mode == "inherit" || mode == "clear" || mode == "custom", "提示词模式无效");
  require(p.value("prompt", std::string()).size() <= 32768, "提示词过长");
  for (auto key : {"model_when", "effort_when"}) {
    auto w = p.value(key, std::string("empty"));
    require(w == "always" || w == "empty", "预设应用时机无效");
  }
  auto effort = p.value("effort", std::string());
  require(effort.empty() || effort == "none" || effort == "api_default" ||
              effort == "low" || effort == "medium" || effort == "high" ||
              effort == "xhigh" || effort == "max",
          "推理强度无效");
}
} // namespace
Transfer::Transfer(Database &d, Events &e, const std::string &data)
    : db_(d), events_(e), data_(data), root_("/userdisk/Favorite/PenChat"),
      stage_(join(data, "transfer-tmp")) {
#ifndef __aarch64__
  root_ = join(data_, "transfer-files");
  if (auto p = getenv("PENCHAT_TRANSFER_ROOT"))
    root_ = p;
#endif
}
Transfer::~Transfer() {
  stop();
  removeTree(stage_);
}
void Transfer::check() {
  if (cancel_)
    throw Failure("操作已取消");
}
void Transfer::stop() {
  cancel_ = true;
  if (worker_.joinable())
    worker_.join();
  busy_ = false;
}
bool Transfer::finish(bool persistent) {
  stop();
  if (persistent)
    return true;
  try {
    struct stat st {};
    if (lstat(root_.c_str(), &st) != 0)
      return errno == ENOENT;
    safePath(root_);
    auto target = join(root_, "导出");
    if (lstat(target.c_str(), &st) != 0)
      return errno == ENOENT;
    safePath(target);
    directory(stage_);
    // Retire the visible folder atomically before slow deletion. If the host
    // force-kills shutdown, the private residue is collected on next launch.
    auto retired = join(stage_, uid("retired_exports"));
    if (rename(target.c_str(), retired.c_str()) != 0)
      return false;
    return lstat(target.c_str(), &st) != 0 && errno == ENOENT;
  } catch (...) {
    return false;
  }
}
bool Transfer::start(const std::string &op, Json args) {
  if (busy_)
    return false;
  if (worker_.joinable())
    worker_.join();
  cancel_ = false;
  busy_ = true;
  auto token = ++token_;
  worker_ = std::thread([this, op, args, token] {
    Event event;
    try {
      auto result = execute(op, args);
      event = {"transfer_result", {{"op", op}, {"token", token}, {"data", result}}};
    } catch (const Failure &e) {
      event = {"transfer_error", {{"op", op}, {"token", token}, {"error", e.what()}}};
    } catch (...) {
      event = {"transfer_error",
               {{"op", op},
                {"token", token},
                {"error", "操作失败：文件格式、数据或存储状态异常"}}};
    }
    busy_ = false;
    events_.push(event.type, event.data);
  });
  return true;
}
void Transfer::directories() {
  for (auto dir : {"导出/聊天", "导出/预设", "导入/聊天", "导入/预设"})
    directory(join(root_, dir));
  auto path = join(root_, "使用说明.txt");
  struct stat s {};
  if (lstat(path.c_str(), &s))
    writeExclusive(path,
                   "PenChat 导入与导出\n聊天包：.penchat.zip，解压后 markdown "
                   "目录可阅读，assets 保存附件。\n预设：.penchat-presets.json。普通 "
                   "Markdown：.md，可附带相对路径图片。\n将文件复制到 导入/聊天 或 "
                   "导入/"
                   "预设，再到设置中的导入与导出刷新并选择。\n导入保留原文件，不会自"
                   "动替换现有聊天。默认预设替换需要单独确认。\n导出文件包含聊天与提"
                   "示词，请自行妥善保管；不含密钥。\n");
  else
    safePath(path, true);
}
Json Transfer::execute(const std::string &op, const Json &a) {
  check();
  directories();
  if (op == "init") {
    removeTree(stage_);
    directory(stage_);
    const bool warning = db_.settings().value("export_cleanup_failed", false);
    if (warning)
      db_.setting("export_cleanup_failed", false);
    return {{"root", root_}, {"cleanup_warning", warning}};
  }
  if (op == "choices") {
    if (a.value("kind", std::string()) == "presets")
      return {{"items", presetList(db_.settings())}};
    return {{"items", db_.sessions(a.value("offset", 0), a.value("archived", false))}};
  }
  if (op == "list") {
    auto kind = a.value("kind", std::string("chats"));
    auto folder = join(root_, kind == "presets" ? "导入/预设" : "导入/聊天");
    safePath(folder);
    Json files = Json::array();
    auto d = opendir(folder.c_str());
    require(d, "无法浏览导入目录");
    while (auto e = readdir(d)) {
      std::string n = e->d_name;
      if (n == "." || n == "..")
        continue;
      if (!(kind == "presets" ? ends(n, ".penchat-presets.json")
                              : (ends(n, ".penchat.zip") || ends(n, ".md"))))
        continue;
      try {
        auto bytes = sizeOf(join(folder, n));
        files.push_back({{"name", n}, {"bytes", bytes}});
      } catch (...) {
      }
      if (files.size() >= 1000)
        break;
    }
    closedir(d);
    std::sort(files.begin(), files.end(),
              [](const Json &x, const Json &y) { return x["name"] < y["name"]; });
    return {{"items", files}, {"path", folder}};
  }
  if (op == "export") {
    auto result = exportData(a);
    removeTree(stage_);
    return result;
  }
  if (op == "preview")
    return previewFile(a);
  if (op == "import") {
    auto result = importData(a);
    removeTree(stage_);
    return result;
  }
  throw Failure("未知操作");
}
Json Transfer::exportData(const Json &a) {
  removeTree(stage_);
  directory(stage_);
  preview_ = Json();
  db_.flush();
  require(db_.storageStats().value("unsaved", size_t(0)) == 0,
          "有内容尚未保存，请先重试保存后导出");
  Snapshot lease(db_, join(stage_, "snapshot.db"));
  ReadDB snapshot(lease.path);
  auto catalogue = snapshot.q(
      "SELECT m.id,m.name,p.name provider_name,p.base_url,p.metadata_json "
      "provider_metadata FROM model_configs m JOIN providers p ON p.id=m.provider_id");
  auto describe = [&](const std::string &id) {
    for (const auto &m : catalogue)
      if (m["id"] == id)
        return descriptor(m);
    return Json::object();
  };
  auto selected = a.value("ids", Json::array());
  bool all = a.value("all", false);
  auto wants = [&](const std::string &id) {
    return all ||
           std::find(selected.begin(), selected.end(), Json(id)) != selected.end();
  };
  auto kind = a.value("kind", std::string("chats"));
  Json bundle = {{"format", "penchat"},
                 {"version", 1},
                 {"kind", kind},
                 {"app_version", PENCHAT_VERSION},
                 {"items", Json::array()}};
  Json files = Json::object(), missing = Json::array();
  uint64_t total = 0, jsonTotal = 0;
  auto record = [&](const std::string &name) {
    auto r = fileRecord(join(stage_, name));
    total += r["size"].get<uint64_t>();
    require(total <= MaxBytes, "导出内容超过 512MiB，请分批导出");
    if (ends(name, ".json")) {
      jsonTotal += r["size"].get<uint64_t>();
      require(jsonTotal <= 32 * 1024 * 1024, "备份文本合计超过 32MiB，请分批导出");
    }
    files[name] = r;
  };
  auto assetFile = [&](const std::string &src, const std::string &name) {
    check();
    if (files.contains(name))
      return true;
    uint64_t bytes;
    try {
      bytes = sizeOf(src);
    } catch (const Failure &) {
      missing.push_back(name);
      return false;
    }
    space(stage_, bytes);
    copyFile(src, join(stage_, name), [this] { check(); });
    record(name);
    return true;
  };
  if (kind == "presets") {
    Json settings = Json::object();
    auto rows =
        snapshot.q("SELECT value_json FROM app_settings WHERE key='chat_presets'");
    if (!rows.empty())
      settings["chat_presets"] = Json::parse(rows[0]["value_json"].get<std::string>());
    for (auto p : presetList(settings)) {
      if (!wants(p.at("id")))
        continue;
      auto id = p.at("id").get<std::string>();
      p = kept(p, {"id", "name", "prompt_mode", "prompt", "model_when", "effort",
                   "effort_when"});
      for (const auto &original : presetList(settings))
        if (original["id"] == id)
          p["model"] = describe(original.value("model_id", std::string()));
      if (p["model"].empty())
        for (const auto &old : presetList(settings))
          if (old["id"] == id)
            p["model"] = old.value("transfer_model", Json::object());
      for (const auto &original : presetList(settings))
        if (original["id"] == id) {
          p["compression_model"] =
              describe(original.value("compression_model_id", std::string()));
          if (p["compression_model"].empty())
            p["compression_model"] =
                original.value("transfer_compression_model", Json::object());
        }
      p["origin"] = {{"source", id}, {"checksum", digestText(p.dump())}};
      bundle["items"].push_back(p);
    }
  } else {
    directory(join(stage_, "conversations"));
    directory(join(stage_, "markdown"));
    directory(join(stage_, "assets"));
    Json sessions = Json::array();
    if (all) {
      sessions = snapshot.q("SELECT * FROM chat_sessions WHERE EXISTS(SELECT 1 FROM "
                            "chat_messages m WHERE m.session_id=chat_sessions.id) "
                            "ORDER BY pinned_at DESC,updated_at DESC,id LIMIT 2001");
      require(sessions.size() <= 2000, "会话超过 2000 项，请分批勾选导出");
    } else {
      require(selected.is_array() && selected.size() <= 2000,
              "一次最多导出 2000 个会话");
      for (const auto &id : selected) {
        require(id.is_string(), "会话选择无效");
        for (const auto &row :
             snapshot.q("SELECT * FROM chat_sessions WHERE id=?", Json::array({id})))
          sessions.push_back(row);
      }
    }
    for (const auto &s : sessions) {
      check();
      auto id = s.at("id").get<std::string>();
      if (!wants(id))
        continue;
      auto textBytes =
          snapshot.q("SELECT "
                     "coalesce(sum(length(CAST(output_text AS "
                     "BLOB))+coalesce(length(CAST(reasoning_text AS BLOB)),"
                     "0)),0) AS n FROM chat_messages WHERE session_id=?",
                     {id});
      require(textBytes[0]["n"].get<uint64_t>() <= 16 * 1024 * 1024,
              "单个会话文本超过 16MiB，暂不能导出");
      auto rows = snapshot.q(
          "SELECT * FROM chat_messages WHERE session_id=? ORDER BY seq", {id});
      int lastAssistant = 0;
      bool unstable = false;
      for (const auto &m : rows) {
        if (m["role"] == "assistant")
          lastAssistant = m.at("seq");
        if (m["status"] == "streaming")
          unstable = true;
      }
      require(!unstable, "选中的会话仍在生成，请完成或停止后导出");
      auto meta = objectField(s, "metadata_json");
      Json c = kept(s, {"id", "title", "status", "created_at", "updated_at",
                        "last_message_at", "pinned_at", "preview_text"});
      c["metadata"] = {{"chat_preferences",
                        preferences(meta.value("chat_preferences", Json::object()))}};
      if(meta.contains("timeline"))c["metadata"]["timeline"]=publicTimeline(meta["timeline"]);
      c["model"] = describe(s["default_model_config_id"].is_string()
                                ? s["default_model_config_id"].get<std::string>()
                                : "");
      if (c["model"].empty())
        c["model"] = meta.value("transfer_model", Json::object());
      auto compressionId = meta.value("chat_preferences", Json::object())
                               .value("compression_model_id", std::string());
      c["compression_model"] = describe(compressionId);
      if (c["compression_model"].empty())
        c["compression_model"] =
            meta.value("transfer_compression_model", Json::object());
      if (meta.contains("compression_archive"))
        c["metadata"]["compression_archive"] =
            kept(meta["compression_archive"], {"label", "created_at"});
      c["messages"] = Json::array();
      c["assets"] = Json::array();
      std::set<std::string> seen;
      auto sessionAssets =
          snapshot.q("SELECT * FROM message_assets WHERE session_id=?", {id});
      std::string md = "# " + s.at("title").get<std::string>() + "\n\n";
      for (const auto &m : rows) {
        check();
        if (m["role"] == "user" && m["message_type"] == "image" &&
            m.at("seq").get<int>() > lastAssistant)
          continue;
        Json message = kept(
            m, {"seq", "role", "status", "message_type", "created_at", "updated_at"});
        message["text"] = m["output_text"].is_string() ? m["output_text"] : Json("");
        message["reasoning"] =
            m["reasoning_text"].is_string() ? m["reasoning_text"] : Json("");
        message["metadata"] = publicMetadata(objectField(m, "metadata_json"));
        message["request"] =
            kept(objectField(m, "request_json"),
                 {"model", "effort", "effort_value", "summary", "system_prompt"});
        message["model"] = describe(m["model_config_id"].is_string()
                                        ? m["model_config_id"].get<std::string>()
                                        : "");
        if (message["model"].empty())
          message["model"] =
              objectField(m, "metadata_json").value("transfer_model", Json::object());
        message["parts"] = Json::array();
        md += "## " + std::string(m["role"] == "user" ? "用户" : "助手") + "\n\n";
        auto reason = message["reasoning"].get<std::string>();
        if (!reason.empty())
          md += "<details>\n<summary>公开思考</summary>\n\n" + reason +
                "\n\n</details>\n\n";
        md += message["text"].get<std::string>() + "\n\n";
        for (const auto &p : snapshot.q(
                 "SELECT * FROM message_parts WHERE message_id=? ORDER BY part_index",
                 Json::array({m["id"]}))) {
          if (p["part_type"] == "text") {
            message["parts"].push_back(
                {{"type", "text"},
                 {"text", p["text"].is_string() ? p["text"] : Json("")}});
            continue;
          }
          if (p["part_type"] != "image" || !p["asset_id"].is_string())
            continue;
          auto assetId = p["asset_id"].get<std::string>();
          auto assets =
              snapshot.q("SELECT * FROM message_assets WHERE id=?", {assetId});
          if (assets.empty()) {
            missing.push_back("缺失的图片记录");
            continue;
          }
          auto asset = assets[0];
          auto src = asset["stored_path"].is_string()
                         ? asset["stored_path"].get<std::string>()
                         : "";
          if (src.empty() && asset["original_path"].is_string())
            src = asset["original_path"];
          std::string extension = "img";
          auto dot = src.rfind('.');
          if (dot != std::string::npos) {
            auto ext = src.substr(dot + 1);
            if (std::regex_match(ext, std::regex("[A-Za-z0-9]{1,8}")))
              extension = ext;
          }
          auto file = "assets/" + safeName(assetId) + "-" + digestText(assetId) + "." +
                      extension;
          if (!assetFile(src, file))
            continue;
          if (seen.insert(assetId).second) {
            auto item =
                kept(asset, {"id", "mime_type", "width", "height", "size_bytes"});
            item["file"] = file;
            item["preview_file"] = "";
            if (asset["display_src"].is_string() &&
                !asset["display_src"].get<std::string>().empty()) {
              auto preview = file + ".preview.png";
              if (assetFile(asset["display_src"], preview))
                item["preview_file"] = preview;
            }
            c["assets"].push_back(item);
          }
          message["parts"].push_back({{"type", "image"}, {"asset", assetId}});
          md += "![图片](../" + file + ")\n\n";
        }
        auto text = message["text"].get<std::string>();
        for (const auto &asset : sessionAssets) {
          auto source = asset["stored_path"].is_string()
                            ? asset["stored_path"].get<std::string>()
                            : "";
          if (source.empty() || text.find(source) == std::string::npos)
            continue;
          auto aid = asset.at("id").get<std::string>(),
               file = "assets/inline-" + digestText(aid) + ".img";
          if (!assetFile(source, file))
            continue;
          if (seen.insert(aid).second) {
            auto item =
                kept(asset, {"id", "mime_type", "width", "height", "size_bytes"});
            item["file"] = file;
            item["preview_file"] = "";
            c["assets"].push_back(item);
          }
          size_t at = 0;
          auto marker = "penchat-asset:" + aid;
          while ((at = text.find(source, at)) != std::string::npos) {
            text.replace(at, source.size(), marker);
            at += marker.size();
          }
        }
        message["text"] = text;
        if (message["parts"].empty() && !text.empty())
          message["parts"].push_back({{"type", "text"}, {"text", text}});
        for (auto &part : message["parts"])
          if (part["type"] == "text")
            part["text"] = text;
        c["messages"].push_back(message);
      }
      if (c["messages"].empty())
        continue;
      md = "# " + s.at("title").get<std::string>() + "\n\n";
      for (const auto &m : c["messages"]) {
        md += "## " + std::string(m["role"] == "user" ? "用户" : "助手") + "\n\n";
        auto text = m.at("text").get<std::string>();
        for (const auto &asset : c["assets"]) {
          auto marker = "penchat-asset:" + asset.at("id").get<std::string>(),
               target = "../" + asset.at("file").get<std::string>();
          size_t at = 0;
          while ((at = text.find(marker, at)) != std::string::npos) {
            text.replace(at, marker.size(), target);
            at += target.size();
          }
        }
        auto reason = m.at("reasoning").get<std::string>();
        if (!reason.empty())
          md += "<details>\n<summary>公开思考</summary>\n\n" + reason +
                "\n\n</details>\n\n";
        md += text + "\n\n";
        for (const auto &part : m["parts"])
          if (part["type"] == "image")
            for (const auto &asset : c["assets"])
              if (asset["id"] == part["asset"])
                md += "![图片](../" + asset.at("file").get<std::string>() + ")\n\n";
      }
      if (meta.contains("context_compression")) {
        auto cp = publicContext(meta["context_compression"]);
        auto portable = portableContextRows(c), original = portable;
        for (auto &m : original)
          for (const auto &raw : rows)
            if (m["seq"] == raw["seq"])
              m["text"] =
                  raw["output_text"].is_string() ? raw["output_text"] : Json("");
        if (validContext(cp, original))
          cp["checksum"] = contextChecksum(portable, cp.at("through").get<int64_t>());
        else
          cp["enabled"] = false;
        c["metadata"]["context_compression"] = cp;
      }
      c["origin"] = {{"source", id}, {"checksum", digestText(c.dump())}};
      auto cid = "conversations/" + digestText(id) + "-" +
                 std::to_string(bundle["items"].size()) + ".json";
      auto markdown = "markdown/" + safeName(s["title"]) + "-" + digestText(id) + ".md";
      auto encoded = c.dump();
      require(encoded.size() <= 32 * 1024 * 1024 && md.size() <= 32 * 1024 * 1024,
              "会话序列化文本超过 32MiB，请缩小导出范围");
      atomicText(join(stage_, cid), encoded);
      atomicText(join(stage_, markdown), md);
      record(cid);
      record(markdown);
      bundle["items"].push_back(
          {{"file", cid}, {"title", s["title"]}, {"markdown", markdown}});
      events_.pushLatest("transfer_progress", {{"done", bundle["items"].size()},
                                               {"label", "正在整理聊天与图片"}});
    }
  }
  require(!bundle["items"].empty(), "没有可导出的内容");
  if (!missing.empty() && !a.value("allow_missing", false))
    return {{"missing", missing}, {"needs_missing", true}};
  bundle["missing"] = missing;
  auto output = join(root_, kind == "presets" ? "导出/预设" : "导出/聊天");
  auto name =
      (kind == "presets"             ? std::string("预设")
       : bundle["items"].size() == 1 ? bundle["items"][0]["title"].get<std::string>()
                                     : std::string("聊天备份"));
  auto leaf = safeName(name) + "-" + std::to_string(nowMs()) + "-" +
              digestText(uid("export")) +
              (kind == "presets" ? ".penchat-presets.json" : ".penchat.zip");
  auto final = join(output, leaf), partial = join(stage_, "output.part");
  struct stat stagingDevice {
  }, outputDevice{};
  require(stat(stage_.c_str(), &stagingDevice) == 0 &&
              stat(output.c_str(), &outputDevice) == 0 &&
              stagingDevice.st_dev == outputDevice.st_dev,
          "导出目录与应用数据不在同一存储分区，无法原子发布文件");
  try {
    if (kind == "presets") {
      check();
      auto encoded = bundle.dump(2);
      require(encoded.size() <= 32 * 1024 * 1024 && bundle["items"].size() <= 2000,
              "预设文件过大，请分批导出");
      writeExclusive(partial, encoded);
    } else {
      bundle["files"] = files;
      auto manifest = bundle.dump(2);
      require(jsonTotal + manifest.size() <= 32 * 1024 * 1024,
              "备份文本合计超过 32MiB，请分批导出");
      atomicText(join(stage_, "manifest.json"), manifest);
      space(output, total + 8 * 1024 * 1024);
      Zip zip;
      zip.writer = true;
      int output = open(partial.c_str(), O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
      require(output >= 0, "无法创建导出文件");
      zip.stream = fdopen(output, "w+b");
      if (!zip.stream)
        close(output);
      require(zip.stream && mz_zip_writer_init_cfile(&zip.z, zip.stream, 0),
              "无法初始化聊天包");
      auto addFile = [&](const std::string &name) {
        auto path = join(stage_, name);
        auto bytes = sizeOf(path);
        int fd = open(path.c_str(), O_RDONLY | O_NOFOLLOW);
        require(fd >= 0, "无法读取导出内容");
        struct Source {
          int fd;
          std::atomic<bool> *cancel;
        } source{fd, &cancel_};
        auto reader = [](void *opaque, mz_uint64 offset, void *buffer,
                         size_t size) -> size_t {
          auto &s = *static_cast<Source *>(opaque);
          if (*s.cancel)
            return 0;
          auto n = pread(s.fd, buffer, size, off_t(offset));
          return n > 0 ? size_t(n) : 0;
        };
        bool ok = mz_zip_writer_add_read_buf_callback(
            &zip.z, name.c_str(), reader, &source, bytes, nullptr, nullptr, 0,
            MZ_NO_COMPRESSION, nullptr, 0, nullptr, 0);
        close(fd);
        check();
        require(ok, "写入聊天包失败");
      };
      for (auto it = files.begin(); it != files.end(); ++it) {
        check();
        addFile(it.key());
      }
      addFile("manifest.json");
      check();
      require(mz_zip_writer_finalize_archive(&zip.z) && fflush(zip.stream) == 0,
              "完成聊天包失败");
    }
    check();
    int fd = open(partial.c_str(), O_RDONLY | O_NOFOLLOW);
    require(fd >= 0, "导出文件无效");
    bool durable = fsync(fd) == 0;
    close(fd);
    require(durable, "导出文件保存失败");
    require(link(partial.c_str(), final.c_str()) == 0, "导出文件发布失败");
    unlink(partial.c_str());
    int dir = open(output.c_str(), O_RDONLY | O_DIRECTORY);
    if (dir >= 0) {
      fsync(dir);
      close(dir);
    }
  } catch (...) {
    unlink(partial.c_str());
    throw;
  }
  return {{"path", final}, {"count", bundle["items"].size()}, {"missing", missing}};
}
Json Transfer::previewFile(const Json &a) {
  preview_ = Json();
  removeTree(stage_);
  directory(stage_);
  check();
  auto kind = a.value("kind", std::string("chats"));
  auto name = a.at("name").get<std::string>();
  require(relative(name) && name.find('/') == std::string::npos,
          "请选择导入列表中的文件");
  auto source = join(join(root_, kind == "presets" ? "导入/预设" : "导入/聊天"), name);
  auto sourceBytes = sizeOf(source);
  space(stage_, sourceBytes * 2);
  Json bundle, missing = Json::array();
  size_t parseBudget = 1000000;
  if (kind == "presets") {
    require(ends(name, ".penchat-presets.json"), "预设文件扩展名不支持");
    copyFile(source, join(stage_, "input.json"), [this] { check(); });
    bundle = parseFile(join(stage_, "input.json"), &parseBudget);
  } else if (ends(name, ".penchat.zip")) {
    copyFile(source, join(stage_, "input.zip"), [this] { check(); });
    Zip zip;
    require(mz_zip_reader_init_file(&zip.z, join(stage_, "input.zip").c_str(), 0),
            "聊天包损坏或格式不支持");
    auto count = mz_zip_reader_get_num_files(&zip.z);
    require(count > 0 && count <= 10000, "聊天包文件数量不支持");
    std::set<std::string> names;
    uint64_t total = 0;
    for (mz_uint i = 0; i < count; ++i) {
      check();
      mz_zip_archive_file_stat st{};
      require(mz_zip_reader_file_stat(&zip.z, i, &st), "无法读取聊天包目录");
      auto len = mz_zip_reader_get_filename(&zip.z, i, nullptr, 0);
      require(len > 1 && len <= 241, "聊天包路径过长");
      char path[256]{};
      mz_zip_reader_get_filename(&zip.z, i, path, sizeof(path));
      std::string leaf(path);
      require((leaf == "manifest.json" || leaf.compare(0, 7, "assets/") == 0 ||
               leaf.compare(0, 9, "markdown/") == 0 ||
               leaf.compare(0, 14, "conversations/") == 0) &&
                  relative(leaf) && names.insert(leaf).second,
              "聊天包包含不安全或重复路径");
      require(!st.m_is_encrypted && !st.m_is_directory &&
                  ((st.m_external_attr >> 16) & 0170000) != 0120000,
              "不支持加密包、目录条目或符号链接");
      require(st.m_uncomp_size <= MaxBytes - total, "聊天包解压内容超过 512MiB");
      total += st.m_uncomp_size;
    }
    space(stage_, total);
    for (mz_uint i = 0; i < count; ++i) {
      check();
      mz_zip_archive_file_stat st{};
      mz_zip_reader_file_stat(&zip.z, i, &st);
      std::string leaf = st.m_filename;
      auto slash = leaf.rfind('/');
      if (slash != std::string::npos)
        directory(join(stage_, leaf.substr(0, slash)));
      auto dest = join(stage_, leaf);
      int fd = open(dest.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
      require(fd >= 0, "无法准备解包文件");
      struct Sink {
        int fd;
        uint64_t total, limit;
        std::atomic<bool> *cancel;
      };
      Sink sink{fd, 0, st.m_uncomp_size, &cancel_};
      auto callback = [](void *p, mz_uint64 offset, const void *data,
                         size_t n) -> size_t {
        auto &s = *static_cast<Sink *>(p);
        if (*s.cancel || offset != s.total || n > s.limit - s.total)
          return 0;
        size_t at = 0;
        while (at < n) {
          auto wrote = write(s.fd, static_cast<const char *>(data) + at, n - at);
          if (wrote <= 0)
            return 0;
          at += size_t(wrote);
        }
        s.total += n;
        return n;
      };
      bool ok = mz_zip_reader_extract_to_callback(&zip.z, i, callback, &sink, 0) &&
                sink.total == sink.limit && fsync(fd) == 0;
      close(fd);
      check();
      require(ok, "聊天包解压或校验失败");
    }
    bundle = parseFile(join(stage_, "manifest.json"), &parseBudget);
    uint64_t jsonBytes = sizeOf(join(stage_, "manifest.json"));
    require(bundle.contains("files") && bundle["files"].is_object(),
            "聊天包缺少校验清单");
    for (const auto &leaf : names)
      require(leaf == "manifest.json" || bundle["files"].contains(leaf),
              "聊天包含未声明文件");
    for (auto it = bundle["files"].begin(); it != bundle["files"].end(); ++it) {
      require(relative(it.key()), "校验路径无效");
      auto path = join(stage_, it.key());
      if (!names.count(it.key())) {
        if (it.key().compare(0, 7, "assets/") == 0) {
          missing.push_back(it.key());
          continue;
        }
        throw Failure("聊天包缺少会话文件");
      }
      require(sizeOf(path) == it.value().at("size").get<uint64_t>() &&
                  digest(path) == it.value().at("crc32").get<std::string>(),
              "聊天包文件校验失败");
    }
    Json items = Json::array();
    require(bundle.at("items").is_array(), "会话索引无效");
    for (const auto &index : bundle["items"]) {
      check();
      auto file = index.at("file").get<std::string>();
      require(file.compare(0, 14, "conversations/") == 0 &&
                  bundle["files"].contains(file),
              "会话索引路径无效");
      jsonBytes += sizeOf(join(stage_, file));
      require(jsonBytes <= 32 * 1024 * 1024, "备份文本合计超过 32MiB，请分批导入");
      items.push_back(parseFile(join(stage_, file), &parseBudget));
    }
    bundle["items"] = items;
  } else if (ends(name, ".md")) {
    copyFile(source, join(stage_, "input.md"), [this] { check(); });
    auto md = fileText(join(stage_, "input.md"), 32 * 1024 * 1024);
    require(!md.empty(), "Markdown 文件为空或过大");
    Json assets = Json::array(), images = Json::array();
    directory(join(stage_, "assets"));
    std::set<std::string> seen;
    Json(md).dump(); // Reject invalid UTF-8 before it can enter the database.
    for (const auto &entry : markdownImages(md)) {
      auto token = entry.first, ref = entry.second;
      if (!seen.insert(token).second)
        continue;
      if (ref.compare(0, 7, "http://") == 0 || ref.compare(0, 8, "https://") == 0) {
        images.push_back({{"token", token}, {"link", true}, {"asset", ""}});
        continue;
      }
      auto aid = "md_" + std::to_string(assets.size()), file = "assets/" + aid + ".img";
      bool available = relative(ref);
      auto original = join(join(root_, "导入/聊天"), ref);
      if (available) {
        try {
          sizeOf(original);
        } catch (const Failure &) {
          available = false;
        }
      }
      if (!available) {
        missing.push_back(ref);
        images.push_back({{"token", token}, {"asset", ""}});
        continue;
      }
      copyFile(original, join(stage_, file), [this] { check(); });
      assets.push_back({{"id", aid},
                        {"file", file},
                        {"preview_file", ""},
                        {"width", 0},
                        {"height", 0},
                        {"size_bytes", sizeOf(join(stage_, file))}});
      images.push_back({{"token", token}, {"ref", ref}, {"asset", aid}});
    }
    auto id = digestText(md);
    Json c = {
        {"id", id},
        {"title", safeName(name.substr(0, name.size() - 3))},
        {"status", "active"},
        {"created_at", nowMs()},
        {"updated_at", nowMs()},
        {"pinned_at", 0},
        {"model", Json::object()},
        {"metadata", Json::object()},
        {"assets", assets},
        {"markdown_images", images},
        {"origin", {{"source", "markdown:" + name}, {"checksum", id}}},
        {"messages",
         Json::array({{{"seq", 1},
                       {"role", "assistant"},
                       {"status", "complete"},
                       {"message_type", "text"},
                       {"created_at", nowMs()},
                       {"updated_at", nowMs()},
                       {"text", md},
                       {"reasoning", ""},
                       {"metadata", {{"imported_reference", true}}},
                       {"model", Json::object()},
                       {"parts", Json::array({{{"type", "text"}, {"text", md}}})}}})}};
    bundle = {{"format", "penchat"},
              {"version", 1},
              {"kind", "chats"},
              {"items", Json::array({c})}};
  } else
    throw Failure("请选择 PenChat 聊天包或 Markdown 文件");
  require(bundle.at("format") == "penchat" && bundle.at("version") == 1 &&
              bundle.at("kind") == kind,
          "文件类型或格式版本不支持");
  require(bundle.at("items").is_array() && !bundle["items"].empty() &&
              bundle["items"].size() <= 2000,
          "导入项目数量无效");
  auto models = db_.models();
  Json modelRows = Json::array(), summary = Json::array();
  std::set<std::string> modelSeen;
  int pictures = 0;
  uint64_t bytes = 0;
  auto noteModel = [&](const Json &d) {
    if (d.empty())
      return;
    require(d.is_object() && d.at("endpoint").is_string() && d.at("api").is_string() &&
                d.at("name").is_string(),
            "模型描述无效");
    auto key = modelKey(d);
    if (modelSeen.insert(key).second)
      modelRows.push_back({{"key", key}, {"descriptor", d}, {"id", match(d, models)}});
  };
  auto oldPresets = presetList(db_.settings());
  auto oldSessions = db_.query("SELECT metadata_json FROM chat_sessions WHERE "
                               "metadata_json LIKE '%transfer_origin%'");
  for (size_t i = 0; i < bundle["items"].size(); ++i) {
    check();
    auto &item = bundle["items"][i];
    item["model"] = cleanDescriptor(item.value("model", Json::object()));
    item["compression_model"] =
        cleanDescriptor(item.value("compression_model", Json::object()));
    noteModel(item["compression_model"]);
    if (kind == "chats")
      for (auto &message : item["messages"])
        message["model"] = cleanDescriptor(message.value("model", Json::object()));
    auto origin = kept(item.at("origin"), {"source", "checksum"});
    item["origin"] = origin;
    require(origin.is_object() && origin.at("source").is_string() &&
                origin.at("checksum").is_string(),
            "来源标记无效");
    bool duplicate = false;
    if (kind == "presets") {
      validatePreset(item);
      for (const auto &p : oldPresets)
        if (p.value("transfer_origin", Json()) == origin)
          duplicate = true;
      noteModel(item.value("model", Json::object()));
    } else {
      require(item.at("title").is_string() &&
                  item.at("title").get<std::string>().size() <= 1024,
              "会话标题无效");
      require(item.at("messages").is_array() && !item["messages"].empty() &&
                  item["messages"].size() <= 100000,
              "消息数量无效");
      require(item.at("assets").is_array(), "资产列表无效");
      for (const auto &s : oldSessions)
        if (objectField(s, "metadata_json").value("transfer_origin", Json()) == origin)
          duplicate = true;
      for (auto key : {"created_at", "updated_at"})
        require(item.at(key).is_number_integer(), "会话时间无效");
      if (item.contains("pinned_at"))
        require(item["pinned_at"].is_number_integer(), "置顶时间无效");
      noteModel(item.value("model", Json::object()));
      std::set<std::string> assetIds;
      for (const auto &asset : item["assets"]) {
        auto id = asset.at("id").get<std::string>();
        require(assetIds.insert(id).second, "资产 ID 重复");
        for (auto field : {"file", "preview_file"}) {
          auto file = asset.value(field, std::string());
          if (file.empty())
            continue;
          require(relative(file) && file.compare(0, 7, "assets/") == 0, "图片路径无效");
          struct stat st {};
          if (lstat(join(stage_, file).c_str(), &st)) {
            missing.push_back(file);
            continue;
          }
          bytes += sizeOf(join(stage_, file));
        }
        ++pictures;
      }
      int64_t seq = 0;
      for (const auto &m : item["messages"]) {
        require(m.at("created_at").is_number_integer() &&
                    m.at("updated_at").is_number_integer(),
                "消息时间无效");
        require(m.at("seq").is_number_integer(), "消息序号无效");
        auto next = m.at("seq").get<int64_t>();
        require(next > seq, "消息顺序重复或无效");
        seq = next;
        auto role = m.at("role").get<std::string>();
        require(role == "user" || role == "assistant" || role == "system",
                "消息角色不支持");
        auto status = m.at("status").get<std::string>();
        require(status == "complete" || status == "cancelled" || status == "error" ||
                    status == "interrupted",
                "消息状态不支持");
        require(m.at("text").is_string() && m.at("reasoning").is_string() &&
                    m.at("parts").is_array(),
                "消息内容无效");
        noteModel(m.value("model", Json::object()));
        for (const auto &p : m["parts"]) {
          auto t = p.at("type").get<std::string>();
          require(t == "text" || t == "image", "消息分段类型不支持");
          if (t == "image")
            require(assetIds.count(p.at("asset").get<std::string>()),
                    "消息关联的图片记录缺失");
        }
      }
    }
    bytes += item.dump().size();
    item["duplicate"] = duplicate;
    std::string warning;
    if (!item["compression_model"].empty() &&
        match(item["compression_model"], models).empty())
      warning = "压缩模型待配置；";
    if (kind == "chats") {
      auto cp = item.value("metadata", Json::object())
                    .value("context_compression", Json::object());
      if (!cp.empty()) {
        bool valid = false;
        try {
          valid = validContext(cp, portableContextRows(item));
        } catch (...) {
        }
        if (!valid || !missing.empty()) {
          cp = publicContext(cp);
          cp["enabled"] = false;
          warning += "摘要覆盖范围不匹配或附件缺失，保留历史并停用摘要，请重新压缩";
        }
        item["metadata"]["context_compression"] = cp;
      }
    }
    if (kind == "presets" && !item.value("model", Json::object()).empty()) {
      auto modelId = match(item["model"], models);
      if (modelId.empty())
        warning = "模型待配置";
      else
        for (const auto &m : models)
          if (m["id"] == modelId &&
              !presetSupports(m, item.value("effort", std::string())))
            warning = "推理强度不兼容，应用时保留会话选择";
    }
    summary.push_back(
        {{"index", i},
         {"name", item.value(kind == "presets" ? "name" : "title", std::string())},
         {"duplicate", duplicate},
         {"warning", warning},
         {"is_default",
          kind == "presets" && item.value("id", std::string()) == "default"}});
  }
  std::set<std::string> missingSet;
  for (const auto &entry : missing)
    missingSet.insert(entry.get<std::string>());
  missing = Json::array();
  for (const auto &entry : missingSet)
    missing.push_back(entry);
  preview_ = bundle;
  preview_["missing"] = missing;
  preview_["models"] = modelRows;
  Json result = {{"kind", kind},         {"items", summary}, {"models", modelRows},
                 {"pictures", pictures}, {"bytes", bytes},   {"missing", missing},
                 {"file", name}};
  for (const auto &item : bundle["items"])
    if (kind == "presets" && item.value("id", std::string()) == "default")
      result["default_diff"] = {{"before", oldPresets[0]}, {"after", item}};
  return result;
}
Json Transfer::importData(const Json &a) {
  require(preview_.is_object() && !preview_.empty(), "请先选择并预览导入文件");
  require(preview_["missing"].empty() || a.value("allow_missing", false),
          "部分附件缺失，请确认仅导入可用内容");
  auto kind = preview_.at("kind").get<std::string>();
  auto chosen = a.value("indices", Json::array());
  require(!chosen.empty(), "请选择要导入的内容");
  auto models = db_.models();
  auto maps = a.value("mapping", Json::object());
  auto resolve = [&](const Json &d) {
    if (d.empty())
      return std::string();
    auto key = modelKey(d);
    auto id = maps.value(key, match(d, models));
    if (!id.empty()) {
      bool exists = false;
      for (const auto &m : models)
        if (m["id"] == id)
          exists = true;
      require(exists, "指定模型已不可用，请重新选择");
      return id;
    }
    return "missing_model_" + key;
  };
  Snapshot lease(db_, join(stage_, "commit-snapshot.db"));
  directory(join(data_, "assets"));
  space(data_, 0);
  Json bundle = {
      {"kind", kind}, {"copy", a.value("copy", false)}, {"items", Json::array()}};
  std::vector<std::string> created;
  std::map<std::string, std::string> copied;
  auto prepare = [&](const std::string &file) {
    check();
    if (file.empty())
      return std::string();
    if (copied.count(file))
      return copied[file];
    auto source = join(stage_, file);
    struct stat s {};
    if (lstat(source.c_str(), &s))
      return std::string();
    space(data_, sizeOf(source));
    auto target = join(join(data_, "assets"), uid("asset") + ".img");
    copyFile(source, target, [this] { check(); });
    created.push_back(target);
    copied[file] = target;
    return target;
  };
  try {
    std::set<size_t> seen;
    for (const auto &index : chosen) {
      check();
      auto i = index.get<size_t>();
      require(i < preview_["items"].size() && seen.insert(i).second, "导入选择无效");
      auto item = preview_["items"][i];
      if (item.value("duplicate", false) && !bundle["copy"].get<bool>())
        continue;
      if (kind == "presets") {
        auto action = a.value("default_action", std::string("replace"));
        bool def = item.value("id", std::string()) == "default";
        if (def && action == "skip")
          continue;
        require(!def || action != "replace" || a.value("default_confirm", false),
                "替换默认预设需要单独确认");
        auto p = kept(item, {"id", "name", "prompt_mode", "prompt", "model_when",
                             "effort", "effort_when", "origin"});
        p["model_id"] = resolve(item.value("model", Json::object()));
        p["transfer_model"] = item.value("model", Json::object());
        p["compression_model_id"] =
            resolve(item.value("compression_model", Json::object()));
        p["transfer_compression_model"] =
            item.value("compression_model", Json::object());
        p["replace_default"] = def && action == "replace";
        bundle["items"].push_back(p);
      } else {
        auto c = kept(item, {"title", "status", "created_at", "updated_at",
                             "last_message_at", "pinned_at", "preview_text", "origin"});
        if (!c.contains("status") ||
            (c["status"] != "active" && c["status"] != "archived"))
          c["status"] = "active";
        c["model_id"] = resolve(item.value("model", Json::object()));
        auto pref = preferences(item.value("metadata", Json::object())
                                    .value("chat_preferences", Json::object()));
        pref["model_id"] = c["model_id"];
        pref["preset_adjusted"] = true;
        pref["compression_model_id"] =
            resolve(item.value("compression_model", Json::object()));
        c["metadata"] = {{"chat_preferences", pref},
                         {"transfer_model", item.value("model", Json::object())},
                         {"transfer_compression_model",
                          item.value("compression_model", Json::object())}};
        auto importedMeta = item.value("metadata", Json::object());
        if(importedMeta.contains("timeline"))c["metadata"]["timeline"]=publicTimeline(importedMeta["timeline"]);
        if (importedMeta.contains("context_compression"))
          c["metadata"]["context_compression"] =
              publicContext(importedMeta["context_compression"]);
        if (importedMeta.contains("compression_archive"))
          c["metadata"]["compression_archive"] =
              kept(importedMeta["compression_archive"], {"label", "created_at"});
        c["assets"] = Json::array();
        std::map<std::string, std::string> paths;
        for (const auto &asset : item["assets"]) {
          auto path = prepare(asset.at("file"));
          if (path.empty())
            continue;
          auto x = kept(asset, {"id", "width", "height", "size_bytes"});
          x["mime_type"] = asset.contains("mime_type") && asset["mime_type"].is_string()
                               ? asset["mime_type"]
                               : Json("");
          x["path"] = path;
          x["preview"] = prepare(asset.value("preview_file", std::string()));
          paths[x.at("id").get<std::string>()] = path;
          c["assets"].push_back(x);
        }
        c["messages"] = Json::array();
        for (const auto &m : item["messages"]) {
          check();
          auto message = kept(m, {"seq", "role", "status", "message_type", "created_at",
                                  "updated_at", "text", "reasoning", "parts"});
          message["metadata"] = publicMetadata(m.value("metadata", Json::object()));
          message["metadata"]["transfer_model"] = m.value("model", Json::object());
          message["request"] =
              kept(m.value("request", Json::object()),
                   {"model", "effort", "effort_value", "summary", "system_prompt"});
          message["model_id"] = resolve(m.value("model", Json::object()));
          auto text = message.at("text").get<std::string>();
          auto replaceAll = [&](const std::string &needle, const std::string &value) {
            size_t at = 0;
            while ((at = text.find(needle, at)) != std::string::npos) {
              text.replace(at, needle.size(), value);
              at += value.size();
            }
          };
          for (const auto &path : paths)
            replaceAll("penchat-asset:" + path.first, path.second);
          if (item.contains("markdown_images")) {
            for (const auto &image : item["markdown_images"]) {
              auto token = image.at("token").get<std::string>(),
                   id = image.at("asset").get<std::string>();
              if (image.value("link", false))
                replaceAll(token, token.substr(1));
              else if (paths.count(id))
                replaceAll(token, "![图片](" + paths[id] + ")");
              else
                replaceAll(token, "[图片不可用]");
            }
          }
          message["text"] = text;
          for (auto &part : message["parts"])
            if (part["type"] == "text")
              part["text"] = text;
          if (message["message_type"] == "image") {
            bool available = false;
            for (const auto &part : message["parts"])
              if (part["type"] == "image" &&
                  paths.count(part.value("asset", std::string())))
                available = true;
            if (!available) {
              message["message_type"] = "text";
              message["text"] = "[图片不可用]";
              message["parts"] =
                  Json::array({{{"type", "text"}, {"text", "[图片不可用]"}}});
            }
          }
          c["messages"].push_back(message);
        }
        if (c["messages"].size())
          c["preview_text"] = std::string();
        for (const auto &cp :
             characters(c["messages"].back()["text"].get<std::string>())) {
          if (characters(c["preview_text"]).size() >= 40)
            break;
          c["preview_text"] = c["preview_text"].get<std::string>() + cp;
        }
        if (c["metadata"].contains("context_compression")) {
          auto &cp = c["metadata"]["context_compression"];
          cp["import_validated"] = validContext(cp, portableContextRows(item)) &&
                                   preview_["missing"].empty();
          if (!cp["import_validated"].get<bool>())
            cp["enabled"] = false;
        }
        bundle["items"].push_back(c);
      }
    }
    check();
    require(!bundle["items"].empty(), "没有新的可导入内容，重复项已跳过");
    auto result = db_.importTransfer(bundle);
    preview_ = Json();
    return result;
  } catch (...) {
    for (const auto &file : created)
      unlink(file.c_str());
    throw;
  }
}
} // namespace penchat
