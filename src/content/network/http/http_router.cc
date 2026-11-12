/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "content/network/http/http_router.h"

namespace XER::Network::Http {

namespace {

std::vector<std::string_view> SplitPath(std::string_view path) {
    std::vector<std::string_view> out;
    size_t pos = 0;
    while (pos <= path.size()) {
        const size_t slash = path.find('/', pos);
        const std::string_view seg =
            path.substr(pos, slash == std::string_view::npos
                                ? std::string_view::npos : slash - pos);
        if (!seg.empty()) out.push_back(seg);
        if (slash == std::string_view::npos) break;
        pos = slash + 1;
    }
    return out;
}

}  // namespace

void HttpRouter::AddRoute(HttpMethod method, std::string pattern,
                          HttpHandler handler) {
    Route route;
    route.method = method;
    route.segments = SplitPattern(pattern);
    route.handler = std::move(handler);
    routes_.push_back(std::move(route));
}

void HttpRouter::AddRoute(std::string_view method_name, std::string pattern,
                          HttpHandler handler) {
    AddRoute(HttpMethodFromString(method_name), std::move(pattern),
             std::move(handler));
}

std::vector<HttpRouter::Segment> HttpRouter::SplitPattern(
    std::string_view pattern) {
    std::vector<Segment> out;
    for (const std::string_view seg : SplitPath(pattern)) {
        Segment s;
        if (seg.size() >= 3 && seg.front() == '{' && seg.back() == '}') {
            s.is_param = true;
            s.text = std::string(seg.substr(1, seg.size() - 2));
        } else {
            s.text = std::string(seg);
        }
        out.push_back(std::move(s));
    }
    return out;
}

HttpResponse HttpRouter::Dispatch(const HttpRequest& request) const {
    const std::vector<std::string_view> path_segs = SplitPath(request.path);
    bool path_matched = false;

    for (const Route& route : routes_) {
        if (route.segments.size() != path_segs.size()) continue;

        std::map<std::string, std::string> params;
        bool match = true;
        for (size_t i = 0; i < route.segments.size() && match; ++i) {
            const Segment& seg = route.segments[i];
            if (seg.is_param) {
                params[seg.text] = std::string(path_segs[i]);
            } else if (seg.text != path_segs[i]) {
                match = false;
            }
        }
        if (!match) continue;

        path_matched = true;
        const bool method_ok =
            route.method == HttpMethod::kUnknown ||
            request.method == HttpMethod::kUnknown ||
            route.method == request.method;
        if (!method_ok) continue;

        return route.handler(request, params);
    }

    if (path_matched) {
        HttpResponse r = HttpResponse::JsonError(405, "method not allowed");
        r.headers.Set("Allow", "GET, POST, PUT, DELETE, PATCH, HEAD, OPTIONS");
        return r;
    }
    return HttpResponse::JsonError(404, "no route for " + request.path);
}

}  // namespace XER::Network::Http
