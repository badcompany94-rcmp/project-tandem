#include "d3d11_geometry.h"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3dcompiler.h>
#include <d3d11shader.h>
#include <bit>
#include <map>
#include <stdexcept>
namespace desktop_d3d11 {
namespace {
template<class T>struct Com {T* p=nullptr;~Com(){if(p)p->Release();}T* operator->()const{return p;}T** out(){return &p;}};
void require(bool ok,const std::string& error){if(!ok)throw std::runtime_error(error);}
struct Field {std::string name,type,semantic;unsigned index,mask;D3D_REGISTER_COMPONENT_TYPE scalar;};
std::vector<Field> signature(std::span<const uint8_t> code){
    Com<ID3D11ShaderReflection> r;require(SUCCEEDED(D3DReflect(code.data(),code.size(),IID_ID3D11ShaderReflection,reinterpret_cast<void**>(r.out()))),"Reflect GX expansion input");
    D3D11_SHADER_DESC desc{};require(SUCCEEDED(r->GetDesc(&desc)),"Read GX expansion signature");std::vector<Field> fields;
    for(unsigned i=0;i<desc.OutputParameters;++i){D3D11_SIGNATURE_PARAMETER_DESC p{};require(SUCCEEDED(r->GetOutputParameterDesc(i,&p)),"Read GX expansion field");
        const auto components=std::popcount(p.Mask);require(components>0 && components<=4,"Invalid GX expansion component count");
        std::string type=p.ComponentType==D3D_REGISTER_COMPONENT_FLOAT32?"float":p.ComponentType==D3D_REGISTER_COMPONENT_SINT32?"int":p.ComponentType==D3D_REGISTER_COMPONENT_UINT32?"uint":"";
        require(!type.empty(),"Unsupported GX expansion scalar");if(components>1)type+=std::to_string(components);
        fields.push_back({"f"+std::to_string(i),type,p.SemanticName,p.SemanticIndex,p.Mask,p.ComponentType});}
    return fields;
}
}
bool geometry_compatible(std::span<const uint8_t> vertex,std::span<const uint8_t> geometry,std::string& error){
    error.clear();try{const auto fields=signature(vertex),generated=signature(geometry);
        require(fields.size()==generated.size(),"Native expansion varying count differs");
        for(const auto& f:fields){bool found=false;for(const auto& g:generated)if(f.semantic==g.semantic && f.index==g.index){found=g.mask==f.mask && g.scalar==f.scalar;break;}
            require(found,"Native expansion layout differs at "+f.semantic+std::to_string(f.index));}return true;
    }catch(const std::exception& failure){error=failure.what();return false;}
}
bool compile_point_geometry(std::span<const uint8_t> vertex,std::vector<uint8_t>& output,std::string& error){
    error.clear();try{
        const auto fields=signature(vertex);std::string position,coordinate,size;
        std::string hlsl="cbuffer ExpansionParameters:register(b1){float2 inverseSize;float2 padding;}\nstruct Vertex{\n";
        for(const auto& f:fields){hlsl+=f.type+" "+f.name+":"+f.semantic+std::to_string(f.index)+";\n";
            if(f.semantic=="SV_Position")position=f.name;
            if(f.semantic=="TEXCOORD" && f.index==12)coordinate=f.name;
            if(f.semantic=="TEXCOORD" && f.index==13)size=f.name;}
        require(!position.empty() && !coordinate.empty() && !size.empty(),"GX expansion interface lacks position/point varyings");hlsl+="};\n[maxvertexcount(4)] void main(";
            hlsl+="point Vertex input[1],inout TriangleStream<Vertex> output){\nVertex v=input[0];float4 center=v."+position+";\n";
            hlsl+="if(center.w<=0 || any(abs(center.xy)>center.ww))return;\n";
            for(const auto& f:fields)if(f.semantic=="SV_ClipDistance")hlsl+="if(any(v."+f.name+"<0))return;\n";
            // Core GL 3.3 section 3.4.1 uses an unrounded center and square
            // width. Older compatibility-profile point snapping is different.
            hlsl+="float width=max(1,v."+size+");\n";
            const char* offsets[]={"float2(-1,1)","float2(-1,-1)","float2(1,1)","float2(1,-1)"};
            const char* coords[]={"float2(0,1)","float2(0,0)","float2(1,1)","float2(1,0)"};
            for(unsigned i=0;i<4;++i)hlsl+="v."+position+"=center;v."+position+".xy+="+offsets[i]+"*inverseSize*width*center.w;v."+coordinate+"="+coords[i]+";output.Append(v);\n";
        hlsl+="output.RestartStrip();}\n";
        Com<ID3DBlob> code,diagnostic;const auto result=D3DCompile(hlsl.data(),hlsl.size(),"TandemNativeExpansion",nullptr,nullptr,"main","gs_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,code.out(),diagnostic.out());
        require(SUCCEEDED(result),diagnostic.p?std::string(static_cast<const char*>(diagnostic->GetBufferPointer()),diagnostic->GetBufferSize()):"Compile native GX expansion");
        // Geometry output must retain the complete packed varying interface.
        const auto generated=signature({static_cast<const uint8_t*>(code->GetBufferPointer()),code->GetBufferSize()});
        for(const auto& f:fields){bool found=false;for(const auto& g:generated)if(f.semantic==g.semantic && f.index==g.index){found=g.mask==f.mask && g.scalar==f.scalar;break;}
            require(found,"Native expansion changed packed varying "+f.semantic+std::to_string(f.index));}
        const auto* bytes=static_cast<const uint8_t*>(code->GetBufferPointer());output.assign(bytes,bytes+code->GetBufferSize());return true;
    }catch(const std::exception& failure){error=failure.what();return false;}
}
}
#endif
