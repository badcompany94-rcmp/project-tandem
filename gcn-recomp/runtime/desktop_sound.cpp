#include "desktop_sound.h"
#include "audio_gain.h"
#include <filesystem>
#include <fstream>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif
namespace desktop_sound {
namespace {Settings active;std::string path;}
Settings current(){return active;}
void preview(Settings settings){settings.volume=std::clamp(settings.volume,0,100);active=settings;audio_gain::set(active.muted?0:active.volume);}
bool load(const std::string& file,int default_volume,std::string& error) {
    path=file;error.clear();preview({default_volume,false});std::error_code ec;
    if(!std::filesystem::exists(path,ec) && !ec)return true;
    if(ec || std::filesystem::file_size(path,ec)>1024 || ec){error="Sound settings are unreadable. Using the default volume.";return false;}
    std::ifstream in(path);std::string tag,extra;int version,volume,muted;
    if(!(in>>tag>>version>>volume>>muted) || (in>>extra) || tag!="GCN_SOUND" || version!=1 || volume<0 || volume>100 || muted<0 || muted>1) {
        error="Invalid sound settings. Using the default volume.";return false;
    }
    preview({volume,muted!=0});return true;
}
bool save(std::string& error) {
    error.clear();if(path.empty()){error="Sound settings are not initialized.";return false;}
    const std::filesystem::path target(path),temporary=path+".tmp";std::error_code ec;
    if(!target.parent_path().empty())std::filesystem::create_directories(target.parent_path(),ec);
    if(ec){error="Cannot create the sound settings folder.";return false;}
    std::ofstream out(temporary,std::ios::binary|std::ios::trunc);
    out<<"GCN_SOUND 1\n"<<active.volume<<' '<<(active.muted?1:0)<<'\n';out.flush();bool ok=(bool)out;out.close();ok=ok && !out.fail();
    if(!ok){error="Cannot save sound settings. Check the folder is writable.";return false;}
#ifdef _WIN32
    ok=MoveFileExW(temporary.wstring().c_str(),target.wstring().c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
    std::filesystem::rename(temporary,target,ec);ok=!ec;
#endif
    if(!ok){error="Cannot replace sound settings. Previous settings were preserved.";return false;}
    return true;
}
}
