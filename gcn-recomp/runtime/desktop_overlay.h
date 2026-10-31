#pragma once
#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

// Separate from presentation.cfg so older versions keep their display settings.
namespace desktop_overlay {
inline std::atomic<bool> visible{false};
inline std::string path;
inline bool current(){return visible.load();}
inline void preview(bool value){visible.store(value);}
inline bool load(const std::string& file,std::string& error){
    path=file;preview(false);error.clear();std::error_code ec;
    if(!std::filesystem::exists(path,ec)){if(ec)error="Cannot read overlay settings.";return !ec;}
    if(std::filesystem::file_size(path,ec)>1024 || ec){error="Cannot read overlay settings.";return false;}
    std::ifstream in(path);std::string tag,extra;int version=0,value=-1;
    if(!(in>>tag>>version>>value) || (in>>extra) || tag!="GCN_OVERLAY" || version!=1 || value<0 || value>1){error="Invalid overlay settings. Counter is off.";return false;}
    preview(value!=0);return true;
}
inline bool save(std::string& error){
    error.clear();if(path.empty())return true;
    const std::filesystem::path target(path),temporary(path+".tmp");std::error_code ec;
    if(!target.parent_path().empty())std::filesystem::create_directories(target.parent_path(),ec);
    if(ec){error="Cannot create overlay settings folder.";return false;}
    std::ofstream out(temporary,std::ios::binary|std::ios::trunc);out<<"GCN_OVERLAY 1\n"<<(current()?1:0)<<'\n';out.flush();bool ok=bool(out);out.close();ok=ok && !out.fail();
    if(!ok){error="Cannot save overlay settings.";return false;}
#ifdef _WIN32
    ok=MoveFileExW(temporary.wstring().c_str(),target.wstring().c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
    std::filesystem::rename(temporary,target,ec);ok=!ec;
#endif
    if(!ok)error="Cannot replace overlay settings. Previous settings preserved.";return ok;
}
}
