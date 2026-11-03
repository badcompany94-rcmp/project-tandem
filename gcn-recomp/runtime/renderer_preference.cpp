#include "renderer_preference.h"
#include <array>
#include <filesystem>
#include <fstream>
#include <sstream>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif
namespace desktop_renderer {
const char* label(Api api){return api==Api::Direct3D11?"Direct3D 11 (experimental)":"OpenGL";}
bool read_preference(const std::string& path,Api& result,std::string& error){
    result=Api::OpenGL;error.clear();try{
        const auto file=std::filesystem::u8path(path);std::error_code ec;
        const bool exists=std::filesystem::exists(file,ec);if(ec){error="Cannot read renderer settings. Using OpenGL.";return false;}if(!exists)return true;
        std::ifstream in(file,std::ios::binary);if(!in){error="Cannot read renderer settings. Using OpenGL.";return false;}
        std::array<char,1025> bytes{};in.read(bytes.data(),bytes.size());
        if(in.bad() || in.gcount()>1024){error="Renderer settings are unreadable or too large. Using OpenGL.";return false;}
        std::istringstream text(std::string(bytes.data(),size_t(in.gcount())));std::string tag,extra;int version=0,choice=-1;
        if(!(text>>tag>>version>>choice) || (text>>extra) || tag!="GCN_RENDERER" || version!=1 || choice<0 || choice>1){error="Invalid renderer settings. Using OpenGL.";return false;}
        result=Api(choice);return true;
    }catch(const std::exception&){error="Cannot read renderer settings. Using OpenGL.";return false;}
}
bool write_preference(const std::string& path,Api choice,std::string& error){
    error.clear();if(path.empty() || (choice!=Api::OpenGL && choice!=Api::Direct3D11)){error="Invalid renderer choice or settings path.";return false;}
    try{
        const auto target=std::filesystem::u8path(path);auto temporary=target;temporary+=".tmp";std::error_code ec;
        if(!target.parent_path().empty())std::filesystem::create_directories(target.parent_path(),ec);
        if(ec){error="Cannot create the renderer settings folder.";return false;}
        std::ofstream out(temporary,std::ios::binary|std::ios::trunc);out<<"GCN_RENDERER 1\n"<<int(choice)<<'\n';out.flush();bool ok=bool(out);out.close();ok=ok && !out.fail();
        if(!ok){error="Cannot save renderer settings. The previous choice was preserved.";return false;}
#ifdef _WIN32
        ok=MoveFileExW(temporary.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
        std::filesystem::rename(temporary,target,ec);ok=!ec;
#endif
        if(!ok){error="Cannot replace renderer settings. The previous choice was preserved.";return false;}return true;
    }catch(const std::exception&){error="Cannot save renderer settings. The previous choice was preserved.";return false;}
}
}
