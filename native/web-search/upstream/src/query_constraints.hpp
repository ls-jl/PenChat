#pragma once
#include <string>
namespace edgesearch::detail {
struct QueryConstraints {
    std::string evidence; // Same byte offsets/case as input; constraint syntax is blanked.
    bool rejected = false, uncertain = false, complex_logic = false, verified = false;
    std::string reason;
};
QueryConstraints query_constraints(const std::string& query, const std::string& url);
}
