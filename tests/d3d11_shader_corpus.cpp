// Developer-only compiler/interface fixture. Read a locally generated shader
// cache; never package that cache or treat compilation as game-rendering parity.
// "points" opts into explicit native interfaces; "hash" preserves GL baselines.
#include "gx/shadergen.h"
#include <glslang/Public/ShaderLang.h>
#include <glslang/Public/ResourceLimits.h>
#include <SPIRV/GlslangToSpv.h>
#include <spirv_hlsl.hpp>
#include <d3dcompiler.h>
#include <d3d11shader.h>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <vector>
#include <cstring>
#include <iterator>
#include <chrono>
#include <map>
namespace {
void check(bool ok,const std::string& error){if(!ok)throw std::runtime_error(error);}
std::string modern(std::string source){
    const auto newline=source.find('\n');check(newline!=std::string::npos,"missing shader version");
    return "#version 450 core\n"+source.substr(newline+1);
}
}
int main(int argc,char** argv)try{
    check(glslang::InitializeProcess(),"glslang initialization");
    gx::ShaderKey key{};key.alpha_func=0xff;key.efb_has_alpha=1;
    std::vector<gx::ShaderKey> keys{key};
    if(argc>=2){
        std::ifstream in(argv[1],std::ios::binary);check(bool(in),"open read-only shader corpus");
        std::vector<char> data((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>());
        check(data.size()>=16 && data.size()<16*1024*1024,"invalid shader corpus size");
        uint32_t magic=0,format=0;std::memcpy(&magic,data.data(),4);std::memcpy(&format,data.data()+4,4);
        check(magic==0x43535257 && format==((3u<<16)|sizeof(gx::ShaderKey)),"incompatible shader corpus");
        size_t at=16;
        while(at<data.size()){
            check(data.size()-at>=sizeof(key)+16,"truncated shader corpus");
            std::memcpy(&key,data.data()+at,sizeof(key));at+=sizeof(key)+12;
            uint32_t bytes=0;std::memcpy(&bytes,data.data()+at,4);at+=4;
            check(bytes<=data.size()-at,"truncated shader corpus binary");at+=bytes;keys.push_back(key);
        }
    }
    const auto began=std::chrono::steady_clock::now();size_t binaries=0;
    for(size_t index=0;index<keys.size();++index){
    key=keys[index];
    if(argc>=3 && std::string(argv[2])=="hash"){
        auto hash=[](const std::string& text){uint64_t h=1469598103934665603ull;for(unsigned char c:text)h=(h^c)*1099511628211ull;return h;};
        std::printf("%zu %016llx %016llx\n",index,(unsigned long long)hash(gx::gen_vertex_shader()),(unsigned long long)hash(gx::gen_pixel_shader(key)));continue;
    }
    const bool explicit_points=argc>=3 && std::string(argv[2])=="points";
    std::string sources[]={modern(gx::gen_vertex_shader(explicit_points)),modern(gx::gen_pixel_shader(key,explicit_points))};
    glslang::TShader vs(EShLangVertex),ps(EShLangFragment);
    glslang::TProgram program;glslang::TShader* stages[]={&vs,&ps};
    const auto messages=EShMessages(EShMsgSpvRules|EShMsgVulkanRules);
    for(unsigned i=0;i<2;++i){
        auto& shader=*stages[i];const char* source=sources[i].c_str();shader.setStrings(&source,1);
        shader.setEnvInput(glslang::EShSourceGlsl,i?EShLangFragment:EShLangVertex,glslang::EShClientVulkan,450);
        shader.setEnvClient(glslang::EShClientVulkan,glslang::EShTargetVulkan_1_0);
        shader.setEnvTarget(glslang::EShTargetSpv,glslang::EShTargetSpv_1_0);
        shader.setEnvInputVulkanRulesRelaxed();shader.setAutoMapBindings(true);shader.setAutoMapLocations(true);
        shader.setGlobalUniformBlockName("TandemUniforms");shader.setGlobalUniformSet(0);shader.setGlobalUniformBinding(0);
        check(shader.parse(GetDefaultResources(),450,false,messages),shader.getInfoLog());program.addShader(&shader);
    }
    check(program.link(messages),program.getInfoLog());check(program.mapIO(),program.getInfoLog());
    std::map<std::pair<std::string,unsigned>,D3D11_SIGNATURE_PARAMETER_DESC> vertex_outputs;
    std::map<std::pair<std::string,unsigned>,unsigned> vertex_masks;
    for(unsigned i=0;i<2;++i){
        std::vector<unsigned> spirv;glslang::SpvOptions options;options.disableOptimizer=true;
        glslang::GlslangToSpv(*program.getIntermediate(i?EShLangFragment:EShLangVertex),spirv,&options);
        spirv_cross::CompilerHLSL compiler(std::move(spirv));
        const auto resources=compiler.get_shader_resources();
        for(const auto& resource:resources.sampled_images){
            if(resource.name=="u_tex")compiler.set_decoration(resource.id,spv::DecorationBinding,0);
            if(resource.name=="u_pal")compiler.set_decoration(resource.id,spv::DecorationBinding,8);
        }
        auto hlsl=compiler.get_hlsl_options();hlsl.shader_model=50;hlsl.point_size_compat=true;hlsl.point_coord_compat=!explicit_points;compiler.set_hlsl_options(hlsl);
        auto common=compiler.get_common_options();common.vertex.fixup_clipspace=true;compiler.set_common_options(common);
        const auto code=compiler.compile();std::ofstream(i?"pixel.hlsl":"vertex.hlsl")<<code;
        ID3DBlob* binary=nullptr;ID3DBlob* error=nullptr;
        const auto result=D3DCompile(code.c_str(),code.size(),"Tandem GX probe",nullptr,nullptr,"main",i?"ps_5_0":"vs_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&binary,&error);
        if(FAILED(result)){const std::string log=error?std::string(static_cast<const char*>(error->GetBufferPointer()),error->GetBufferSize()):"D3DCompile failed";if(error)error->Release();throw std::runtime_error(log);}
        ID3D11ShaderReflection* reflection=nullptr;
        check(SUCCEEDED(D3DReflect(binary->GetBufferPointer(),binary->GetBufferSize(),IID_ID3D11ShaderReflection,reinterpret_cast<void**>(&reflection))),"reflect DXBC");
        D3D11_SHADER_DESC description{};reflection->GetDesc(&description);
        if(!i){
            for(unsigned n=0;n<description.OutputParameters;++n){D3D11_SIGNATURE_PARAMETER_DESC parameter{};reflection->GetOutputParameterDesc(n,&parameter);
                const auto key=std::make_pair(std::string(parameter.SemanticName),parameter.SemanticIndex);
                vertex_masks[key]=parameter.Mask;parameter.SemanticName=nullptr;vertex_outputs[key]=parameter;}
        }else{
            for(unsigned n=0;n<description.InputParameters;++n){D3D11_SIGNATURE_PARAMETER_DESC parameter{};reflection->GetInputParameterDesc(n,&parameter);
                if(parameter.SystemValueType!=D3D_NAME_UNDEFINED || !parameter.ReadWriteMask)continue;
                const auto semantic=std::make_pair(std::string(parameter.SemanticName),parameter.SemanticIndex);
                if(vertex_outputs.count(semantic) && ((vertex_masks.at(semantic)&parameter.Mask)!=parameter.Mask || vertex_outputs.at(semantic).ComponentType!=parameter.ComponentType))
                    std::fprintf(stderr,"Interface detail VS mask=%u type=%u PS mask=%u used=%u type=%u\n",vertex_masks.at(semantic),vertex_outputs.at(semantic).ComponentType,parameter.Mask,parameter.ReadWriteMask,parameter.ComponentType);
                check(vertex_outputs.count(semantic) && (vertex_masks.at(semantic)&parameter.Mask)==parameter.Mask && vertex_outputs.at(semantic).ComponentType==parameter.ComponentType,
                    "DXBC stage interface mismatch at key"+std::to_string(index)+" "+semantic.first+std::to_string(semantic.second));}
        }
        reflection->Release();
        if(error)error->Release();binaries+=binary->GetBufferSize();binary->Release();
    }
    if(index%50==0)std::printf("Compiled GX key %zu/%zu\n",index+1,keys.size());
    }
    glslang::FinalizeProcess();
    if(argc>=3 && std::string(argv[2])=="hash")std::printf("PASS %zu original OpenGL source hashes; no bytecode compilation in hash mode\n",keys.size());
    else std::printf("PASS %zu actual GX shader pairs -> SPIR-V -> HLSL -> Direct3D bytecode, %zu bytes, %.3f seconds CPU/compiler work\n",keys.size(),binaries,std::chrono::duration<double>(std::chrono::steady_clock::now()-began).count());
    std::puts("Compiler feasibility only; point expansion/coordinate semantics and game equivalence are not validated.");
    return 0;
}catch(const std::exception& error){std::fprintf(stderr,"FAIL %s\n",error.what());return 1;}
