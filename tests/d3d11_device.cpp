// Hidden native Direct3D correctness probe, not a game performance benchmark.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include "graphics/d3d11_device.h"
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>
namespace {
template<class T>struct Com {
    T* p=nullptr;~Com(){if(p)p->Release();}T* operator->()const{return p;}
    T** out(){return &p;}
};
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
void checked(HRESULT result,const char* message){if(FAILED(result))throw std::runtime_error(message);}
struct Window {
    HWND handle=nullptr;
    Window(){WNDCLASSW c{};c.lpfnWndProc=DefWindowProcW;c.hInstance=GetModuleHandleW(nullptr);c.lpszClassName=L"TandemD3D11HiddenProbe";
        require(RegisterClassW(&c)!=0,"register hidden window");
        handle=CreateWindowExW(0,c.lpszClassName,L"Project Tandem hidden Direct3D test",WS_OVERLAPPEDWINDOW,0,0,128,128,nullptr,nullptr,c.hInstance,nullptr);
        require(handle!=nullptr,"create hidden window");}
    ~Window(){if(handle)DestroyWindow(handle);UnregisterClassW(L"TandemD3D11HiddenProbe",GetModuleHandleW(nullptr));}
};
struct Constants {float color[4];float params[4];};
}
int main()try{
    Window window;desktop_d3d11::Device graphics;std::string error;
    require(!graphics.initialize(nullptr,64,64,error),"invalid native window was accepted");
    if(!graphics.initialize(window.handle,64,64,error))throw std::runtime_error(error);
    auto* d=graphics.device();auto* c=graphics.context();
    require(d && c && graphics.window_target(),"native device resources missing");
    const char* shader=R"(
cbuffer Constants : register(b0) { float4 color; float4 params; };
float4 vs(uint id:SV_VertexID):SV_Position {
    float2 p[3]={float2(-1,-1),float2(-1,3),float2(3,-1)};
    return float4(p[id],params.x,1);
}
float4 ps():SV_Target { return color; }
)";
    Com<ID3DBlob> vertex,pixel,log;
    auto compile=[&](const char* entry,const char* profile,ID3DBlob** out){
        const auto result=D3DCompile(shader,std::strlen(shader),"tandem-native-probe",nullptr,nullptr,entry,profile,D3DCOMPILE_ENABLE_STRICTNESS,0,out,log.out());
        if(FAILED(result)){if(log.p)std::fwrite(log->GetBufferPointer(),1,log->GetBufferSize(),stderr);throw std::runtime_error("compile native HLSL");}
    };
    compile("vs","vs_5_0",vertex.out());compile("ps","ps_5_0",pixel.out());
    Com<ID3D11VertexShader> vs;Com<ID3D11PixelShader> ps;
    checked(d->CreateVertexShader(vertex->GetBufferPointer(),vertex->GetBufferSize(),nullptr,vs.out()),"create vertex shader");
    checked(d->CreatePixelShader(pixel->GetBufferPointer(),pixel->GetBufferSize(),nullptr,ps.out()),"create pixel shader");
    D3D11_TEXTURE2D_DESC desc{};desc.Width=desc.Height=64;desc.MipLevels=desc.ArraySize=1;
    desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;desc.BindFlags=D3D11_BIND_RENDER_TARGET;
    Com<ID3D11Texture2D> color,snapshot,staging,depth,depth_snapshot;Com<ID3D11RenderTargetView> target;Com<ID3D11DepthStencilView> depth_view;
    checked(d->CreateTexture2D(&desc,nullptr,color.out()),"create native color");
    checked(d->CreateRenderTargetView(color.p,nullptr,target.out()),"create native target");
    desc.BindFlags=0;checked(d->CreateTexture2D(&desc,nullptr,snapshot.out()),"create native snapshot");
    desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    checked(d->CreateTexture2D(&desc,nullptr,staging.out()),"create native readback");
    desc.Usage=D3D11_USAGE_DEFAULT;desc.CPUAccessFlags=0;desc.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;desc.BindFlags=D3D11_BIND_DEPTH_STENCIL;
    checked(d->CreateTexture2D(&desc,nullptr,depth.out()),"create native depth");
    checked(d->CreateDepthStencilView(depth.p,nullptr,depth_view.out()),"create native depth view");
    desc.BindFlags=0;checked(d->CreateTexture2D(&desc,nullptr,depth_snapshot.out()),"create native depth snapshot");
    D3D11_BUFFER_DESC buffer{};buffer.ByteWidth=sizeof(Constants);buffer.Usage=D3D11_USAGE_DEFAULT;buffer.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    Com<ID3D11Buffer> constants;checked(d->CreateBuffer(&buffer,nullptr,constants.out()),"create constants");
    D3D11_DEPTH_STENCIL_DESC depth_state{};depth_state.DepthEnable=TRUE;depth_state.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;depth_state.DepthFunc=D3D11_COMPARISON_LESS;
    Com<ID3D11DepthStencilState> ds;checked(d->CreateDepthStencilState(&depth_state,ds.out()),"create depth state");
    D3D11_RASTERIZER_DESC raster{};raster.FillMode=D3D11_FILL_SOLID;raster.CullMode=D3D11_CULL_NONE;raster.DepthClipEnable=TRUE;
    Com<ID3D11RasterizerState> rs;checked(d->CreateRasterizerState(&raster,rs.out()),"create raster state");
    D3D11_VIEWPORT viewport{};viewport.Width=viewport.Height=64;viewport.MaxDepth=1;
    c->RSSetViewports(1,&viewport);c->RSSetState(rs.p);c->OMSetDepthStencilState(ds.p,0);
    c->VSSetShader(vs.p,nullptr,0);c->PSSetShader(ps.p,nullptr,0);c->VSSetConstantBuffers(0,1,&constants.p);c->PSSetConstantBuffers(0,1,&constants.p);
    c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    const float black[4]={0,0,0,1};c->ClearRenderTargetView(target.p,black);c->ClearDepthStencilView(depth_view.p,D3D11_CLEAR_DEPTH|D3D11_CLEAR_STENCIL,1,0);
    auto draw=[&](Constants values){c->OMSetRenderTargets(1,&target.p,depth_view.p);c->UpdateSubresource(constants.p,0,nullptr,&values,0,0);c->Draw(3,0);};
    auto read=[&]{
        c->OMSetRenderTargets(0,nullptr,nullptr);c->CopyResource(staging.p,color.p);D3D11_MAPPED_SUBRESOURCE mapped{};
        checked(c->Map(staging.p,0,D3D11_MAP_READ,0,&mapped),"map native pixels");
        std::vector<unsigned char> pixels(64*64*4);
        for(unsigned y=0;y<64;++y)std::memcpy(pixels.data()+y*64*4,static_cast<unsigned char*>(mapped.pData)+y*mapped.RowPitch,64*4);
        c->Unmap(staging.p,0);return pixels;
    };
    draw({{0.2f,0.4f,0.8f,1},{0.25f,0,0,0}});const auto original=read();
    for(size_t i=0;i<original.size();i+=4)require(original[i]==51 && original[i+1]==102 && original[i+2]==204 && original[i+3]==255,"native HLSL pixel mismatch");
    c->CopyResource(snapshot.p,color.p);c->CopyResource(depth_snapshot.p,depth.p);
    draw({{1,0,0,1},{0.75f,0,0,0}});require(read()==original,"native depth comparison changed nearer pixels");
    draw({{1,0,0,1},{0.1f,0,0,0}});require(read()!=original,"native nearer draw failed");
    c->CopyResource(color.p,snapshot.p);c->CopyResource(depth.p,depth_snapshot.p);
    require(read()==original,"native resource restoration changed pixels");
    draw({{1,0,0,1},{0.5f,0,0,0}});require(read()==original,"restored depth failed to occlude farther draw");
    require(graphics.resize(0,0,error) && graphics.width()==64,"minimize changed native dimensions");
    require(graphics.resize(96,80,error) && graphics.width()==96 && graphics.height()==80,"resize native drawable");
    c->ClearRenderTargetView(graphics.window_target(),black);bool occluded=false;
    require(graphics.present(false,occluded,error),"native unsynchronized presentation failed");
    require(graphics.present(true,occluded,error),"native synchronized presentation failed");
    require(!graphics.initialize(nullptr,64,64,error) && graphics.device()==d,"failed replacement retired working native device");
    std::printf("PASS native Direct3D11 hardware=%s feature=0x%x flip=%d: HLSL/color/depth/readback/resource restore/resize/presentation\n",
        graphics.info().adapter.c_str(),graphics.info().feature_level,graphics.info().flip_presentation);
    std::puts("Correctness probe only; no game rendering, cross-renderer online or performance acceptance claim.");
    return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}
