#include "d3d11_program.h"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <cstring>
#include <unordered_map>
#include <utility>
namespace desktop_d3d11 {
namespace {
template<class T>struct Com {
    T* value=nullptr;~Com(){if(value)value->Release();}
    T* operator->()const{return value;}T** out(){return &value;}
};
}
struct Program::Impl {
    Com<ID3D11Device> device;
    Com<ID3D11VertexShader> vertex;
    Com<ID3D11PixelShader> pixel;
    Com<ID3D11Buffer> constants;
    CompiledProgram compiled;
    std::unordered_map<std::string,int> slots;
    std::vector<uint8_t> shadow;
    bool dirty=false;
    uint64_t uploads=0;
    template<class T>bool set(int slot,std::span<const T> values,bool floating,std::string& error){
        error.clear();
        if(slot<0)return true; // An inactive uniform needs no update.
        if(size_t(slot)>=compiled.uniforms.size()){error="Invalid native uniform slot.";return false;}
        const auto& u=compiled.uniforms[size_t(slot)];
        if((u.scalar==UniformScalar::Float)!=floating){error="Native uniform scalar type mismatch.";return false;}
        const size_t scalars=size_t(u.components)*u.columns;
        if(!scalars || values.empty() || values.size()%scalars || values.size()/scalars>u.array_count){
            error="Native uniform value count mismatch.";return false;
        }
        const size_t count=values.size()/scalars;
        const size_t element_stride=u.array_stride?u.array_stride:u.size;
        const size_t column_stride=u.matrix_stride?u.matrix_stride:u.components*sizeof(T);
        const auto end=size_t(u.offset)+(count-1)*element_stride+(u.columns-1)*column_stride+u.components*sizeof(T);
        if(end>shadow.size()){error="Native uniform update exceeds its buffer.";return false;}
        for(size_t element=0;element<count;++element)for(size_t column=0;column<u.columns;++column){
            auto* destination=shadow.data()+u.offset+element*element_stride+column*column_stride;
            const auto* source=values.data()+element*scalars+column*u.components;
            const size_t size=u.components*sizeof(T);
            if(std::memcmp(destination,source,size)){std::memcpy(destination,source,size);dirty=true;}
        }
        return true;
    }
};
Program::Program():state(std::make_unique<Impl>()){}
Program::~Program()=default;
bool Program::initialize(ID3D11Device* device,CompiledProgram compiled,std::string& error){
    error.clear();
    if(!device || compiled.vertex.empty() || compiled.pixel.empty() || compiled.uniform_bytes>65536 || compiled.uniform_bytes%16){
        error="Invalid native program or device.";return false;
    }
    auto next=std::make_unique<Impl>();next->device.value=device;device->AddRef();
    if(FAILED(device->CreateVertexShader(compiled.vertex.data(),compiled.vertex.size(),nullptr,next->vertex.out())) ||
       FAILED(device->CreatePixelShader(compiled.pixel.data(),compiled.pixel.size(),nullptr,next->pixel.out()))){
        error="Cannot create native shader objects.";return false;
    }
    if(compiled.uniform_bytes){
        D3D11_BUFFER_DESC desc{};desc.ByteWidth=compiled.uniform_bytes;desc.Usage=D3D11_USAGE_DYNAMIC;
        desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;desc.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
        if(FAILED(device->CreateBuffer(&desc,nullptr,next->constants.out()))){error="Cannot create native constant buffer.";return false;}
    }
    next->shadow.resize(compiled.uniform_bytes,0);next->dirty=compiled.uniform_bytes!=0;
    next->compiled=std::move(compiled);next->slots.reserve(next->compiled.uniforms.size());
    for(size_t i=0;i<next->compiled.uniforms.size();++i){
        const auto& u=next->compiled.uniforms[i];
        if(u.offset>next->shadow.size() || u.size>next->shadow.size()-u.offset || !next->slots.emplace(u.name,int(i)).second){
            error="Invalid or duplicate native uniform layout.";return false;
        }
    }
    state=std::move(next);return true;
}
int Program::uniform(const std::string& name)const{const auto at=state->slots.find(name);return at==state->slots.end()?-1:at->second;}
bool Program::floats(int slot,std::span<const float> data,std::string& error){return state->set(slot,data,true,error);}
bool Program::integers(int slot,std::span<const int32_t> data,std::string& error){return state->set(slot,data,false,error);}
bool Program::upload_uniforms(ID3D11DeviceContext* context,std::string& error){
    error.clear();if(!context || !state->vertex.value){error="Native program is not initialized.";return false;}
    if(!state->dirty)return true;
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if(FAILED(context->Map(state->constants.value,0,D3D11_MAP_WRITE_DISCARD,0,&mapped))){error="Cannot upload native uniforms.";return false;}
    std::memcpy(mapped.pData,state->shadow.data(),state->shadow.size());context->Unmap(state->constants.value,0);
    state->dirty=false;++state->uploads;return true;
}
bool Program::bind(ID3D11DeviceContext* context,std::string& error){
    if(!upload_uniforms(context,error))return false;
    context->VSSetShader(state->vertex.value,nullptr,0);context->PSSetShader(state->pixel.value,nullptr,0);
    context->VSSetConstantBuffers(0,1,&state->constants.value);context->PSSetConstantBuffers(0,1,&state->constants.value);return true;
}
std::span<const uint8_t> Program::vertex_code()const{return state->compiled.vertex;}
uint64_t Program::uniform_uploads()const{return state->uploads;}
}
#endif
