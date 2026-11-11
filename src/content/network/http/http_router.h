/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "content/network/http/http_message.h"

namespace XER::Network::Http {

// Handler receives the parsed request plus path parameters extracted
// from {name} segments of the route pattern.
using HttpHandler = std::function<HttpResponse(
    const HttpRequest& request,
    const std::map<std::string, std::string>& path_params)>;

// Path router supporting exact segments and {param} placeholders.
// Patterns look like "/api/archive/{name}/entries".
// A route registered with HttpMethod::kUnknown matches every method.
class HttpRouter {
public:
    void AddRoute(HttpMethod method, std::string pattern, HttpHandler handler);
    void AddRoute(std::string_view method_name, std::string pattern,
                  HttpHandler handler);

    // Returns the handler response, or a 404/405 error response.
    HttpResponse Dispatch(const HttpRequest& request) const;

    size_t RouteCount() const { return routes_.size(); }

private:
    struct Segment {
        bool is_param = false;
        std::string text;
    };
    struct Route {
        HttpMethod method = HttpMethod::kUnknown;
        std::vector<Segment> segments;
        HttpHandler handler;
    };

    static std::vector<Segment> SplitPattern(std::string_view pattern);

    std::vector<Route> routes_;
};

}  // namespace XER::Network::Http
