#pragma once

// Native curl handles must never cross into a chatbot's stock libcurl instance.
#if EDGE_SEARCH_USE_IMPERSONATE
#include <edgesearch_curl_namespace.h>
#endif
#include <curl/curl.h>
