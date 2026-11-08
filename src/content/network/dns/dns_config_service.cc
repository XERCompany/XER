/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "content/network/dns/dns_config_service.h"

#include <fstream>
#include <sstream>

#include "helper/string_utils.h"

namespace XER::Network::Dns {

bool DnsConfigService::ReadConfig(DnsConfig* config) const {
    std::ifstream file("/etc/resolv.conf");
    if (!file.is_open()) return false;

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        std::istringstream iss(line);
        std::string keyword;
        iss >> keyword;
        if (keyword == "nameserver") {
            std::string ns;
            if (iss >> ns) config->nameservers.push_back(ns);
        } else if (keyword == "search" || keyword == "domain") {
            std::string domain;
            while (iss >> domain) config->search.push_back(domain);
        } else if (keyword == "options") {
            std::string opt;
            while (iss >> opt) {
                if (opt.rfind("timeout:", 0) == 0)
                    config->timeout_ms = std::stoul(opt.substr(8)) * 1000;
                else if (opt.rfind("ndots:", 0) == 0)
                    config->ndots = std::stoi(opt.substr(6));
                else if (opt == "rotate")
                    config->rotate = true;
                else if (opt == "use-vc")
                    config->use_tcp = true;
            }
        }
    }
    if (config->nameservers.empty()) {
        config->nameservers = {"8.8.8.8", "8.8.4.4"};
    }
    return true;
}

// static
DnsConfig DnsConfigService::DefaultConfig() {
    DnsConfig cfg;
    cfg.nameservers = {"8.8.8.8", "8.8.4.4"};
    return cfg;
}

}  // namespace XER::Network::Dns
