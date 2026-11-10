#include "repository.hpp"
#include "bounded_json.hpp"
#include "image.hpp"
#include <algorithm>
#include <sstream>

namespace edgesearch::detail {
namespace {
struct ApiFailure { HttpResponse response; };
bool commit_id(const std::string& s) {
    return (s.size() == 40 || s.size() == 64) && std::all_of(s.begin(), s.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}
std::string path_encode(const std::string& path) {
    std::string result; std::size_t at = 0;
    while (at < path.size()) { const auto end = path.find('/', at); if (!result.empty()) result += '/'; result += encode(path.substr(at, end - at)); if (end == std::string::npos) break; at = end + 1; }
    return result;
}
std::string base64(const std::string& data) {
    const std::string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    unsigned bits = 0, count = 0; std::string out;
    bool padded = false;
    for (char c : data) {
        if (c == '\r' || c == '\n' || c == ' ') continue;
        if (c == '=') { padded = true; continue; }
        const auto n = chars.find(c); if (n == std::string::npos || padded) throw std::invalid_argument("invalid repository base64 content");
        bits = ((bits << 6U) | static_cast<unsigned>(n)) & 0xffffffU; count += 6;
        if (count >= 8) { count -= 8; out += static_cast<char>((bits >> count) & 255U); }
    }
    return out;
}
}
std::optional<Page> repository_page(const std::string& url, const BrowserOptions& options, const RepositoryFetch& fetch) {
    const bool github = url.compare(0, 19, "https://github.com/") == 0;
    const bool gitlab = url.compare(0, 19, "https://gitlab.com/") == 0;
    if (!github && !gitlab) return std::nullopt;
    auto path = url.substr(19); path = path.substr(0, path.find_first_of("?#"));
    while (!path.empty() && path.back() == '/') path.pop_back();
    const auto first = path.find('/'); if (first == std::string::npos || first == 0) return std::nullopt;
    std::string project, ref, file, mode;
    const auto marker = gitlab ? path.find("/-/") : path.find('/', first + 1);
    project = marker == std::string::npos ? path : path.substr(0, marker);
    if (project.size() > 4 && project.compare(project.size() - 4, 4, ".git") == 0) project.resize(project.size() - 4);
    if (marker != std::string::npos) {
        auto suffix = path.substr(marker + (gitlab ? 3 : 1));
        const auto slash = suffix.find('/'); if (slash == std::string::npos) return std::nullopt;
        mode = suffix.substr(0, slash);
        if (mode != "tree" && mode != "blob" && mode != "raw") return std::nullopt;
        suffix.erase(0, slash + 1);
        const auto next = suffix.find('/'); ref = decode(suffix.substr(0, next));
        file = next == std::string::npos ? "" : decode(suffix.substr(next + 1));
    }
    if (project.empty() || project.size() > 512 || file.size() > 4096 || ref.size() > 256) return std::nullopt;
    const auto api = github ? "https://api.github.com/repos/" + project : "https://gitlab.com/api/v4/projects/" + encode(decode(project));
    Page result; result.requested_url = url; result.url = url;
    try {
        auto get = [&](const std::string& target) {
            auto response = fetch(target);
            if (response.status != "ok") throw ApiFailure{std::move(response)};
            result.http_status = response.http_status; result.transport_code = response.transport_code; result.network = response.network;
            return Json::parse(response.body);
        };
        if (ref.empty()) {
            ref = get(api).str("default_branch");
            if (ref.empty()) throw std::invalid_argument("repository has no default branch");
        }
        auto commit = ref;
        if (!commit_id(commit)) commit = get(api + (github ? "/commits/" : "/repository/commits/") + encode(ref)).str(github ? "sha" : "id");
        if (!commit_id(commit)) throw std::invalid_argument("repository did not return an immutable commit ID");
        result.repository_commit = commit;
        const auto web = std::string(github ? "https://github.com/" : "https://gitlab.com/") + project + (github ? "/" : "/-/");
        const bool is_file = mode == "blob" || mode == "raw";
        Json data;
        if (github) data = get(api + "/contents/" + path_encode(file) + "?ref=" + encode(commit));
        else if (is_file) {
            try { data = get(api + "/repository/files/" + encode(file) + "?ref=" + encode(commit)); }
            catch (const ApiFailure& failure) {
                if (failure.response.http_status != 404) throw;
                data = get(api + "/repository/tree?ref=" + encode(commit) + "&path=" + encode(file) + "&per_page=100&page=1");
            }
        }
        else data = get(api + "/repository/tree?ref=" + encode(commit) + "&path=" + encode(file) + "&per_page=100&page=1");
        if (data.kind == Json::array) {
            std::string listing = project + " @ " + commit + "\n" + file + "\n";
            std::size_t page_number = 1;
            bool truncated = false;
            for (;;) {
                for (const auto& item : data.items) {
                    const auto name = item.str("name"), child = item.str("path"), type = item.str("type");
                    if (name.empty() || child.empty() || (type != "dir" && type != "tree" && type != "file" && type != "blob")) continue;
                    const auto target = web + ((type == "dir" || type == "tree") ? "tree/" : "blob/") + commit + "/" + path_encode(child);
                    const auto weight = name.size() + target.size() + sizeof(PageLink);
                    if (result.links.size() >= options.max_links || weight > options.max_link_bytes - std::min(result.link_bytes, options.max_link_bytes)) { truncated = true; break; }
                    result.link_bytes += weight;
                    result.links.push_back({result.links.size() + 1, name, target, image_url_mime(target).empty() ? "link" : "image", "", false});
                    listing += name + "\n";
                }
                if (github || truncated || data.items.size() < 100) { if (github && data.items.size() >= 1000) truncated = true; break; }
                if (++page_number > (options.max_links + 99) / 100 + 1) { truncated = true; break; }
                data = get(api + "/repository/tree?ref=" + encode(commit) + "&path=" + encode(file) + "&per_page=100&page=" + std::to_string(page_number));
                if (data.kind != Json::array) throw std::invalid_argument("invalid repository directory response");
            }
            result.text = truncate_utf8(listing, options.max_text_bytes); result.text_truncated = result.text.size() < listing.size();
            result.links_truncated = truncated;
            result.url = web + "tree/" + commit + (file.empty() ? "" : "/" + path_encode(file));
            result.title = project + (file.empty() ? "" : "/" + file);
        } else {
            if (data.str("encoding") != "base64") throw std::invalid_argument("repository file content is unavailable or too large");
            const auto content = base64(data.str("content"));
            auto image_type = image_mime(content);
            if (image_type.empty()) image_type = image_url_mime(file);
            if (image_type.empty() && (content.find('\0') != std::string::npos || !xmlCheckUTF8(BAD_CAST content.c_str()))) {
                result.status = "unsupported_type"; result.message = "binary repository file"; return result;
            }
            const auto pinned_url = web + "blob/" + commit + "/" + path_encode(file);
            // Markdown relative links resolve against the immutable raw-file directory.
            const auto base = github ? "https://raw.githubusercontent.com/" + project + "/" + commit + "/" + path_encode(file) : web + "raw/" + commit + "/" + path_encode(file);
            auto extracted = extract_page(content, base, image_type.empty() ? "text/plain" : image_type, options);
            if (!image_type.empty() && extracted.status != "ok") {
                result.status = extracted.status; result.message = extracted.message;
                result.fetch_complete = result.retained_complete = false;
                result.incomplete_reasons.push_back(result.status); return result;
            }
            result.resource_type = std::move(extracted.resource_type); result.image_mime_type = std::move(extracted.image_mime_type);
            result.image_data = std::move(extracted.image_data); result.total_image_bytes = extracted.total_image_bytes;
            result.text = std::move(extracted.text); result.links = std::move(extracted.links);
            result.link_bytes = extracted.link_bytes; result.text_truncated = extracted.text_truncated; result.links_truncated = extracted.links_truncated;
            for (auto& link : result.links) {
                const auto prefix = github ? "https://raw.githubusercontent.com/" + project + "/" + commit + "/" : web + "raw/" + commit + "/";
                if (link.kind != "image" && link.url.compare(0, prefix.size(), prefix) == 0) link.url = web + "blob/" + commit + "/" + link.url.substr(prefix.size());
                if (github && link.kind == "image") {
                    const auto raw_root = "https://raw.githubusercontent.com/" + project + "/";
                    if (link.url.compare(0, raw_root.size(), raw_root) == 0) {
                        const auto end = link.url.find('/', raw_root.size());
                        if (end != std::string::npos) link.url = raw_root + commit + link.url.substr(end);
                    }
                }
                // Moving self-links in README files stay in the same immutable snapshot.
                for (const auto* view : {"tree/", "blob/", "raw/"}) {
                    const auto moving = web + view;
                    if (link.url.compare(0, moving.size(), moving) != 0) continue;
                    const auto end = link.url.find('/', moving.size());
                    if (end != std::string::npos && !commit_id(link.url.substr(moving.size(), end - moving.size())))
                        link.url = moving + commit + link.url.substr(end);
                }
                const auto repository_root = std::string(github ? "https://github.com/" : "https://gitlab.com/") + project;
                if (link.url == repository_root || link.url == repository_root + "/") link.url = web + "tree/" + commit;
            }
            result.link_bytes = 0;
            auto keep = result.links.begin();
            for (; keep != result.links.end(); ++keep) {
                const auto weight = keep->url.size() + keep->text.size() + keep->rel.size() + sizeof(PageLink);
                if (weight > options.max_link_bytes - result.link_bytes) break;
                result.link_bytes += weight;
            }
            if (keep != result.links.end()) { result.links_truncated = true; result.links.erase(keep, result.links.end()); }
            result.url = pinned_url; result.title = project + "/" + file;
        }
        if (!file.empty()) {
            const auto add_navigation = [&](const std::string& label, const std::string& target) {
                if (std::any_of(result.links.begin(), result.links.end(), [&](const PageLink& link) { return link.url == target; })) return;
                const auto weight = label.size() + target.size() + sizeof(PageLink);
                if (result.links.size() >= options.max_links || weight > options.max_link_bytes - result.link_bytes) { result.links_truncated = true; return; }
                result.link_bytes += weight; result.links.push_back({result.links.size() + 1, label, target, "link", "", false});
            };
            add_navigation("Repository root", web + "tree/" + commit);
            const auto slash = file.find_last_of('/');
            if (slash != std::string::npos) add_navigation("Parent directory", web + "tree/" + commit + "/" + path_encode(file.substr(0, slash)));
        }
        result.content_type = result.resource_type == "image" ? result.image_mime_type : "text/plain; charset=utf-8"; result.status = "ok";
        result.total_text_bytes = result.text.size(); result.total_links = result.links.size();
        result.retained_complete = !result.text_truncated && !result.links_truncated;
        if (result.text_truncated) result.incomplete_reasons.push_back("retained_text_limit");
        if (result.links_truncated) result.incomplete_reasons.push_back("repository_directory_limit");
    } catch (const ApiFailure& failure) {
        result.http_status = failure.response.http_status; result.transport_code = failure.response.transport_code;
        result.status = (result.http_status == 429 || result.http_status == 403) ? "repository_api_limited" : failure.response.status;
        result.message = "repository API: " + failure.response.message;
        result.fetch_complete = false; result.incomplete_reasons.push_back(result.status);
    } catch (const std::exception& error) {
        result.status = "repository_unavailable"; result.message = error.what(); result.fetch_complete = false;
        result.incomplete_reasons.push_back(result.status);
    }
    return result;
}
} // namespace edgesearch::detail
