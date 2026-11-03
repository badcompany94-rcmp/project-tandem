#include "d3d11_device.h"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <cstdio>
#include <utility>

namespace desktop_d3d11 {
namespace {
template<class T>struct Com {
    T* value=nullptr;
    ~Com(){reset();}
    Com()=default;Com(const Com&)=delete;Com& operator=(const Com&)=delete;
    T* operator->()const{return value;}
    void reset(){if(value)value->Release();value=nullptr;}
    T** out(){reset();return &value;}
};
bool failed(HRESULT result,const char* operation,std::string& error){
    if(SUCCEEDED(result))return false;
    char code[24];std::snprintf(code,sizeof(code),"0x%08lX",static_cast<unsigned long>(result));
    error=std::string(operation)+" failed ("+code+").";return true;
}
std::string utf8(const wchar_t* text){
    const int count=WideCharToMultiByte(CP_UTF8,0,text,-1,nullptr,0,nullptr,nullptr);
    if(count<=1)return "Unknown adapter";
    std::string out(size_t(count),0);
    WideCharToMultiByte(CP_UTF8,0,text,-1,out.data(),count,nullptr,nullptr);out.pop_back();return out;
}
}
struct Device::Impl {
    Com<ID3D11Device> device;
    Com<ID3D11DeviceContext> context;
    Com<IDXGISwapChain> swap;
    Com<ID3D11RenderTargetView> target;
    DeviceInfo info;
    unsigned width=0,height=0;
    ~Impl(){if(context.value){context->ClearState();context->Flush();}}
    bool target_view(std::string& error){
        Com<ID3D11Texture2D> back;
        if(failed(swap->GetBuffer(0,__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(back.out())),"Get back buffer",error))return false;
        return !failed(device->CreateRenderTargetView(back.value,nullptr,target.out()),"Create window render target",error);
    }
};
Device::Device():state(std::make_unique<Impl>()){}
Device::~Device()=default;
bool Device::initialize(void* window,unsigned width,unsigned height,std::string& error){
    error.clear();
    if(!window || !IsWindow(static_cast<HWND>(window)) || !width || !height || width>16384 || height>16384){
        error="Invalid Direct3D window or dimensions.";return false;
    }
    // Build a complete replacement before retiring a previous valid device.
    auto next=std::make_unique<Impl>();D3D_FEATURE_LEVEL level{};
    const D3D_FEATURE_LEVEL levels[]={D3D_FEATURE_LEVEL_11_0};
    const auto created=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT,levels,1,D3D11_SDK_VERSION,
        next->device.out(),&level,next->context.out());
    if(failed(created,"Create hardware Direct3D 11 device",error))return false;
    next->info.feature_level=unsigned(level);
    Com<IDXGIDevice> dxgi;Com<IDXGIAdapter> adapter;Com<IDXGIFactory> factory;
    if(failed(next->device->QueryInterface(__uuidof(IDXGIDevice),reinterpret_cast<void**>(dxgi.out())),"Query DXGI device",error) ||
       failed(dxgi->GetAdapter(adapter.out()),"Get graphics adapter",error) ||
       failed(adapter->GetParent(__uuidof(IDXGIFactory),reinterpret_cast<void**>(factory.out())),"Get DXGI factory",error))return false;
    DXGI_ADAPTER_DESC description{};
    if(failed(adapter->GetDesc(&description),"Get adapter description",error))return false;
    next->info.adapter=utf8(description.Description);next->info.vendor=description.VendorId;next->info.device=description.DeviceId;
    Com<IDXGIFactory2> modern;
    if(SUCCEEDED(factory->QueryInterface(__uuidof(IDXGIFactory2),reinterpret_cast<void**>(modern.out())))){
        DXGI_SWAP_CHAIN_DESC1 desc{};desc.Width=width;desc.Height=height;
        desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;
        desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;desc.BufferCount=2;
        desc.Scaling=DXGI_SCALING_STRETCH;desc.AlphaMode=DXGI_ALPHA_MODE_IGNORE;
        // Prefer modern presentation, retaining compatibility with older drivers.
        for(auto effect:{DXGI_SWAP_EFFECT_FLIP_DISCARD,DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL}){
            desc.SwapEffect=effect;Com<IDXGISwapChain1> swap;
            if(SUCCEEDED(modern->CreateSwapChainForHwnd(next->device.value,static_cast<HWND>(window),&desc,nullptr,nullptr,swap.out())) &&
               SUCCEEDED(swap->QueryInterface(__uuidof(IDXGISwapChain),reinterpret_cast<void**>(next->swap.out())))){
                next->info.flip_presentation=true;break;
            }
        }
    }
    if(!next->swap.value){
        DXGI_SWAP_CHAIN_DESC desc{};desc.BufferDesc.Width=width;desc.BufferDesc.Height=height;
        desc.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;
        desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;desc.BufferCount=2;
        desc.OutputWindow=static_cast<HWND>(window);desc.Windowed=TRUE;desc.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
        if(failed(factory->CreateSwapChain(next->device.value,&desc,next->swap.out()),"Create compatible swap chain",error))return false;
    }
    if(failed(factory->MakeWindowAssociation(static_cast<HWND>(window),DXGI_MWA_NO_ALT_ENTER),"Configure window association",error) ||
       !next->target_view(error))return false;
    next->width=width;next->height=height;state=std::move(next);return true;
}
bool Device::resize(unsigned width,unsigned height,std::string& error){
    error.clear();if(!state->swap.value){error="Direct3D device is not initialized.";return false;}
    if(!width || !height)return true; // Minimized windows have no drawable surface.
    if(width>16384 || height>16384){error="Unsupported drawable dimensions.";return false;}
    if(width==state->width && height==state->height && state->target.value)return true;
    state->context->OMSetRenderTargets(0,nullptr,nullptr);state->target.reset();
    if(failed(state->swap->ResizeBuffers(0,width,height,DXGI_FORMAT_UNKNOWN,0),"Resize swap chain",error))return false;
    state->width=width;state->height=height;return state->target_view(error);
}
bool Device::present(bool vsync,bool& occluded,std::string& error){
    error.clear();occluded=false;
    if(!state->swap.value){error="Direct3D device is not initialized.";return false;}
    const auto result=state->swap->Present(vsync?1:0,0);
    if(result==DXGI_STATUS_OCCLUDED){occluded=true;return true;}
    if(failed(result,"Present Direct3D frame",error)){
        char code[24];std::snprintf(code,sizeof(code),"0x%08lX",static_cast<unsigned long>(state->device->GetDeviceRemovedReason()));
        error+=" Device status: "+std::string(code)+".";return false;
    }
    return true;
}
ID3D11Device* Device::device()const{return state->device.value;}
ID3D11DeviceContext* Device::context()const{return state->context.value;}
ID3D11RenderTargetView* Device::window_target()const{return state->target.value;}
const DeviceInfo& Device::info()const{return state->info;}
unsigned Device::width()const{return state->width;}
unsigned Device::height()const{return state->height;}
}
#endif
