#include "graphics/d3d11_shaders.h"
#include "gx/shadergen.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <set>
#include <stdexcept>

namespace {
void require(bool ok,const std::string& error){if(!ok)throw std::runtime_error(error);}
}
int main(int argc,char** argv)try{
    std::vector<gx::ShaderKey> keys(1);keys[0].alpha_func=0xff;keys[0].efb_has_alpha=1;
    if(argc==2){
        std::ifstream input(argv[1],std::ios::binary);require(bool(input),"Cannot read local shader cache");
        std::vector<char> data((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());
        require(data.size()>=16 && data.size()<=16*1024*1024,"Invalid shader cache size");
        uint32_t magic=0,format=0;std::memcpy(&magic,data.data(),4);std::memcpy(&format,data.data()+4,4);
        require(magic==0x43535257 && format==((3u<<16)|sizeof(gx::ShaderKey)),"Unsupported local shader cache");
        size_t at=16;
        while(at<data.size()){
            gx::ShaderKey key{};require(data.size()-at>=sizeof(key)+16,"Truncated shader key");
            std::memcpy(&key,data.data()+at,sizeof(key));at+=sizeof(key)+12;
            uint32_t bytes=0;std::memcpy(&bytes,data.data()+at,4);at+=4;
            require(bytes<=data.size()-at,"Truncated shader binary");at+=bytes;keys.push_back(key);
        }
    }
    size_t total_bytes=0;
    for(size_t index=0;index<keys.size();++index){
        desktop_d3d11::CompiledProgram program;std::string error;
        require(desktop_d3d11::compile_gx_program(keys[index],program,error),"Key"+std::to_string(index)+": "+error);
        require(!program.vertex.empty() && !program.pixel.empty(),"Empty native bytecode");
        require(program.uniform_bytes && program.uniform_bytes%16==0 && program.uniform_bytes<=65536,"Invalid native constant buffer");
        std::set<std::string> names;
        for(const auto& uniform:program.uniforms){
            require(names.insert(uniform.name).second,"Duplicate native uniform");
            require(uniform.offset+uniform.size<=program.uniform_bytes,"Native uniform exceeds buffer");
            if(uniform.name=="u_proj")require(uniform.columns==4 && uniform.components==4 && uniform.matrix_stride==16,"Invalid projection matrix layout");
            if(uniform.name=="u_reg")require(uniform.array_count==4 && uniform.array_stride==16 && uniform.scalar==desktop_d3d11::UniformScalar::Signed,"Invalid TEV register layout");
            if(uniform.name=="u_point_off")require(uniform.array_count==8 && uniform.array_stride==16,"Invalid scalar-array layout");
        }
        require(names.count("u_proj") && names.count("u_reg"),"Lost original uniform names");
        for(const auto& sampler:program.samplers){
            if(sampler.name=="u_tex")require(sampler.binding==0 && sampler.count==8,"Invalid GX texture binding");
            if(sampler.name=="u_pal")require(sampler.binding==8 && sampler.count==8,"Invalid GX palette binding");
        }
        total_bytes+=program.vertex.size()+program.pixel.size();
        if(index==0){
            const auto saved=program.vertex;const auto saved_pixel=program.pixel;
            require(!desktop_d3d11::compile_program("#version 450\nvoid bad(","#version 450\nvoid bad(",program,error),"Malformed shader accepted");
            require(!error.empty() && program.vertex==saved && program.pixel==saved_pixel,"Compile failure replaced a valid program or lost diagnostics");
            auto bad=keys[index];bad.num_texgens=9;
            require(!desktop_d3d11::compile_gx_program(bad,program,error) && !error.empty() && program.vertex==saved,"Unsupported GX configuration replaced a valid program");
            desktop_d3d11::CompiledProgram flipped;
            require(desktop_d3d11::compile_gx_program(keys[index],flipped,error,{true}),error);
            require(flipped.vertex!=saved && flipped.pixel==saved_pixel && flipped.uniforms==program.uniforms,"Target orientation changed unrelated shader state");
        }
    }
    std::printf("PASS native shader library: %zu GX pairs, %zu bytecode bytes, uniform/sampler layouts, live stage signatures and atomic failure preservation\n",keys.size(),total_bytes);
    std::puts("No game/GPU execution, coordinate parity or mixed-renderer online acceptance claimed.");return 0;
}catch(const std::exception& error){std::fprintf(stderr,"FAIL %s\n",error.what());return 1;}
