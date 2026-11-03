#include "d3d11_shaders.h"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3dcompiler.h>
#include <d3d11shader.h>
#include <glslang/Public/ShaderLang.h>
#include <glslang/Public/ResourceLimits.h>
#include <SPIRV/GlslangToSpv.h>
#include <spirv_hlsl.hpp>
#include "gx/shadergen.h"
#include <algorithm>
#include <cstring>
#include <map>
#include <stdexcept>

namespace desktop_d3d11 {
namespace {
template<class T>struct Com {
    T* value=nullptr;~Com(){if(value)value->Release();}
    T* operator->()const{return value;}T** out(){return &value;}
};
void require(bool ok,const std::string& error){if(!ok)throw std::runtime_error(error);}
struct CompilerLifetime {
    bool ready=glslang::InitializeProcess();
    ~CompilerLifetime(){if(ready)glslang::FinalizeProcess();}
};
std::string modern(const std::string& source){
    require(!source.empty() && source.size()<=1024*1024,"Invalid shader source size");
    const auto newline=source.find('\n');
    require(source.starts_with("#version ") && newline!=std::string::npos,"Missing shader language version");
    return "#version 450 core\n"+source.substr(newline+1);
}
UniformScalar scalar(spirv_cross::SPIRType::BaseType type){
    switch(type){
    case spirv_cross::SPIRType::Float:return UniformScalar::Float;
    case spirv_cross::SPIRType::Int:return UniformScalar::Signed;
    case spirv_cross::SPIRType::UInt:return UniformScalar::Unsigned;
    case spirv_cross::SPIRType::Boolean:return UniformScalar::Boolean;
    default:throw std::runtime_error("Unsupported native uniform scalar type");
    }
}
uint32_t array_count(const spirv_cross::SPIRType& type){
    uint64_t count=1;
    for(auto n:type.array){require(n>0 && count*n<=4096,"Unsupported native uniform array");count*=n;}
    return uint32_t(count);
}
void resources(spirv_cross::CompilerHLSL& compiler,CompiledProgram& output){
    const auto resources=compiler.get_shader_resources();
    require(resources.uniform_buffers.size()<=1,"Multiple native uniform blocks are unsupported");
    for(const auto& resource:resources.uniform_buffers){
        const auto& block=compiler.get_type(resource.base_type_id);
        const auto size=compiler.get_declared_struct_size(block);
        require(size<=65536,"Native uniform block exceeds Direct3D11 limit");
        output.uniform_bytes=std::max(output.uniform_bytes,uint32_t((size+15)&~size_t(15)));
        compiler.set_decoration(resource.id,spv::DecorationBinding,0);
        for(uint32_t i=0;i<block.member_types.size();++i){
            const auto& type=compiler.get_type(block.member_types[i]);Uniform value;
            require(type.width==32 || type.basetype==spirv_cross::SPIRType::Boolean,"Unsupported native uniform width");
            value.name=compiler.get_member_name(resource.base_type_id,i);
            value.offset=compiler.type_struct_member_offset(block,i);
            value.size=uint32_t(compiler.get_declared_struct_member_size(block,i));
            value.scalar=scalar(type.basetype);value.components=type.vecsize;value.columns=type.columns;
            value.array_count=array_count(type);
            if(!type.array.empty())value.array_stride=compiler.type_struct_member_array_stride(block,i);
            if(type.columns>1)value.matrix_stride=compiler.type_struct_member_matrix_stride(block,i);
            require(value.offset<=output.uniform_bytes && value.size<=output.uniform_bytes-value.offset,"Native uniform lies outside its buffer");
            const auto old=std::find_if(output.uniforms.begin(),output.uniforms.end(),[&](const auto& u){return u.name==value.name;});
            if(old==output.uniforms.end())output.uniforms.push_back(std::move(value));
            else require(*old==value,"Shader stages disagree on native uniform layout");
        }
    }
    for(const auto& resource:resources.sampled_images){
        Sampler sampler;sampler.name=resource.name;sampler.count=array_count(compiler.get_type(resource.type_id));
        sampler.binding=sampler.name=="u_tex"?0:sampler.name=="u_pal"?8:compiler.get_decoration(resource.id,spv::DecorationBinding);
        require(sampler.binding<16 && sampler.count<=16-sampler.binding,"Native shader exceeds sampler slots");
        compiler.set_decoration(resource.id,spv::DecorationBinding,sampler.binding);
        const auto old=std::find_if(output.samplers.begin(),output.samplers.end(),[&](const auto& s){return s.name==sampler.name;});
        if(old==output.samplers.end())output.samplers.push_back(std::move(sampler));
        else require(*old==sampler,"Shader stages disagree on sampler bindings");
    }
}
struct Signature {D3D_REGISTER_COMPONENT_TYPE type;unsigned mask;};
using Signatures=std::map<std::pair<std::string,unsigned>,Signature>;
void reflect(ID3DBlob* code,bool vertex,Signatures& outputs){
    Com<ID3D11ShaderReflection> reflection;
    require(SUCCEEDED(D3DReflect(code->GetBufferPointer(),code->GetBufferSize(),IID_ID3D11ShaderReflection,reinterpret_cast<void**>(reflection.out()))),"Cannot reflect native bytecode");
    D3D11_SHADER_DESC shader{};require(SUCCEEDED(reflection->GetDesc(&shader)),"Cannot read native shader description");
    for(unsigned i=0;i<(vertex?shader.OutputParameters:shader.InputParameters);++i){
        D3D11_SIGNATURE_PARAMETER_DESC p{};
        const auto result=vertex?reflection->GetOutputParameterDesc(i,&p):reflection->GetInputParameterDesc(i,&p);
        require(SUCCEEDED(result),"Cannot read native stage signature");
        const auto key=std::make_pair(std::string(p.SemanticName),p.SemanticIndex);
        if(vertex)outputs[key]={p.ComponentType,p.Mask};
        else if(p.SystemValueType==D3D_NAME_UNDEFINED && p.ReadWriteMask){
            auto found=outputs.find(key);
            require(found!=outputs.end() && found->second.type==p.ComponentType && (found->second.mask&p.Mask)==p.Mask,
                "Native stage interface mismatch: "+key.first+std::to_string(key.second));
        }
    }
}
}
bool compile_program(const std::string& vertex,const std::string& pixel,CompiledProgram& output,std::string& error,ShaderOptions options){
    error.clear();
    try{
        static CompilerLifetime lifetime;require(lifetime.ready,"Cannot initialize shader compiler");
        const std::string sources[]={modern(vertex),modern(pixel)};
        glslang::TShader vs(EShLangVertex),ps(EShLangFragment);glslang::TProgram program;
        glslang::TShader* stages[]={&vs,&ps};
        const auto messages=EShMessages(EShMsgSpvRules|EShMsgVulkanRules);
        for(unsigned i=0;i<2;++i){
            auto& shader=*stages[i];const char* source=sources[i].c_str();shader.setStrings(&source,1);
            shader.setEnvInput(glslang::EShSourceGlsl,i?EShLangFragment:EShLangVertex,glslang::EShClientVulkan,450);
            shader.setEnvClient(glslang::EShClientVulkan,glslang::EShTargetVulkan_1_0);
            shader.setEnvTarget(glslang::EShTargetSpv,glslang::EShTargetSpv_1_0);
            shader.setEnvInputVulkanRulesRelaxed();shader.setAutoMapBindings(true);shader.setAutoMapLocations(true);
            shader.setGlobalUniformBlockName("TandemUniforms");shader.setGlobalUniformSet(0);shader.setGlobalUniformBinding(0);
            const auto parsed=shader.parse(GetDefaultResources(),450,false,messages);
            require(parsed,shader.getInfoLog());program.addShader(&shader);
        }
        const auto linked=program.link(messages);require(linked,program.getInfoLog());
        const auto mapped=program.mapIO();require(mapped,program.getInfoLog());
        CompiledProgram next;Signatures signatures;
        for(unsigned i=0;i<2;++i){
            std::vector<unsigned> spirv;glslang::SpvOptions spv_options;spv_options.disableOptimizer=true;
            glslang::GlslangToSpv(*program.getIntermediate(i?EShLangFragment:EShLangVertex),spirv,&spv_options);
            spirv_cross::CompilerHLSL compiler(std::move(spirv));resources(compiler,next);
            auto hlsl=compiler.get_hlsl_options();hlsl.shader_model=50;hlsl.point_size_compat=true;
            // Point coordinates must be real varyings supplied by geometry expansion.
            hlsl.point_coord_compat=false;compiler.set_hlsl_options(hlsl);
            auto common=compiler.get_common_options();common.vertex.fixup_clipspace=true;
            common.vertex.flip_vert_y=options.flip_vertex_y;compiler.set_common_options(common);
            const auto source=compiler.compile();Com<ID3DBlob> code,diagnostic;
            const auto result=D3DCompile(source.c_str(),source.size(),"Project Tandem native shaders",nullptr,nullptr,"main",i?"ps_5_0":"vs_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,code.out(),diagnostic.out());
            require(SUCCEEDED(result),diagnostic.value?std::string(static_cast<const char*>(diagnostic->GetBufferPointer()),diagnostic->GetBufferSize()):"Native shader compilation failed");
            reflect(code.value,i==0,signatures);
            auto& bytes=i?next.pixel:next.vertex;const auto* data=static_cast<const uint8_t*>(code->GetBufferPointer());
            bytes.assign(data,data+code->GetBufferSize());
        }
        output=std::move(next);return true;
    }catch(const std::exception& failure){error=failure.what();return false;}
}
bool compile_gx_program(const gx::ShaderKey& key,CompiledProgram& output,std::string& error,ShaderOptions options){
    try{
        require(key.num_texgens<=8 && ((key.genmode>>16)&7)<=4 && key.fg_depth<=1 && key.dst_alpha<=2 && key.efb_has_alpha<=1,
            "Unsupported native GX shader configuration");
        return compile_program(gx::gen_vertex_shader(true),gx::gen_pixel_shader(key,true),output,error,options);
    }catch(const std::exception& failure){error=failure.what();return false;}
}
}
#endif
