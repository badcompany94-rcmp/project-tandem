#include "eos_config.h"
#include <filesystem>
#include <fstream>
#include <iterator>

namespace mkdd::eos {
Config Config::load(const std::string& path) {
    std::ifstream file(std::filesystem::u8path(path),std::ios::binary);
    if(!file)throw std::runtime_error("Cannot open EOS configuration file");
    std::string data;
    char block[4096];
    while(file.read(block,sizeof(block)) || file.gcount()) {
        if(data.size()+file.gcount()>16384)throw std::runtime_error("EOS configuration is too large");
        data.append(block,(size_t)file.gcount());
    }
    if(file.bad())throw std::runtime_error("Cannot read EOS configuration file");
    if(data.rfind("\xef\xbb\xbf",0)==0)data.erase(0,3);
    const auto json=online::Json::parse(data);
    auto field=[&](const char* key,size_t maximum,bool opaque=false) {
        if(!json.has(key) || json.at(key).kind!=online::Json::Kind::String)
            throw std::runtime_error(std::string("EOS setting is missing: ")+key);
        std::string value=json.at(key).str();
        const bool bad=opaque?std::any_of(value.begin(),value.end(),[](unsigned char c){return c<33 || c>126;}):
            value.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_")!=std::string::npos;
        if(value.empty() || value.size()>maximum || bad)
            throw std::runtime_error(std::string("EOS setting is empty or invalid: ")+key);
        return value;
    };
    Config result;
    result.product_id=field("product_id",64);result.sandbox_id=field("sandbox_id",64);
    result.deployment_id=field("deployment_id",64);result.client_id=field("client_id",128);
    result.client_secret=field("client_secret",256,true);
    result.cache=(std::filesystem::absolute(std::filesystem::u8path(path)).parent_path()/"eos-cache").u8string();
    return result;
}
}
