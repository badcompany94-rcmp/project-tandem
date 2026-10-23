#pragma once
#include "online_json.h"
#include <string>

namespace mkdd::eos {
struct Config {
    std::string product_id,sandbox_id,deployment_id,client_id,client_secret;
    std::string name="Project Tandem",version="0.1",cache;
    static Config load(const std::string& path);
};
}
