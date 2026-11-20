#include "internal.hpp"

#include <stdexcept>

namespace edgesearch::detail {

const std::vector<Engine>& registry() {
    static const std::vector<Engine> engines = {
        {"baidu", "baidu", "https://www.baidu.com/s",
         "//div[@id='content_left']/div[.//h3]",
         "(.//h3//a[@href])[1]", "(.//h3)[1]",
         ".//*[@data-module='abstract'] | .//*[contains(@class,'c-abstract') or contains(@class,'content-right') or contains(@class,'c-span-last')]"},
        {"bing_cn", "bing", "https://cn.bing.com/search",
         "//li[contains(concat(' ',normalize-space(@class),' '),' b_algo ')]",
         "(.//h2/a[@href] | .//a[@href][h2])[1]", "(.//h2)[1]", ".//p"},
        {"duckduckgo", "bing", "https://html.duckduckgo.com/html/",
         "//div[contains(concat(' ',normalize-space(@class),' '),' web-result ')] | //div[contains(concat(' ',normalize-space(@class),' '),' body ')][.//h2]",
         "(.//a[contains(@class,'result__a')] | ./a[@href])[1]", "(.//h2)[1]",
         ".//*[contains(@class,'result__snippet')] | ./a"},
        {"brave", "brave", "https://search.brave.com/search",
         "//div[@data-type='web']", "(.//a[@href][.//*[contains(@class,'title')]])[1]",
         "(.//*[contains(@class,'snippet-title') or contains(@class,'title')])[last()]",
         ".//div[contains(@class,'generic-snippet')] | .//div[contains(@class,'content')]"},
        {"yahoo", "bing", "https://search.yahoo.com/search",
         "//div[contains(@class,'relsrch')] | //div[contains(@class,'algo-sr')]",
         "(.//h3//a[@href] | .//*[contains(@class,'Title')]//a[@href] | .//a[@href])[1]",
         "(.//h3)[1]", ".//*[contains(@class,'compText') or contains(@class,'Text')] | .//p"},
        {"so360", "so360", "https://www.so.com/s",
         "//li[contains(@class,'res-list')]", "(.//h3//a[@href])[1]", "(.//h3)[1]",
         ".//*[contains(@class,'res-desc') or contains(@class,'res-summary')] | .//p"},
        {"sogou", "sogou", "https://www.sogou.com/web",
         "//div[contains(concat(' ',normalize-space(@class),' '),' vrwrap ') or contains(concat(' ',normalize-space(@class),' '),' rb ')]",
         "(.//h3//a[@href])[1]", "(.//h3//a[@href])[1]",
         ".//*[contains(@class,'str_info') or contains(@class,'str-text-info')] | .//p"},
    };
    return engines;
}

const Engine& find_engine(const std::string& name) {
    for (const auto& engine : registry()) {
        if (engine.name == name) return engine;
    }
    throw std::invalid_argument("unknown engine: " + name);
}

Request make_request(const Engine& engine, const std::string& query, const Options& options) {
    const std::string name = engine.name;
    const auto q = encode(query);
    Request request;
    request.session_key = engine.name;
    request.url = engine.endpoint;
    const bool chinese = options.region == "cn-zh";
    request.headers = {"Accept: text/html,application/xhtml+xml", chinese
        ? "Accept-Language: zh-CN,zh;q=0.9,en;q=0.7" : "Accept-Language: en-US,en;q=0.9"};
#if EDGE_SEARCH_USE_IMPERSONATE
    request.headers.erase(request.headers.begin());  // Profile supplies coherent browser headers.
#endif
    if (name == "baidu") request.url += "?wd=" + q + "&rn=" + std::to_string(options.per_engine) + "&ie=utf-8";
    else if (name == "bing_cn") {
        request.variant = options.bing_format == "html" ? "html" : "rss";
        request.url += "?q=" + q + "&count=" + std::to_string(options.per_engine) + "&setmkt=" + (chinese ? "zh-CN" : "en-US");
        if (request.variant == "rss") request.url += "&format=rss";
    }
    else if (name == "duckduckgo") {
        request.post_data = "q=" + q + "&b=&kl=" + encode(options.region);
        request.headers.push_back("Content-Type: application/x-www-form-urlencoded");
    } else if (name == "brave") request.url += "?q=" + q + "&source=web";
    else if (name == "yahoo") request.url += "?p=" + q + "&n=" + std::to_string(options.per_engine);
    else if (name == "so360") request.url += "?q=" + q + "&ie=utf-8";
    else if (name == "sogou") request.url += "?query=" + q + "&ie=utf8";
    return request;
}

}  // namespace edgesearch::detail

namespace edgesearch {

std::vector<EngineInfo> available_engines() {
    std::vector<EngineInfo> engines;
    for (const auto& engine : detail::registry()) engines.push_back({engine.name, engine.provider});
    return engines;
}

std::vector<std::string> profile_engines(const std::string& profile) {
    if (profile == "domestic") return {"bing_cn", "so360", "sogou", "baidu"};
    if (profile == "global") return {"duckduckgo", "brave", "yahoo"};
    if (profile == "all") {
        std::vector<std::string> names;
        for (const auto& engine : detail::registry()) names.emplace_back(engine.name);
        return names;
    }
    throw std::invalid_argument("unknown profile: " + profile);
}

}  // namespace edgesearch
