#pragma once
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace game_image {
namespace fs=std::filesystem;
struct Search {std::string image;std::vector<std::string> checked;};
inline Search resolve(const std::string& configured,const fs::path& executable,const fs::path& cwd,const std::string& requested={}) {
    Search result;
    auto check=[&](fs::path path) {
        if(path.empty())return false;
        if(path.is_relative())path=cwd/path;
        path=path.lexically_normal();const auto name=path.string();
        if(std::find(result.checked.begin(),result.checked.end(),name)!=result.checked.end())return false;
        result.checked.push_back(name);std::error_code ec;
        if(!fs::is_regular_file(path,ec) || ec || !std::ifstream(path,std::ios::binary).good())return false;
        result.image=name;return true;
    };
    // An explicit selection must fail honestly, never boot a different image.
    if(!requested.empty()){check(requested);return result;}
    std::vector<fs::path> roots;
    auto root=executable.parent_path();
    for(int i=0;i<3 && !root.empty() && root!=root.root_path();++i) {
        roots.push_back(root);
        std::error_code ec;if(fs::is_regular_file(root/"package-manifest.json",ec))break;
        const auto parent=root.parent_path();if(parent==root)break;root=parent;
    }
    // Portable cached image follows the installation even when its build-time
    // Windows path is no longer meaningful (Wine, relocation, another PC).
    for(const auto& base:roots)for(const char* name:{"local-data/game-image.iso","local-data/game-image.ciso"})if(check(base/name))return result;
    if(check(configured))return result;
    auto scan=[&](const fs::path& directory) {
        std::error_code ec;std::vector<fs::path> files;
        for(fs::directory_iterator it(directory,ec),end;!ec && it!=end;it.increment(ec)) {
            auto ext=it->path().extension().string();for(char& c:ext)c=char(std::tolower(static_cast<unsigned char>(c)));
            if(ext==".iso" || ext==".ciso")files.push_back(it->path());
        }
        std::sort(files.begin(),files.end());for(const auto& file:files)if(check(file))return true;
        result.checked.push_back((directory/"*.iso or *.ciso").string());return false;
    };
    for(const auto& base:roots)if(scan(base/"rom"))return result;
    scan(cwd/"rom");return result;
}
}
