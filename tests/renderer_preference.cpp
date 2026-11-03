#include "renderer_preference.h"
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <stdexcept>
namespace {
void require(bool value,const std::string& error){if(!value)throw std::runtime_error(error);}
std::string read(const std::filesystem::path& path){std::ifstream file(path,std::ios::binary);return {std::istreambuf_iterator<char>(file),{}};}
void write(const std::filesystem::path& path,const std::string& text){std::ofstream file(path,std::ios::binary|std::ios::trunc);file<<text;require(bool(file),"Write scratch preference");}
}
int main(int argc,char** argv)try{
    require(argc==2,"Pass an empty scratch folder");const auto root=std::filesystem::u8path(argv[1]);std::filesystem::create_directories(root);
    const auto file=root/std::filesystem::u8path("renderer-\xc3\xa9.cfg");const auto u8=file.u8string();const std::string path(reinterpret_cast<const char*>(u8.data()),u8.size());
    require(!std::filesystem::exists(file),"Fixture scratch preference already exists");using desktop_renderer::Api;Api choice=Api::Direct3D11;std::string error;
    require(desktop_renderer::read_preference(path,choice,error) && choice==Api::OpenGL && !std::filesystem::exists(file),"Missing preference did not default to OpenGL without writing");
    for(auto api:{Api::Direct3D11,Api::OpenGL}){require(desktop_renderer::write_preference(path,api,error),error);choice=Api::Direct3D11;
        require(desktop_renderer::read_preference(path,choice,error) && choice==api,error);}
    const auto before=read(file);require(!desktop_renderer::write_preference(path,Api(2),error) && read(file)==before,"Invalid choice modified the working preference");
    for(const auto& text:{std::string("GCN_RENDERER 2\n1\n"),std::string("GCN_RENDERER 1\n2\n"),std::string("GCN_RENDERER 1\n1 trailing\n"),std::string("garbage"),std::string(1025,'x')}){
        write(file,text);choice=Api::Direct3D11;require(!desktop_renderer::read_preference(path,choice,error) && choice==Api::OpenGL && read(file)==text,"Corrupt preference did not fall back without modifying the file");}
    const auto blocked=root/"directory.cfg";std::filesystem::create_directories(blocked);write(blocked/"preserved.txt","preserved");
    const auto blocked_u8=blocked.u8string();require(!desktop_renderer::write_preference(std::string(reinterpret_cast<const char*>(blocked_u8.data()),blocked_u8.size()),Api::Direct3D11,error),"Directory preference target was replaced");
    require(read(blocked/"preserved.txt")=="preserved","Failed replace affected existing target contents");
    std::puts("PASS renderer preference: OpenGL default, both choices, UTF-8 path, bounded parsing and failed-write preservation");return 0;
}catch(const std::exception& failure){std::fprintf(stderr,"FAIL %s\n",failure.what());return 1;}
