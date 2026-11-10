#pragma once

#include "search.hpp"

namespace edgesearch {

struct BrowserOptions {
    Options network;
    std::size_t max_text_bytes = 2 * 1024 * 1024;
    std::size_t max_links = 4096;
    std::size_t max_link_bytes = 2 * 1024 * 1024;
    std::size_t cache_bytes = 8 * 1024 * 1024;
    std::size_t max_response_bytes = 8 * 1024 * 1024;
    std::size_t request_per_engine_limit = 50;
    std::size_t max_pages = 8;
    // Explicit opt-in for LAN sites or a trusted proxy. Public-only is direct.
    bool allow_private_network = false;
};

struct PageLink {
    std::size_t id = 0;
    std::string text;
    std::string url;
    std::string kind;  // link, frame or image; subresources are never loaded implicitly.
    std::string rel;
    bool external = false;
};

// Disjoint byte intervals in the retained text, including deletion markers.
struct DeletedRange { std::size_t begin = 0, end = 0; };

struct Page {
    std::uint64_t id = 0;
    std::string requested_url;
    std::string url;
    std::string title;
    std::string content_type;
    std::string status;
    std::string message;
    long http_status = 0;
    int transport_code = -1;
    std::int64_t elapsed_ms = 0;
    std::string text;
    std::vector<PageLink> links;
    std::string fragment;
    bool fragment_found = false;
    bool text_truncated = false;
    bool links_truncated = false;
    std::vector<std::string> redirects;
    NetworkTiming network;
    bool fetch_complete = true;
    bool retained_complete = true;
    bool return_complete = true;
    std::vector<std::string> incomplete_reasons;
    std::size_t total_text_bytes = 0, total_links = 0;
    std::size_t next_offset_bytes = 0, next_link_offset = 0;
    std::size_t link_bytes = 0;
    std::string static_redirect;
    std::string repository_commit;
    std::vector<DeletedRange> deleted_ranges;
    bool starts_deleted = false, ends_deleted = false;
    bool cache_retained = false;
    std::vector<std::uint64_t> evicted_page_ids;
    std::string resource_type = "page"; // image: opaque bytes, no decoding/OCR/script execution.
    std::string image_mime_type;
    std::string image_data; // Raw bytes in C++; JSON uses a typed Base64 image object.
    std::size_t total_image_bytes = 0, image_offset_bytes = 0;
    std::size_t network_attempt_count = 0;
    bool resolver_fallback_used = false;
    std::string failure_stage;
    std::vector<EngineAttempt> network_attempts; // Last hop, at most two. variant=primary/alternate; counter covers all hops/API calls.

};

struct PageText {
    std::uint64_t page_id = 0;
    std::size_t start_line = 1;
    std::size_t end_line = 0;
    std::size_t total_lines = 0;
    std::string text;
    bool more = false;
    bool source_truncated = false;
    std::size_t offset_bytes = 0, next_offset_bytes = 0;
    bool starts_deleted = false, ends_deleted = false, deletion_markers = false;
};

struct PageMatch {
    std::size_t line = 0;
    std::string text;
    std::size_t offset_bytes = 0;
    bool text_truncated = false;
    bool starts_deleted = false, ends_deleted = false;
};

// Stateful, bounded text browser. One caller at a time per instance.
// Page and link IDs are scoped to this instance. Evicted IDs report an error.
class Browser {
public:
    explicit Browser(BrowserOptions options = {});
    ~Browser();
    Browser(Browser&&) noexcept;
    Browser& operator=(Browser&&) noexcept;
    Browser(const Browser&) = delete;
    Browser& operator=(const Browser&) = delete;
    Response search(const std::string& query);
    Response search(const std::string& query, bool full, long timeout_ms = 0, std::size_t per_engine = 0);
    Page open(const std::string& url, long timeout_ms = 0);
    Page click(std::uint64_t page_id, std::size_t link_id, long timeout_ms = 0);
    const Page& page(std::uint64_t page_id = 0) const;
    PageText read(std::uint64_t page_id = 0, std::size_t start_line = 1,
                  std::size_t line_count = 80) const;
    std::vector<PageMatch> find(std::uint64_t page_id, const std::string& text,
                                std::size_t max_matches = 20, std::size_t start_line = 1, bool full_lines = false) const;
    PageText read_bytes(std::uint64_t page_id, std::size_t offset_bytes = 0,
                        std::size_t max_bytes = 16384) const;
    std::size_t line_offset(std::uint64_t page_id, std::size_t line = 1) const;
    Page back();
    void clear_session();
private:
    Page open_impl(const std::string& url, long timeout_ms, const std::string& image_source, bool image_request = false);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Offline HTML/text extraction, no requests or script execution.
Page extract_page(const std::string& source, const std::string& url,
                  const std::string& content_type = "text/html",
                  const BrowserOptions& options = {});
std::string to_json(const Page& page);
std::string to_json(const PageText& text);

}  // namespace edgesearch
