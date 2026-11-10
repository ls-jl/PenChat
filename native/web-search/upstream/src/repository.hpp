#pragma once
#include "edgesearch/browser.hpp"
#include "internal.hpp"
namespace edgesearch::detail {
using RepositoryFetch = std::function<HttpResponse(const std::string&)>;
// nullopt means an ordinary URL, which the browser should fetch normally.
std::optional<Page> repository_page(const std::string& url, const BrowserOptions& options,
                                    const RepositoryFetch& fetch);
}
