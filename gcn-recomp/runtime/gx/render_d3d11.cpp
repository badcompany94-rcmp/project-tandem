#include "render_d3d11.h"
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <d3d11_1.h>
#include "shadergen.h"
#include "copy_shaders.h"
#include "copy_encode.h"
#include "flat_math.h"
#include "../graphics/d3d11_device.h"
#include "../graphics/d3d11_program.h"
#include "../graphics/d3d11_geometry.h"
#include "../graphics/d3d11_cache.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <stdexcept>
#include <tuple>
#include <unordered_map>
#include <utility>
namespace gx {
namespace {
template<class T>struct Com {
    T* p=nullptr;Com()=default;~Com(){if(p)p->Release();}
    Com(const Com& other):p(other.p){if(p)p->AddRef();}
    Com& operator=(const Com& other){if(other.p)other.p->AddRef();if(p)p->Release();p=other.p;return *this;}
    Com(Com&& other)noexcept:p(std::exchange(other.p,nullptr)){}
    Com& operator=(Com&& other)noexcept{std::swap(p,other.p);return *this;}
    T* operator->()const{return p;}T** out(){return &p;}
};
void require(bool ok,const std::string& error){if(!ok)throw std::runtime_error(error);}
void checked(HRESULT result,const char* error){require(SUCCEEDED(result),error);}
constexpr unsigned efb_w=640,efb_h=528;
using desktop_d3d11::Program;
struct Pipeline {
    Program program;Com<ID3D11InputLayout> layout[9];std::map<std::string,int> slots;
    explicit Pipeline(ID3D11Device* device,desktop_d3d11::ShaderCache& cache,const ShaderKey& key,int logic=-1){
        desktop_d3d11::CompiledProgram compiled;std::string error;
        const auto hits=cache.cache_hits();require(cache.get(key,{true,logic},compiled,error),error);
        // Resolve original names once, outside draw submission.
        for(const auto& u:compiled.uniforms)slots.emplace(u.name,0);
        if(!program.initialize(device,std::move(compiled),error)){
            require(cache.cache_hits()>hits,error);cache.invalidate(key,{true,logic});
            require(cache.get(key,{true,logic},compiled,error),error);slots.clear();for(const auto& u:compiled.uniforms)slots.emplace(u.name,0);
            require(program.initialize(device,std::move(compiled),error),error);
        }
        for(auto& [name,slot]:slots)slot=program.uniform(name);
        const auto code=program.vertex_code();
        for(unsigned n=0;n<=8;++n){std::array<D3D11_INPUT_ELEMENT_DESC,11> a{};
            for(unsigned i=0;i<3+n;++i){a[i].SemanticName="TEXCOORD";a[i].SemanticIndex=i;
                a[i].Format=(i==1 || i==2)?DXGI_FORMAT_R8G8B8A8_UNORM:DXGI_FORMAT_R32G32B32_FLOAT;
                a[i].AlignedByteOffset=i==0?0:i==1?12:i==2?16:20+(i-3)*12;a[i].InputSlotClass=D3D11_INPUT_PER_VERTEX_DATA;}
            // GLSL declares all coordinates. Unused attributes may still appear
            // in native input signatures, so supply zero from a second buffer
            // for missing coordinates rather than reading beyond packed data.
            for(unsigned i=3+n;i<11;++i){a[i].SemanticName="TEXCOORD";a[i].SemanticIndex=i;
                a[i].Format=DXGI_FORMAT_R32G32B32_FLOAT;a[i].InputSlot=1;a[i].InputSlotClass=D3D11_INPUT_PER_VERTEX_DATA;}
            checked(device->CreateInputLayout(a.data(),UINT(a.size()),code.data(),code.size(),layout[n].out()),"Create native GX packed input layout");
        }
    }
    int slot(const char* name)const{auto it=slots.find(name);return it==slots.end()?-1:it->second;}
    void floats(const char* name,std::span<const float> values){std::string error;require(program.floats(slot(name),values,error),error);}
    void integers(const char* name,std::span<const int32_t> values){std::string error;require(program.integers(slot(name),values,error),error);}
};
struct Texture {Com<ID3D11Texture2D> resource;Com<ID3D11ShaderResourceView> view;Com<ID3D11RenderTargetView> target;unsigned width=0,height=0,levels=0;uint64_t hash=0;bool efb=false,depth=false;
    std::shared_ptr<const unsigned char> lease=std::make_shared<const unsigned char>(0);};
struct ResourceImage {unsigned width,height,mips,format;std::vector<std::vector<uint8_t>> levels;bool operator==(const ResourceImage&)const=default;};
struct TargetPool {
    struct Entry {unsigned width,height,format;Com<ID3D11Texture2D> texture;};
    std::mutex mutex;std::vector<Entry> available;size_t bytes=0;
};
struct TargetLease {std::shared_ptr<ID3D11Texture2D> owner;ID3D11Texture2D* p=nullptr;};
struct ScreenPipeline {
    Program program;std::map<std::string,unsigned> samplers;
    void initialize(ID3D11Device* device,const std::string& pixel,bool flip=true){
        desktop_d3d11::CompiledProgram compiled;std::string error;
        require(desktop_d3d11::compile_program(copy_vertex_shader(),pixel,compiled,error,{flip}),error);
        for(const auto& s:compiled.samplers)samplers.emplace(s.name,s.binding);
        require(program.initialize(device,std::move(compiled),error),error);
    }
    void floats(const char* name,std::span<const float> value){std::string error;require(program.floats(program.uniform(name),value,error),error);}
    void integer(const char* name,int32_t value){std::string error;require(program.integers(program.uniform(name),{&value,1},error),error);}
};
struct Buffer {Com<ID3D11Buffer> resource;unsigned bytes=0;};
void upload(ID3D11Device* device,ID3D11DeviceContext* context,Buffer& buffer,UINT bind,std::span<const uint8_t> data){
    if(data.empty())return;
    require(data.size()<=256u*1024*1024,"Native GX geometry exceeds upload limit");
    if(buffer.bytes<data.size()){
        D3D11_BUFFER_DESC desc{};desc.ByteWidth=UINT(std::min<size_t>(256u*1024*1024,std::max<size_t>(4096,std::bit_ceil(data.size()))));
        desc.Usage=D3D11_USAGE_DYNAMIC;desc.BindFlags=bind;desc.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
        Buffer replacement;replacement.bytes=desc.ByteWidth;checked(device->CreateBuffer(&desc,nullptr,replacement.resource.out()),"Create native GX upload buffer");buffer=std::move(replacement);
    }
    D3D11_MAPPED_SUBRESOURCE mapped{};checked(context->Map(buffer.resource.p,0,D3D11_MAP_WRITE_DISCARD,0,&mapped),"Map native GX geometry");
    std::memcpy(mapped.pData,data.data(),data.size());context->Unmap(buffer.resource.p,0);
}
int32_t signed11(uint32_t v){return int32_t(v<<21)>>21;}
float fog_float(uint32_t v){return std::bit_cast<float>(((v>>19)&1)<<31|((v>>11)&255)<<23|(v&2047)<<12);}
ShaderKey native_key(const PixelState& st){
    auto key=make_shader_key(st);
    // Direct3D11 has dual-source blending regardless of the active GL driver.
    const auto* bp=st.bp;const uint32_t bm=bp[0x41];
    if(((bp[0x42]>>8)&1) && ((bm>>4)&1) && (bp[0x43]&7)==1){
        const unsigned sf=(bm>>8)&7,df=(bm>>5)&7;
        key.dst_alpha=(bm&1) && !(bm&(1<<11)) && (sf==4 || sf==5 || df==4 || df==5)?2:1;
    }
    return key;
}
}
struct D3D11Renderer::Impl {
    desktop_d3d11::Device* graphics=nullptr;ID3D11Device* device=nullptr;ID3D11DeviceContext* context=nullptr;unsigned scale=1;
    std::shared_ptr<const unsigned char> generation=std::make_shared<const unsigned char>(0);
    std::shared_ptr<TargetPool> target_pool=std::make_shared<TargetPool>();
    Com<ID3D11Texture2D> color,depth,staging;Com<ID3D11RenderTargetView> target;Com<ID3D11DepthStencilView> depth_target;
    Com<ID3D11ShaderResourceView> color_srv,depth_srv;Com<ID3D11Buffer> zero_coordinates;
    Com<ID3D11RasterizerState> screen_raster;Com<ID3D11DepthStencilState> screen_depth[2];
    Com<ID3D11BlendState> write_masks[16];Com<ID3D11SamplerState> nearest,linear;
    Buffer vertices,indices;
    Com<ID3D11GeometryShader> expansion;std::vector<uint8_t> expansion_code;
    Buffer expansion_params;std::array<float,4> last_expansion={};
    std::unordered_map<ShaderKey,std::unique_ptr<Pipeline>,ShaderKeyHash> programs;
    std::unordered_map<ShaderKey,std::unique_ptr<Pipeline>,ShaderKeyHash> logic_programs[16];
    std::shared_ptr<desktop_d3d11::ShaderCache> shader_cache=std::make_shared<desktop_d3d11::ShaderCache>();
    std::vector<desktop_d3d11::CachedShaderKey> warm_keys;size_t warm_done=0;
    Com<ID3D11Device1> device1;bool native_logic=false;Texture logic_copy;
    std::unordered_map<uint32_t,Texture> textures;
    Texture black;
    std::map<std::tuple<uint32_t,uint32_t,unsigned>,Com<ID3D11SamplerState>> samplers;
    std::map<uint32_t,Com<ID3D11DepthStencilState>> depth_states;
    std::map<uint32_t,Com<ID3D11RasterizerState>> raster_states;
    std::map<std::tuple<uint32_t,uint32_t,uint32_t>,Com<ID3D11BlendState>> blend_states;
    std::vector<uint32_t> dead_pending;
    TextureLoader texture_loader=nullptr;
    ScreenPipeline copy_pipeline,clear_pipeline,scanout_pipeline;
    struct Xfb {uint32_t base,stride,height,written_rows=0;std::vector<uint8_t> written;Texture texture;};
    std::vector<Xfb> displays;
    float zplane[4]={};
    int logic_mode(const PixelState& state)const{
        const auto blend=state.bp[0x41];return !native_logic && !(blend&1) && !(blend&(1<<11)) && (blend&2) && ((blend>>12)&15)!=3?int((blend>>12)&15):-1;
    }
    Texture target_texture(unsigned w,unsigned h){
        require(w && h && w<=16384 && h<=16384,"Invalid native GX target size");
        Texture t;t.width=w;t.height=h;t.levels=1;t.efb=true;
        D3D11_TEXTURE2D_DESC d{};d.Width=w;d.Height=h;d.MipLevels=d.ArraySize=1;d.Format=DXGI_FORMAT_R8G8B8A8_UNORM;d.SampleDesc.Count=1;d.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
        checked(device->CreateTexture2D(&d,nullptr,t.resource.out()),"Create native GX copy texture");
        checked(device->CreateShaderResourceView(t.resource.p,nullptr,t.view.out()),"Create native GX copy view");
        checked(device->CreateRenderTargetView(t.resource.p,nullptr,t.target.out()),"Create native GX copy target");return t;
    }
    void screen_begin(ID3D11RenderTargetView* target,ID3D11DepthStencilView* depth,unsigned w,unsigned h,unsigned mask,bool write_depth,const D3D11_RECT& rect){
        ID3D11ShaderResourceView* empty[16]={};context->PSSetShaderResources(0,16,empty);context->OMSetRenderTargets(1,&target,depth);
        context->RSSetState(screen_raster.p);context->RSSetScissorRects(1,&rect);D3D11_VIEWPORT vp{};vp.Width=float(w);vp.Height=float(h);vp.MaxDepth=1;context->RSSetViewports(1,&vp);
        context->OMSetBlendState(write_masks[mask].p,nullptr,~0u);context->OMSetDepthStencilState(screen_depth[write_depth?1:0].p,0);
        context->IASetInputLayout(nullptr);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);context->GSSetShader(nullptr,nullptr,0);
    }
    std::vector<uint8_t> read(ID3D11Texture2D* source){
        D3D11_TEXTURE2D_DESC d{};source->GetDesc(&d);d.BindFlags=0;d.Usage=D3D11_USAGE_STAGING;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;d.MiscFlags=0;
        Com<ID3D11Texture2D> staging;checked(device->CreateTexture2D(&d,nullptr,staging.out()),"Create native GX copy readback");
        context->OMSetRenderTargets(0,nullptr,nullptr);context->CopyResource(staging.p,source);
        std::vector<uint8_t> bytes(size_t(d.Width)*d.Height*4);D3D11_MAPPED_SUBRESOURCE m{};
        checked(context->Map(staging.p,0,D3D11_MAP_READ,0,&m),"Read native GX copy texture");
        for(unsigned y=0;y<d.Height;++y)std::memcpy(bytes.data()+size_t(y)*d.Width*4,static_cast<const uint8_t*>(m.pData)+y*m.RowPitch,d.Width*4);
        context->Unmap(staging.p,0);return bytes;
    }
    TargetLease clone(ID3D11Texture2D* source){
        D3D11_TEXTURE2D_DESC desc{};source->GetDesc(&desc);desc.BindFlags=0;desc.MiscFlags=0;desc.Usage=D3D11_USAGE_DEFAULT;desc.CPUAccessFlags=0;
        require(desc.MipLevels==1 && desc.ArraySize==1 && desc.SampleDesc.Count==1,"Unsupported native GX checkpoint target");
        Com<ID3D11Texture2D> copy;const size_t bytes=size_t(desc.Width)*desc.Height*4;
        {std::lock_guard lock(target_pool->mutex);auto found=std::find_if(target_pool->available.begin(),target_pool->available.end(),[&](const auto& t){return t.width==desc.Width && t.height==desc.Height && t.format==unsigned(desc.Format);});
            if(found!=target_pool->available.end()){copy=std::move(found->texture);target_pool->bytes-=bytes;target_pool->available.erase(found);}}
        if(!copy.p)checked(device->CreateTexture2D(&desc,nullptr,copy.out()),"Create native GX checkpoint texture");context->CopyResource(copy.p,source);
        auto* resource=std::exchange(copy.p,nullptr);auto pool=target_pool;
        std::shared_ptr<ID3D11Texture2D> owner(resource,[pool,desc,bytes](ID3D11Texture2D* resource){
            Com<ID3D11Texture2D> texture;texture.p=resource;std::lock_guard lock(pool->mutex);
            if(pool->available.size()<32 && pool->bytes+bytes<=256u*1024u*1024u){pool->available.push_back({desc.Width,desc.Height,unsigned(desc.Format),std::move(texture)});pool->bytes+=bytes;}
        });return {std::move(owner),resource};
    }
    ResourceImage image(ID3D11Texture2D* source){
        D3D11_TEXTURE2D_DESC d{};source->GetDesc(&d);ResourceImage image{d.Width,d.Height,d.MipLevels,unsigned(d.Format),{}};
        d.BindFlags=0;d.MiscFlags=0;d.Usage=D3D11_USAGE_STAGING;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;Com<ID3D11Texture2D> staging;
        checked(device->CreateTexture2D(&d,nullptr,staging.out()),"Create native GX checkpoint oracle");context->CopyResource(staging.p,source);
        unsigned w=d.Width,h=d.Height;for(unsigned mip=0;mip<d.MipLevels;++mip){std::vector<uint8_t> data(size_t(w)*h*4);D3D11_MAPPED_SUBRESOURCE m{};
            checked(context->Map(staging.p,mip,D3D11_MAP_READ,0,&m),"Read native GX checkpoint oracle");
            for(unsigned y=0;y<h;++y)std::memcpy(data.data()+size_t(y)*w*4,static_cast<const uint8_t*>(m.pData)+y*m.RowPitch,w*4);context->Unmap(staging.p,mip);
            image.levels.push_back(std::move(data));w=std::max(1u,w/2);h=std::max(1u,h/2);}return image;
    }
    void unbind(){context->OMSetRenderTargets(0,nullptr,nullptr);ID3D11ShaderResourceView* views[17]={};context->PSSetShaderResources(0,17,views);}
    ID3D11SamplerState* sampler(uint32_t mode0,uint32_t mode1,unsigned levels){
        const auto key=std::make_tuple(mode0,mode1,levels);auto found=samplers.find(key);if(found!=samplers.end())return found->second.p;
        D3D11_SAMPLER_DESC s{};const D3D11_TEXTURE_ADDRESS_MODE wrap[]={D3D11_TEXTURE_ADDRESS_CLAMP,D3D11_TEXTURE_ADDRESS_WRAP,D3D11_TEXTURE_ADDRESS_MIRROR,D3D11_TEXTURE_ADDRESS_WRAP};
        s.AddressU=wrap[mode0&3];s.AddressV=wrap[(mode0>>2)&3];s.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
        const auto minf=(mode0>>5)&7,mip=minf&3;
        const bool min_linear=(minf&4)!=0,mag_linear=(mode0&(1<<4))!=0,mip_linear=mip>1 && levels>1;
        s.Filter=D3D11_FILTER((min_linear?0x10:0)|(mag_linear?0x4:0)|(mip_linear?0x1:0));
        s.MipLODBias=int8_t((mode0>>9)&255)/32.f;s.MinLOD=(mode1&255)/16.f;s.MaxLOD=((mode1>>8)&255)/16.f;
        if(!mip || levels<=1)s.MinLOD=s.MaxLOD=0;
        else s.MaxLOD=std::min(s.MaxLOD,float(levels-1));
        s.ComparisonFunc=D3D11_COMPARISON_NEVER;s.MaxAnisotropy=1;
        Com<ID3D11SamplerState> value;checked(device->CreateSamplerState(&s,value.out()),"Create native GX sampler");
        return samplers.emplace(key,std::move(value)).first->second.p;
    }
    void upload_texture(const TexData& data){
        require(data.id && data.width && data.height && !data.levels.empty(),"Invalid native GX texture");
        Replacement replacement;
        if(texture_loader && texture_loader(data,replacement)){
            require(replacement.width && replacement.height && replacement.width<=8192 && replacement.height<=8192 && replacement.rgba.size()==size_t(replacement.width)*replacement.height*4,"Invalid native GX replacement texture");
            D3D11_TEXTURE2D_DESC d{};d.Width=replacement.width;d.Height=replacement.height;d.ArraySize=1;d.Format=DXGI_FORMAT_R8G8B8A8_UNORM;d.SampleDesc.Count=1;
            d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;d.MiscFlags=D3D11_RESOURCE_MISC_GENERATE_MIPS;
            Texture texture;texture.width=d.Width;texture.height=d.Height;texture.hash=data.hash;
            for(unsigned n=std::max(d.Width,d.Height);n;n/=2)++texture.levels;
            checked(device->CreateTexture2D(&d,nullptr,texture.resource.out()),"Create native GX replacement mip chain");
            context->UpdateSubresource(texture.resource.p,0,nullptr,replacement.rgba.data(),d.Width*4,0);
            checked(device->CreateShaderResourceView(texture.resource.p,nullptr,texture.view.out()),"Create native GX replacement view");context->GenerateMips(texture.view.p);
            textures.insert_or_assign(data.id,std::move(texture));return;
        }
        require(data.width<=D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION && data.height<=D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION,"Native GX texture dimensions exceed hardware limit");
        D3D11_TEXTURE2D_DESC d{};d.Width=data.width;d.Height=data.height;d.MipLevels=UINT(data.levels.size());d.ArraySize=1;
        d.Format=DXGI_FORMAT_R8G8B8A8_UNORM;d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_IMMUTABLE;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        std::vector<D3D11_SUBRESOURCE_DATA> levels;unsigned w=data.width,h=data.height;
        for(const auto& level:data.levels){require(level.size()==size_t(w)*h,"Invalid native GX mip payload");
            D3D11_SUBRESOURCE_DATA l{};l.pSysMem=level.data();l.SysMemPitch=w*4;levels.push_back(l);w=std::max(1u,w/2);h=std::max(1u,h/2);}
        Texture texture;texture.width=data.width;texture.height=data.height;texture.levels=unsigned(levels.size());texture.hash=data.hash;checked(device->CreateTexture2D(&d,levels.data(),texture.resource.out()),"Upload native GX texture");
        checked(device->CreateShaderResourceView(texture.resource.p,nullptr,texture.view.out()),"Create native GX texture view");textures.insert_or_assign(data.id,std::move(texture));
    }
    void prepare_state(const PixelState& state,int prim){
        const auto* bp=state.bp;auto key=native_key(state);const int logic=logic_mode(state);auto& cache=logic<0?programs:logic_programs[logic];
        if(logic>=0 && !logic_copy.resource.p)logic_copy=target_texture(efb_w*scale,efb_h*scale);
        if(!cache.contains(key)){
            auto pipeline=std::make_unique<Pipeline>(device,*shader_cache,key,logic);std::string error;
            if(expansion_code.empty()){
                require(desktop_d3d11::compile_point_geometry(pipeline->program.vertex_code(),expansion_code,error),error);
                checked(device->CreateGeometryShader(expansion_code.data(),expansion_code.size(),nullptr,expansion.out()),"Create native GX point expansion");
            }else require(desktop_d3d11::geometry_compatible(pipeline->program.vertex_code(),expansion_code,error),error);
            cache.emplace(key,std::move(pipeline));
        }
        const uint32_t zm=bp[0x40]&31;
        if(!depth_states.contains(zm)){
            D3D11_DEPTH_STENCIL_DESC z{};z.DepthEnable=zm&1;z.DepthWriteMask=(zm&16)?D3D11_DEPTH_WRITE_MASK_ALL:D3D11_DEPTH_WRITE_MASK_ZERO;
            z.DepthFunc=D3D11_COMPARISON_FUNC(1+((zm>>1)&7));Com<ID3D11DepthStencilState> value;
            checked(device->CreateDepthStencilState(&z,value.out()),"Create native GX depth state");depth_states.emplace(zm,std::move(value));
        }
        const auto cull=prim==0?(bp[0]>>14)&3:0;
        if(!raster_states.contains(cull)){
            D3D11_RASTERIZER_DESC r{};r.FillMode=D3D11_FILL_SOLID;r.CullMode=cull==1?D3D11_CULL_FRONT:cull==2?D3D11_CULL_BACK:D3D11_CULL_NONE;
            r.FrontCounterClockwise=FALSE;r.DepthClipEnable=FALSE;r.ScissorEnable=TRUE;
            Com<ID3D11RasterizerState> value;checked(device->CreateRasterizerState(&r,value.out()),"Create native GX raster state");raster_states.emplace(cull,std::move(value));
        }
        const auto blend_key=std::make_tuple(bp[0x41],bp[0x42],bp[0x43]);
        if(!blend_states.contains(blend_key)){
            const uint32_t bm=bp[0x41];D3D11_BLEND_DESC b{};auto& rt=b.RenderTarget[0];
            const bool subtract=bm&(1<<11),blend=bm&1,logic=bm&2;
            static const D3D11_BLEND src[]={D3D11_BLEND_ZERO,D3D11_BLEND_ONE,D3D11_BLEND_DEST_COLOR,D3D11_BLEND_INV_DEST_COLOR,D3D11_BLEND_SRC_ALPHA,D3D11_BLEND_INV_SRC_ALPHA,D3D11_BLEND_DEST_ALPHA,D3D11_BLEND_INV_DEST_ALPHA};
            static const D3D11_BLEND dst[]={D3D11_BLEND_ZERO,D3D11_BLEND_ONE,D3D11_BLEND_SRC_COLOR,D3D11_BLEND_INV_SRC_COLOR,D3D11_BLEND_SRC_ALPHA,D3D11_BLEND_INV_SRC_ALPHA,D3D11_BLEND_DEST_ALPHA,D3D11_BLEND_INV_DEST_ALPHA};
            auto alpha=[](D3D11_BLEND f){return f==D3D11_BLEND_DEST_COLOR?D3D11_BLEND_DEST_ALPHA:f==D3D11_BLEND_INV_DEST_COLOR?D3D11_BLEND_INV_DEST_ALPHA:f==D3D11_BLEND_SRC_COLOR?D3D11_BLEND_SRC_ALPHA:f==D3D11_BLEND_INV_SRC_COLOR?D3D11_BLEND_INV_SRC_ALPHA:f;};
            auto dual=[](D3D11_BLEND f){return f==D3D11_BLEND_SRC_ALPHA?D3D11_BLEND_SRC1_ALPHA:f==D3D11_BLEND_INV_SRC_ALPHA?D3D11_BLEND_INV_SRC1_ALPHA:f;};
            rt.BlendEnable=subtract || blend;rt.BlendOp=rt.BlendOpAlpha=subtract?D3D11_BLEND_OP_REV_SUBTRACT:D3D11_BLEND_OP_ADD;
            rt.SrcBlend=subtract?D3D11_BLEND_ONE:src[(bm>>8)&7];rt.DestBlend=subtract?D3D11_BLEND_ONE:dst[(bm>>5)&7];
            rt.SrcBlendAlpha=alpha(rt.SrcBlend);rt.DestBlendAlpha=alpha(rt.DestBlend);
            if(key.dst_alpha){if(!subtract){rt.SrcBlend=dual(rt.SrcBlend);rt.DestBlend=dual(rt.DestBlend);}rt.BlendOpAlpha=D3D11_BLEND_OP_ADD;rt.SrcBlendAlpha=D3D11_BLEND_ONE;rt.DestBlendAlpha=D3D11_BLEND_ZERO;}
            rt.RenderTargetWriteMask=((bm&8)?7:0)|(((bm&16) && (bp[0x43]&7)==1)?8:0);
            Com<ID3D11BlendState> value;
            if(logic && !subtract && !blend && native_logic){
                static const D3D11_LOGIC_OP operations[]={D3D11_LOGIC_OP_CLEAR,D3D11_LOGIC_OP_AND,D3D11_LOGIC_OP_AND_REVERSE,D3D11_LOGIC_OP_COPY,D3D11_LOGIC_OP_AND_INVERTED,D3D11_LOGIC_OP_NOOP,D3D11_LOGIC_OP_XOR,D3D11_LOGIC_OP_OR,D3D11_LOGIC_OP_NOR,D3D11_LOGIC_OP_EQUIV,D3D11_LOGIC_OP_INVERT,D3D11_LOGIC_OP_OR_REVERSE,D3D11_LOGIC_OP_COPY_INVERTED,D3D11_LOGIC_OP_OR_INVERTED,D3D11_LOGIC_OP_NAND,D3D11_LOGIC_OP_SET};
                D3D11_BLEND_DESC1 d{};auto& t=d.RenderTarget[0];t.LogicOpEnable=TRUE;t.LogicOp=operations[(bm>>12)&15];t.RenderTargetWriteMask=rt.RenderTargetWriteMask;
                t.SrcBlend=t.SrcBlendAlpha=D3D11_BLEND_ONE;t.DestBlend=t.DestBlendAlpha=D3D11_BLEND_ZERO;t.BlendOp=t.BlendOpAlpha=D3D11_BLEND_OP_ADD;
                Com<ID3D11BlendState1> state1;checked(device1->CreateBlendState1(&d,state1.out()),"Create native GX logical blend state");value.p=state1.p;state1.p=nullptr;
            }else checked(device->CreateBlendState(&b,value.out()),"Create native GX blend state");
            blend_states.emplace(blend_key,std::move(value));
        }
        for(unsigned m=0;m<8;++m){auto found=textures.find(state.tex_id[m]);if(found==textures.end())continue;
            const auto base=m<4?0x80+m:0xa0+m-4;const auto& t=found->second;
            sampler(t.depth?bp[base]&~0xf0u:bp[base],bp[base+4],t.efb?1:t.levels);}
    }
    void uniforms(Pipeline& p,const PixelState& st,int prim,float overlay_scale,float overlay_offset){
        const auto* bp=st.bp;const auto* proj=st.proj;const auto* vp=st.viewport;
        const bool perspective=int(proj[6])==0;float matrix[16]={};
        matrix[0]=proj[0];matrix[5]=proj[2];matrix[10]=proj[4];matrix[14]=proj[5];
        if(perspective){matrix[8]=proj[1];matrix[9]=proj[3];matrix[11]=-1;}else{matrix[12]=proj[1];matrix[13]=proj[3];matrix[15]=1;}
        p.floats("u_proj",matrix);const int32_t mode[]={0};p.integers("u_vr",mode);
        const int xoff=int(bp[0x59]&1023)*2,yoff=int((bp[0x59]>>10)&1023)*2;
        const float vpa[]={((2*(vp[3]-xoff)/efb_w-1)*overlay_scale+overlay_offset),2*vp[0]/efb_w*overlay_scale,2*(vp[4]-yoff)/efb_h-1,2*vp[1]/efb_h};
        const float vpb[]={2*vp[5]/16777215.f-1,2*vp[2]/16777215.f,0,0};p.floats("u_vp_a",vpa);p.floats("u_vp_b",vpb);
        const float zproj[]={proj[4],proj[5],perspective?-1.f:0.f,perspective?0.f:1.f};p.floats("u_zproj",zproj);
        int32_t regs[16],konst[16];for(unsigned r=0;r<4;++r){const auto ra=st.tev_reg[r][0],bg=st.tev_reg[r][1],ka=st.tev_konst[r][0],kb=st.tev_konst[r][1];
            regs[r*4]=signed11(ra);regs[r*4+3]=signed11(ra>>12);regs[r*4+2]=signed11(bg);regs[r*4+1]=signed11(bg>>12);
            konst[r*4]=ka&255;konst[r*4+3]=(ka>>12)&255;konst[r*4+2]=kb&255;konst[r*4+1]=(kb>>12)&255;}
        p.integers("u_reg",regs);p.integers("u_konst",konst);const int32_t aref[]={int32_t(bp[0xf3]&255),int32_t((bp[0xf3]>>8)&255)};p.integers("u_alpharef",aref);
        const int32_t zbias[]={int32_t(bp[0xf4]&0xffffff)};p.integers("u_zbias",zbias);const float da[]={float(bp[0x42]&255)/255};p.floats("u_dstalpha",da);p.floats("u_zplane",zplane);
        int32_t im[18],isc[3];for(unsigned m=0;m<3;++m){const auto a=bp[6+m*3],b=bp[7+m*3],c=bp[8+m*3];
            im[m*6]=signed11(a);im[m*6+1]=signed11(b);im[m*6+2]=signed11(c);im[m*6+3]=signed11(a>>11);im[m*6+4]=signed11(b>>11);im[m*6+5]=signed11(c>>11);
            isc[m]=int(((a>>22)&3)|(((b>>22)&3)<<2)|(((c>>22)&3)<<4))-17;}
        p.integers("u_indmtx",im);p.integers("u_indscale",isc);float cs[8];for(unsigned i=0;i<4;++i){const auto word=bp[0x25+i/2]>>((i&1)*8);cs[i*2]=1.f/float(1u<<(word&15));cs[i*2+1]=1.f/float(1u<<((word>>4)&15));}p.floats("u_indcoordscale",cs);
        const float fog[]={fog_float(bp[0xee]),fog_float(bp[0xf1]),float(bp[0xef]&0xffffff),float(bp[0xf0]&31)};p.floats("u_fog",fog);
        const float fc[]={float((bp[0xf2]>>16)&255)/255,float((bp[0xf2]>>8)&255)/255,float(bp[0xf2]&255)/255};p.floats("u_fogcolor",fc);
        const int32_t no_screen[]={0};p.integers("u_screen_uv",no_screen);const float ripple[]={1,1};p.floats("u_screen_ripple",ripple);
        const float point_size[]={std::max(1.f,float((bp[0x22]>>8)&255)/6*scale)};p.floats("u_point_size",point_size);
        float offsets[8]={};static const float ranges[]={0,1.f/16,1.f/8,1.f/4,.5f,1,1,1};bool points=false;
        if(prim==2)for(unsigned t=0;t<draw_ntex(st);++t)if(bp[0x30+t*2]&(1<<19)){offsets[t]=ranges[(bp[0x22]>>19)&7];points=points || offsets[t]!=0;}
        const int32_t point_mode[]={points?1:0};p.integers("u_points",point_mode);p.floats("u_point_off",offsets);
        float sizes[16];ID3D11ShaderResourceView* views[16];ID3D11SamplerState* filters[16];uint32_t texsnap=0;
        std::fill(std::begin(views),std::end(views),black.view.p);std::fill(std::begin(filters),std::end(filters),nearest.p);
        for(unsigned m=0;m<8;++m){sizes[m*2]=sizes[m*2+1]=1;auto it=textures.find(st.tex_id[m]);
            if(it!=textures.end()){const unsigned base=m<4?0x80+m:0xa0+m-4;const auto& t=it->second;views[m]=t.view.p;
                filters[m]=sampler(t.depth?bp[base]&~0xf0u:bp[base],bp[base+4],t.efb?1:t.levels);sizes[m*2]=float((bp[base+8]&1023)+1);sizes[m*2+1]=float(((bp[base+8]>>10)&1023)+1);
                if(t.depth)texsnap|=1u<<m;}
            if(st.tex_is_efb[m]){auto pal=textures.find(st.tex_pal[m]);if(pal!=textures.end())views[8+m]=pal->second.view.p;}}
        p.floats("u_texsize",sizes);const int32_t snap[]={int32_t(texsnap)};p.integers("u_texsnap",snap);
        context->PSSetShaderResources(0,16,views);context->PSSetSamplers(0,16,filters);
    }
};
struct D3D11Renderer::Snapshot {
    TargetLease color,depth;
    std::unordered_map<uint32_t,Texture> textures;
    std::vector<Impl::Xfb> displays;
    std::vector<uint32_t> dead;
    std::array<float,4> zplane;
    std::shared_ptr<const unsigned char> generation;
    std::unique_ptr<const std::vector<ResourceImage>> oracle;
    std::vector<ResourceImage> images(Impl& s)const{
        std::vector<ResourceImage> images;images.push_back(s.image(color.p));images.push_back(s.image(depth.p));
        std::vector<uint32_t> ids;for(const auto& [id,texture]:textures)ids.push_back(id);std::sort(ids.begin(),ids.end());
        for(auto id:ids)images.push_back(s.image(textures.at(id).resource.p));for(const auto& x:displays)images.push_back(s.image(x.texture.resource.p));return images;
    }
};
D3D11Renderer::D3D11Renderer()=default;D3D11Renderer::~D3D11Renderer(){std::string error;if(!save_shader_cache(error))std::fprintf(stderr,"[d3d11-shaders] %s\n",error.c_str());}
bool D3D11Renderer::initialize(desktop_d3d11::Device& graphics,int scale,std::string& error,bool force_logic_fallback){
    error.clear();try{
        require(graphics.device() && graphics.context() && scale>=1 && scale<=8,"Invalid native GX initialization");
        auto s=std::make_unique<Impl>();s->graphics=&graphics;s->device=graphics.device();s->context=graphics.context();s->scale=unsigned(scale);auto* d=s->device;
        D3D11_FEATURE_DATA_D3D11_OPTIONS features{};
        s->native_logic=!force_logic_fallback && SUCCEEDED(d->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS,&features,sizeof(features))) && features.OutputMergerLogicOp &&
            SUCCEEDED(d->QueryInterface(IID_ID3D11Device1,reinterpret_cast<void**>(s->device1.out())));
        D3D11_TEXTURE2D_DESC t{};t.Width=efb_w*scale;t.Height=efb_h*scale;t.MipLevels=t.ArraySize=1;t.Format=DXGI_FORMAT_R8G8B8A8_UNORM;t.SampleDesc.Count=1;t.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
        checked(d->CreateTexture2D(&t,nullptr,s->color.out()),"Create native GX EFB color");checked(d->CreateRenderTargetView(s->color.p,nullptr,s->target.out()),"Create native GX EFB target");
        checked(d->CreateShaderResourceView(s->color.p,nullptr,s->color_srv.out()),"Create native GX EFB color view");
        // Match GL's DEPTH_COMPONENT32F exactly; D24 would change CPU-visible
        // depth-copy bytes and rollback results near rounding boundaries.
        t.Format=DXGI_FORMAT_R32_TYPELESS;t.BindFlags=D3D11_BIND_DEPTH_STENCIL|D3D11_BIND_SHADER_RESOURCE;checked(d->CreateTexture2D(&t,nullptr,s->depth.out()),"Create native GX EFB depth");
        D3D11_DEPTH_STENCIL_VIEW_DESC z{};z.Format=DXGI_FORMAT_D32_FLOAT;z.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2D;
        checked(d->CreateDepthStencilView(s->depth.p,&z,s->depth_target.out()),"Create native GX EFB depth target");
        D3D11_SHADER_RESOURCE_VIEW_DESC view{};view.Format=DXGI_FORMAT_R32_FLOAT;view.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;view.Texture2D.MipLevels=1;
        checked(d->CreateShaderResourceView(s->depth.p,&view,s->depth_srv.out()),"Create native GX EFB depth view");
        t.Format=DXGI_FORMAT_R8G8B8A8_UNORM;t.Usage=D3D11_USAGE_STAGING;t.BindFlags=0;t.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        checked(d->CreateTexture2D(&t,nullptr,s->staging.out()),"Create native GX EFB readback");
        D3D11_BUFFER_DESC b{};b.ByteWidth=12;b.Usage=D3D11_USAGE_IMMUTABLE;b.BindFlags=D3D11_BIND_VERTEX_BUFFER;const float zero[3]={0,0,1};D3D11_SUBRESOURCE_DATA data{};data.pSysMem=zero;
        checked(d->CreateBuffer(&b,&data,s->zero_coordinates.out()),"Create native GX default coordinates");
        s->copy_pipeline.initialize(d,copy_pixel_shader());
        s->scanout_pipeline.initialize(d,scanout_pixel_shader(),false);
        s->clear_pipeline.initialize(d,"#version 330 core\nuniform vec4 u_clear;uniform float u_clear_depth;out vec4 o;void main(){o=u_clear;gl_FragDepth=u_clear_depth;}\n");
        D3D11_RASTERIZER_DESC r{};r.FillMode=D3D11_FILL_SOLID;r.CullMode=D3D11_CULL_NONE;r.ScissorEnable=TRUE;r.DepthClipEnable=FALSE;
        checked(d->CreateRasterizerState(&r,s->screen_raster.out()),"Create native GX copy raster state");
        for(unsigned i=0;i<2;++i){D3D11_DEPTH_STENCIL_DESC ds{};ds.DepthEnable=i!=0;ds.DepthWriteMask=i?D3D11_DEPTH_WRITE_MASK_ALL:D3D11_DEPTH_WRITE_MASK_ZERO;ds.DepthFunc=D3D11_COMPARISON_ALWAYS;
            checked(d->CreateDepthStencilState(&ds,s->screen_depth[i].out()),"Create native GX copy depth state");}
        for(unsigned i=0;i<16;++i){D3D11_BLEND_DESC blend{};auto& rt=blend.RenderTarget[0];rt.RenderTargetWriteMask=UINT8(i);rt.SrcBlend=rt.SrcBlendAlpha=D3D11_BLEND_ONE;rt.DestBlend=rt.DestBlendAlpha=D3D11_BLEND_ZERO;rt.BlendOp=rt.BlendOpAlpha=D3D11_BLEND_OP_ADD;
            checked(d->CreateBlendState(&blend,s->write_masks[i].out()),"Create native GX copy channel mask");}
        D3D11_SAMPLER_DESC sampler{};sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sampler.ComparisonFunc=D3D11_COMPARISON_NEVER;sampler.MaxAnisotropy=1;sampler.MaxLOD=0;
        sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;checked(d->CreateSamplerState(&sampler,s->nearest.out()),"Create native GX depth copy sampler");
        sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;checked(d->CreateSamplerState(&sampler,s->linear.out()),"Create native GX color copy sampler");
        // GL's incomplete/unbound texture reads opaque black. D3D's null SRV
        // returns transparent black, which would change TEV alpha decisions.
        {D3D11_TEXTURE2D_DESC t{};t.Width=t.Height=t.MipLevels=t.ArraySize=1;t.Format=DXGI_FORMAT_R8G8B8A8_UNORM;t.SampleDesc.Count=1;t.Usage=D3D11_USAGE_IMMUTABLE;t.BindFlags=D3D11_BIND_SHADER_RESOURCE;
            const uint32_t opaque_black=0xff000000;D3D11_SUBRESOURCE_DATA data{};data.pSysMem=&opaque_black;data.SysMemPitch=4;
            checked(d->CreateTexture2D(&t,&data,s->black.resource.out()),"Create native GX unbound texture");checked(d->CreateShaderResourceView(s->black.resource.p,nullptr,s->black.view.out()),"Create native GX unbound texture view");}
        const float black[]={0,0,0,1};s->context->ClearRenderTargetView(s->target.p,black);s->context->ClearDepthStencilView(s->depth_target.p,D3D11_CLEAR_DEPTH,1,0);
        // Warm the bounded GPU target pool during initialization, outside a
        // race. Two ordinary-resolution checkpoints need no fresh allocation.
        {std::vector<TargetLease> warm;for(unsigned i=0;i<(scale<8?2u:1u);++i){warm.push_back(s->clone(s->color.p));warm.push_back(s->clone(s->depth.p));}}
        if(state && state->device==d){
            s->texture_loader=state->texture_loader;
            s->shader_cache=state->shader_cache;s->warm_keys=state->warm_keys;s->warm_done=state->warm_done;
            for(const auto& [id,texture]:state->textures)if(!texture.efb)s->textures.emplace(id,texture);
            if(s->native_logic==state->native_logic){
                s->programs=std::move(state->programs);for(unsigned i=0;i<16;++i)s->logic_programs[i]=std::move(state->logic_programs[i]);
                s->expansion=std::move(state->expansion);s->expansion_code=std::move(state->expansion_code);
                s->samplers=std::move(state->samplers);s->depth_states=std::move(state->depth_states);s->raster_states=std::move(state->raster_states);s->blend_states=std::move(state->blend_states);
            }
        }
        state=std::move(s);return true;
    }catch(const std::exception& failure){error=failure.what();return false;}
}
bool D3D11Renderer::prepare(const Batch& batch,std::string& error){
    error.clear();try{require(state!=nullptr,"Native GX backend is not initialized");auto& s=*state;
        for(auto id:s.dead_pending)s.textures.erase(id);s.dead_pending=batch.dead_textures;
        for(const auto& texture:batch.new_textures){require(texture!=nullptr,"Null native GX texture");s.upload_texture(*texture);}
        upload(s.device,s.context,s.vertices,D3D11_BIND_VERTEX_BUFFER,batch.packed);
        upload(s.device,s.context,s.indices,D3D11_BIND_INDEX_BUFFER,{reinterpret_cast<const uint8_t*>(batch.indices.data()),batch.indices.size()*sizeof(uint32_t)});
        for(const auto& cmd:batch.cmds)if(cmd.type==CmdType::Draw){require(cmd.state<batch.states.size(),"Native GX draw state outside batch");s.prepare_state(batch.states[cmd.state],cmd.prim);}
        std::fill(std::begin(s.zplane),std::end(s.zplane),0.f);return true;
    }catch(const std::exception& failure){error=failure.what();return false;}
}
bool D3D11Renderer::draw(const Batch& batch,const Cmd& cmd,float overlay_scale,float overlay_offset,std::string& error){
    error.clear();try{require(state!=nullptr,"Native GX backend is not initialized");auto& s=*state;
        require(cmd.type==CmdType::Draw && cmd.state<batch.states.size() && cmd.first<=batch.indices.size() && cmd.count<=batch.indices.size()-cmd.first,"Invalid native GX draw");
        const auto& st=batch.states[cmd.state];const auto* bp=st.bp;const auto cull=cmd.prim==0?(bp[0]>>14)&3:0;
        // Like the GL path, even a culled triangle can set the depth plane used
        // by following z-freeze draws. Do this before the cull-all early return.
        if(!((bp[0]>>19)&1))flat_zfreeze_plane(batch,cmd,s.scale,s.zplane);
        if(!cmd.count || (cmd.prim==0 && cull==3))return true;
        require(cmd.prim<=2,"Invalid native GX primitive");
        const int logic=s.logic_mode(st);auto& cache=logic<0?s.programs:s.logic_programs[logic];
        const auto found=cache.find(native_key(st));require(found!=cache.end(),"Native GX pipeline was not prepared");auto& p=*found->second;
        if(logic>=0){
            ID3D11ShaderResourceView* empty=nullptr;s.context->PSSetShaderResources(16,1,&empty);s.context->OMSetRenderTargets(0,nullptr,nullptr);
            s.context->CopyResource(s.logic_copy.resource.p,s.color.p);s.context->PSSetShaderResources(16,1,&s.logic_copy.view.p);
        }
        s.uniforms(p,st,cmd.prim,overlay_scale,overlay_offset);require(p.program.bind(s.context,error),error);
        const UINT stride[]={packed_stride(draw_ntex(st)),0},offset[]={0,0};ID3D11Buffer* vbo[]={s.vertices.resource.p,s.zero_coordinates.p};
        s.context->IASetVertexBuffers(0,2,vbo,stride,offset);s.context->IASetIndexBuffer(s.indices.resource.p,DXGI_FORMAT_R32_UINT,0);
        s.context->IASetInputLayout(p.layout[draw_ntex(st)].p);
        s.context->IASetPrimitiveTopology(cmd.prim==0?D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST:cmd.prim==1?D3D11_PRIMITIVE_TOPOLOGY_LINELIST:D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
        if(cmd.prim==2){
            const std::array<float,4> params={1.f/width(),1.f/height(),0,0};
            if(params!=s.last_expansion){upload(s.device,s.context,s.expansion_params,D3D11_BIND_CONSTANT_BUFFER,{reinterpret_cast<const uint8_t*>(params.data()),sizeof(params)});s.last_expansion=params;}
            s.context->GSSetConstantBuffers(1,1,&s.expansion_params.resource.p);s.context->GSSetShader(s.expansion.p,nullptr,0);
        }else s.context->GSSetShader(nullptr,nullptr,0);
        s.context->RSSetState(s.raster_states.at(cull).p);s.context->OMSetDepthStencilState(s.depth_states.at(bp[0x40]&31).p,0);
        s.context->OMSetBlendState(s.blend_states.at(std::make_tuple(bp[0x41],bp[0x42],bp[0x43])).p,nullptr,~0u);
        const int xoff=int(bp[0x59]&1023)*2,yoff=int((bp[0x59]>>10)&1023)*2;
        const int x0=std::max(0,int((bp[0x20]>>12)&2047)-xoff),y0=std::max(0,int(bp[0x20]&2047)-yoff);
        const int x1=std::max(x0,std::min(int(efb_w),int((bp[0x21]>>12)&2047)-xoff+1)),y1=std::max(y0,std::min(int(efb_h),int(bp[0x21]&2047)-yoff+1));
        const float left=(x0-efb_w*.5f)*overlay_scale+efb_w*.5f+efb_w*.5f*overlay_offset;
        const float right=(x1-efb_w*.5f)*overlay_scale+efb_w*.5f+efb_w*.5f*overlay_offset;
        const LONG sx0=std::max(0,int(std::floor(left*s.scale))),sx1=std::min(int(efb_w*s.scale),int(std::ceil(right*s.scale)));
        const D3D11_RECT scissor={sx0,LONG((efb_h-y1)*s.scale),std::max(sx0,sx1),LONG((efb_h-y0)*s.scale)};s.context->RSSetScissorRects(1,&scissor);
        D3D11_VIEWPORT viewport{};viewport.Width=float(efb_w*s.scale);viewport.Height=float(efb_h*s.scale);viewport.MaxDepth=1;s.context->RSSetViewports(1,&viewport);
        s.context->OMSetRenderTargets(1,&s.target.p,s.depth_target.p);s.context->DrawIndexed(cmd.count,cmd.first,cmd.base_vertex);
        return true;
    }catch(const std::exception& failure){error=failure.what();return false;}
}
void D3D11Renderer::clear(uint32_t rgba,float depth){if(!state)return;const float color[]={float(rgba>>24)/255,float((rgba>>16)&255)/255,float((rgba>>8)&255)/255,float(rgba&255)/255};
    state->context->ClearRenderTargetView(state->target.p,color);state->context->ClearDepthStencilView(state->depth_target.p,D3D11_CLEAR_DEPTH,depth,0);}
bool D3D11Renderer::read_color(std::vector<uint8_t>& output,std::string& error){
    error.clear();try{require(state!=nullptr,"Native GX backend is not initialized");auto& s=*state;s.context->OMSetRenderTargets(0,nullptr,nullptr);s.context->CopyResource(s.staging.p,s.color.p);
        std::vector<uint8_t> data(size_t(width())*height()*4);D3D11_MAPPED_SUBRESOURCE mapped{};checked(s.context->Map(s.staging.p,0,D3D11_MAP_READ,0,&mapped),"Read native GX EFB pixels");
        for(unsigned y=0;y<height();++y)std::memcpy(data.data()+size_t(y)*width()*4,static_cast<const uint8_t*>(mapped.pData)+y*mapped.RowPitch,width()*4);
        s.context->Unmap(s.staging.p,0);output=std::move(data);return true;
    }catch(const std::exception& failure){error=failure.what();return false;}
}
bool D3D11Renderer::copy(const EfbCopyCmd& copy,std::vector<uint8_t>* writeback,std::string& error){
    error.clear();try{require(state!=nullptr,"Native GX backend is not initialized");auto& s=*state;auto* c=s.context;
        require(copy.src_w && copy.src_h && copy.src_x<efb_w && copy.src_y<efb_h && copy.src_w<=efb_w-copy.src_x && copy.src_h<=efb_h-copy.src_y,"Native GX copy outside EFB");
        if(!copy.to_xfb && copy.tex_id){
            require(copy.dst_w && copy.dst_h && copy.dst_w<=16384/s.scale && copy.dst_h<=16384/s.scale,"Invalid native GX copy dimensions");
            const auto w=copy.dst_w*s.scale,h=copy.dst_h*s.scale;auto found=s.textures.find(copy.tex_id);
            if(found==s.textures.end() || !found->second.efb || found->second.width!=w || found->second.height!=h || found->second.lease.use_count()>1)
                found=s.textures.insert_or_assign(copy.tex_id,s.target_texture(w,h)).first;
            auto& texture=found->second;texture.depth=copy.depth;
            const D3D11_RECT rect={0,0,LONG(w),LONG(h)};s.screen_begin(texture.target.p,nullptr,w,h,15,false,rect);
            auto& p=s.copy_pipeline;const float source[]={float(copy.src_x)/efb_w,float(copy.src_y)/efb_h,float(copy.src_x+copy.src_w)/efb_w,float(copy.src_y+copy.src_h)/efb_h};
            p.floats("u_rect",source);p.integer("u_mode",int32_t(copy.format)|(copy.depth?32:0));
            ID3D11ShaderResourceView* views[16]={};ID3D11SamplerState* samplers[16]={};
            for(const auto& [name,slot]:p.samplers){require(slot<16,"Native copy sampler outside range");
                views[slot]=name=="u_src"?s.color_srv.p:s.depth_srv.p;samplers[slot]=name=="u_src"?s.linear.p:s.nearest.p;}
            c->PSSetShaderResources(0,16,views);c->PSSetSamplers(0,16,samplers);require(p.program.bind(c,error),error);c->Draw(4,0);
            if(writeback){auto pixels=s.read(texture.resource.p);require(encode_copy(copy,s.scale,pixels,*writeback),"Unsupported native GX CPU copy encoding");}
        }
        if(copy.clear){
            const D3D11_RECT rect={LONG(copy.src_x*s.scale),LONG((efb_h-copy.src_y-copy.src_h)*s.scale),LONG((copy.src_x+copy.src_w)*s.scale),LONG((efb_h-copy.src_y)*s.scale)};
            const unsigned mask=(copy.clear_color?7:0)|(copy.clear_alpha?8:0);s.screen_begin(s.target.p,s.depth_target.p,width(),height(),mask,copy.clear_z,rect);
            auto& p=s.clear_pipeline;const float color[]={float(copy.clear_rgba>>24)/255,float((copy.clear_rgba>>16)&255)/255,float((copy.clear_rgba>>8)&255)/255,float(copy.clear_rgba&255)/255};
            const float depth[]={float(copy.clear_z_value)/16777215.f};p.floats("u_clear",color);p.floats("u_clear_depth",depth);require(p.program.bind(c,error),error);c->Draw(4,0);
        }
        ID3D11ShaderResourceView* empty[16]={};c->PSSetShaderResources(0,16,empty);return true;
    }catch(const std::exception& failure){error=failure.what();return false;}
}
bool D3D11Renderer::read_texture(uint32_t id,std::vector<uint8_t>& output,std::string& error){
    error.clear();try{require(state!=nullptr,"Native GX backend is not initialized");auto& s=*state;auto found=s.textures.find(id);require(found!=s.textures.end(),"Native GX texture not found");
        require(found->second.levels==1,"Native GX diagnostic read requires one mip");auto data=s.read(found->second.resource.p);output=std::move(data);return true;
    }catch(const std::exception& failure){error=failure.what();return false;}
}
bool D3D11Renderer::scanout(const EfbCopyCmd& copy,unsigned display_height,float aspect,bool publish,bool& complete,std::string& error){
    error.clear();complete=false;try{require(state!=nullptr,"Native GX backend is not initialized");auto& s=*state;auto* c=s.context;
        require(std::isfinite(aspect) && aspect>0 && aspect<=16,"Invalid native scanout aspect");
        require(copy.src_w && copy.src_h && copy.src_x<efb_w && copy.src_y<efb_h && copy.src_w<=efb_w-copy.src_x && copy.src_h<=efb_h-copy.src_y,"Native scanout outside EFB");
        ID3D11ShaderResourceView* source=s.color_srv.p;unsigned tex_w=efb_w,tex_h=efb_h,rx=copy.src_x,ry=copy.src_y,rw=copy.src_w,rh=copy.src_h;
        if(display_height && copy.dst_h<display_height){
            require(copy.dst_w && copy.dst_h && display_height<=528 && copy.dst_w<=efb_w,"Invalid native partial XFB");
            const uint32_t stride=copy.dst_w*2;auto found=std::find_if(s.displays.begin(),s.displays.end(),[&](const auto& x){return x.stride==stride && x.height==display_height && copy.dest>=x.base && uint64_t(copy.dest)<uint64_t(x.base)+uint64_t(stride)*display_height;});
            if(found==s.displays.end()){
                Impl::Xfb x;x.base=copy.dest;x.stride=stride;x.height=display_height;x.written.assign(display_height,0);x.texture=s.target_texture(copy.dst_w*s.scale,display_height*s.scale);
                const float black[]={0,0,0,1};c->ClearRenderTargetView(x.texture.target.p,black);s.displays.push_back(std::move(x));
                if(s.displays.size()>8)s.displays.erase(s.displays.begin());found=std::prev(s.displays.end());
            }
            auto& x=*found;const unsigned row=(copy.dest-x.base)/stride,rows=std::min(copy.dst_h,x.height-row),w=copy.dst_w*s.scale,h=x.height*s.scale;
            if(x.texture.lease.use_count()>1){auto replacement=s.target_texture(w,h);s.unbind();c->CopyResource(replacement.resource.p,x.texture.resource.p);x.texture=std::move(replacement);}
            const D3D11_RECT rect={0,LONG((x.height-row-rows)*s.scale),LONG(w),LONG((x.height-row)*s.scale)};s.screen_begin(x.texture.target.p,nullptr,w,h,15,false,rect);
            D3D11_VIEWPORT vp{};vp.TopLeftY=float((x.height-row-rows)*s.scale);vp.Width=float(w);vp.Height=float(rows*s.scale);vp.MaxDepth=1;c->RSSetViewports(1,&vp);
            auto& p=s.copy_pipeline;const float region[]={float(copy.src_x)/efb_w,float(copy.src_y+copy.src_h)/efb_h,float(copy.src_x+copy.src_w)/efb_w,float(copy.src_y)/efb_h};
            p.floats("u_rect",region);p.integer("u_mode",6);ID3D11ShaderResourceView* views[16]={};ID3D11SamplerState* filters[16]={};
            for(const auto& [name,slot]:p.samplers){views[slot]=name=="u_src"?s.color_srv.p:s.depth_srv.p;filters[slot]=s.nearest.p;}
            c->PSSetShaderResources(0,16,views);c->PSSetSamplers(0,16,filters);require(p.program.bind(c,error),error);c->Draw(4,0);
            for(unsigned r=row;r<row+rows;++r)if(!x.written[r]){x.written[r]=1;++x.written_rows;}
            if(x.written_rows<x.height)return true;std::fill(x.written.begin(),x.written.end(),0);x.written_rows=0;
            source=x.texture.view.p;tex_w=copy.dst_w;tex_h=x.height;rx=ry=0;rw=tex_w;rh=tex_h;
        }
        complete=true;if(!publish)return true;
        const auto w=s.graphics->width(),h=s.graphics->height();require(w && h && s.graphics->window_target(),"Native window target unavailable");
        const float black[]={0,0,0,1};c->ClearRenderTargetView(s.graphics->window_target(),black);
        const D3D11_RECT rect={0,0,LONG(w),LONG(h)};s.screen_begin(s.graphics->window_target(),nullptr,w,h,15,false,rect);
        unsigned vw=w,vh=unsigned(std::lround(w/aspect));if(vh>h){vh=h;vw=unsigned(std::lround(h*aspect));}
        D3D11_VIEWPORT viewport{};viewport.TopLeftX=float((w-vw)/2);viewport.TopLeftY=float((h-vh)/2);viewport.Width=float(vw);viewport.Height=float(vh);viewport.MaxDepth=1;c->RSSetViewports(1,&viewport);
        auto& p=s.scanout_pipeline;const float region[]={float(rx)/tex_w,float(ry+rh)/tex_h,float(rx+rw)/tex_w,float(ry)/tex_h};p.floats("u_rect",region);
        const auto slot=p.samplers.at("u_src");c->PSSetShaderResources(slot,1,&source);c->PSSetSamplers(slot,1,&s.linear.p);require(p.program.bind(c,error),error);c->Draw(4,0);return true;
    }catch(const std::exception& failure){error=failure.what();return false;}
}
std::shared_ptr<const D3D11Renderer::Snapshot> D3D11Renderer::capture(bool verify,std::string& error){
    error.clear();try{require(state!=nullptr,"Native GX backend is not initialized");auto& s=*state;s.unbind();auto snapshot=std::make_shared<Snapshot>();
        snapshot->color=s.clone(s.color.p);snapshot->depth=s.clone(s.depth.p);snapshot->textures=s.textures;snapshot->displays=s.displays;snapshot->dead=s.dead_pending;
        std::copy(std::begin(s.zplane),std::end(s.zplane),snapshot->zplane.begin());snapshot->generation=s.generation;
        if(verify)snapshot->oracle=std::make_unique<const std::vector<ResourceImage>>(snapshot->images(s));return snapshot;
    }catch(const std::exception& failure){error=failure.what();return {};}
}
bool D3D11Renderer::restore(const Snapshot& snapshot,std::string& error){
    error.clear();try{require(state!=nullptr && snapshot.generation==state->generation,"Native GX checkpoint belongs to a different renderer generation");auto& s=*state;s.unbind();
        if(snapshot.oracle)require(snapshot.images(s)==*snapshot.oracle,"Saved native GX texture was overwritten");
        s.context->CopyResource(s.color.p,snapshot.color.p);s.context->CopyResource(s.depth.p,snapshot.depth.p);s.textures=snapshot.textures;s.displays=snapshot.displays;s.dead_pending=snapshot.dead;
        std::copy(snapshot.zplane.begin(),snapshot.zplane.end(),std::begin(s.zplane));return true;
    }catch(const std::exception& failure){error=failure.what();return false;}
}
bool D3D11Renderer::matches(const Snapshot& snapshot,std::string& error){
    error.clear();try{require(state!=nullptr && snapshot.generation==state->generation,"Native GX checkpoint belongs to a different renderer generation");auto& s=*state;s.unbind();
        const auto saved=snapshot.images(s);if(snapshot.oracle)require(saved==*snapshot.oracle,"Saved native GX texture was overwritten");
        if(s.dead_pending!=snapshot.dead || std::memcmp(s.zplane,snapshot.zplane.data(),sizeof(s.zplane)) || s.textures.size()!=snapshot.textures.size() || s.displays.size()!=snapshot.displays.size())return false;
        for(const auto& [id,t]:snapshot.textures){const auto found=s.textures.find(id);if(found==s.textures.end())return false;const auto& current=found->second;
            if(current.width!=t.width || current.height!=t.height || current.levels!=t.levels || current.hash!=t.hash || current.efb!=t.efb || current.depth!=t.depth)return false;}
        for(unsigned i=0;i<s.displays.size();++i){const auto& a=s.displays[i];const auto& b=snapshot.displays[i];if(a.base!=b.base || a.stride!=b.stride || a.height!=b.height || a.written_rows!=b.written_rows || a.written!=b.written)return false;}
        Snapshot current;current.color={nullptr,s.color.p};current.depth={nullptr,s.depth.p};current.textures=s.textures;current.displays=s.displays;return current.images(s)==saved;
    }catch(const std::exception& failure){error=failure.what();return false;}
}
ID3D11ShaderResourceView* D3D11Renderer::color_view()const{return state?state->color_srv.p:nullptr;}
unsigned D3D11Renderer::width()const{return state?efb_w*state->scale:0;}
unsigned D3D11Renderer::height()const{return state?efb_h*state->scale:0;}
bool D3D11Renderer::native_logic_operations()const{return state && state->native_logic;}
void D3D11Renderer::set_texture_loader(TextureLoader loader){if(state)state->texture_loader=loader;}
uint64_t D3D11Renderer::texture_hash(uint32_t id)const{if(!state)return 0;const auto it=state->textures.find(id);return it==state->textures.end()?0:it->second.hash;}
void D3D11Renderer::open_shader_cache(const std::string& native_path,const std::string& opengl_path){
    if(!state)return;auto& s=*state;std::string error;
    if(!s.shader_cache->open(native_path,error))std::fprintf(stderr,"[d3d11-shaders] Ignoring cache: %s\n",error.c_str());
    s.warm_keys=s.shader_cache->keys();s.warm_done=0;std::vector<ShaderKey> keys;
    if(!desktop_d3d11::read_opengl_shader_keys(opengl_path,keys,error))std::fprintf(stderr,"[d3d11-shaders] Ignoring OpenGL keys: %s\n",error.c_str());
    std::unordered_map<ShaderKey,bool,ShaderKeyHash> known;for(const auto& k:s.warm_keys)if(k.options.flip_vertex_y && k.options.logic_operation<0)known.emplace(k.shader,true);
    for(const auto& key:keys)if(known.emplace(key,true).second)s.warm_keys.push_back({key,{true}});
}
bool D3D11Renderer::warm_shader_cache(double budget_ms,int& done,int& total,std::string& error){
    error.clear();done=total=0;try{
        require(state!=nullptr,"Native GX backend is not initialized");auto& s=*state;const auto start=std::chrono::steady_clock::now();
        while(s.warm_done<s.warm_keys.size()){
            const auto& k=s.warm_keys[s.warm_done++];if(!k.options.flip_vertex_y)continue;
            auto& programs=k.options.logic_operation<0?s.programs:s.logic_programs[k.options.logic_operation];
            if(!programs.contains(k.shader))programs.emplace(k.shader,std::make_unique<Pipeline>(s.device,*s.shader_cache,k.shader,k.options.logic_operation));
            if(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()>=budget_ms)break;
        }
        done=int(s.warm_done);total=int(s.warm_keys.size());if(done==total){
            std::fprintf(stderr,"[d3d11-shaders] Warmed %d pipelines (%llu cached, %llu compiled)\n",total,(unsigned long long)s.shader_cache->cache_hits(),(unsigned long long)s.shader_cache->compiled());
            if(!s.shader_cache->save(error))std::fprintf(stderr,"[d3d11-shaders] Cache save: %s\n",error.c_str());error.clear();return true;
        }return false;
    }catch(const std::exception& e){error=e.what();return true;}
}
bool D3D11Renderer::save_shader_cache(std::string& error){error.clear();return !state || state->shader_cache->save(error);}
}
#endif
