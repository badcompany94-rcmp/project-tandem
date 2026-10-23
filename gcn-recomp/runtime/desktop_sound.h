#pragma once
#include <string>
namespace desktop_sound {
struct Settings {int volume=100;bool muted=false;bool operator==(const Settings&)const=default;};
bool load(const std::string& file,int default_volume,std::string& error);
Settings current();
void preview(Settings);
bool save(std::string& error);
}
