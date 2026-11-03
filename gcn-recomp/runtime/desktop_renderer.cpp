#include "desktop_renderer.h"
#include <SDL.h>
#include <memory>
#if defined(_WIN32) && defined(TANDEM_NATIVE_D3D11)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <SDL_syswm.h>
#include <d3d11.h>
#include "graphics/d3d11_device.h"
#include "gx/render_d3d11.h"
#endif
namespace desktop_renderer {
namespace {
Api preferred=Api::OpenGL,active=Api::OpenGL;std::string config;bool vsync=false;
#if defined(_WIN32) && defined(TANDEM_NATIVE_D3D11)
std::unique_ptr<desktop_d3d11::Device> native_device;
std::unique_ptr<gx::D3D11Renderer> native_renderer;
#endif
}
bool supported(){
#if defined(_WIN32) && defined(TANDEM_NATIVE_D3D11)
    return true;
#else
    return false;
#endif
}
bool load(const std::string& path,std::string& error){config=path;return read_preference(config,preferred,error);}
Api preference(){return preferred;}Api running(){return active;}
bool preview(Api api,std::string& error){error.clear();if(api!=Api::OpenGL && api!=Api::Direct3D11){error="Invalid graphics renderer.";return false;}preferred=api;return true;}
bool save(std::string& error){return write_preference(config,preferred,error);}
bool initialize_native(SDL_Window* window,int scale,std::string& error){
    error.clear();
#if defined(_WIN32) && defined(TANDEM_NATIVE_D3D11)
    SDL_SysWMinfo wm{};SDL_VERSION(&wm.version);if(!window || !SDL_GetWindowWMInfo(window,&wm)){error="Cannot obtain the native game window.";return false;}
    int w=0,h=0;SDL_GetWindowSizeInPixels(window,&w,&h);if(w<=0 || h<=0){error="The native game window has no drawable size.";return false;}
    auto graphics=std::make_unique<desktop_d3d11::Device>();if(!graphics->initialize(wm.info.win.window,unsigned(w),unsigned(h),error))return false;
    auto renderer=std::make_unique<gx::D3D11Renderer>();if(!renderer->initialize(*graphics,scale,error))return false;
    native_renderer.reset();native_device=std::move(graphics);native_renderer=std::move(renderer);active=Api::Direct3D11;return true;
#else
    (void)window;(void)scale;error="This build does not include Direct3D 11.";return false;
#endif
}
void shutdown(){
#if defined(_WIN32) && defined(TANDEM_NATIVE_D3D11)
    native_renderer.reset();native_device.reset();
#endif
    active=Api::OpenGL;
}
desktop_d3d11::Device* device(){
#if defined(_WIN32) && defined(TANDEM_NATIVE_D3D11)
    return native_device.get();
#else
    return nullptr;
#endif
}
gx::D3D11Renderer* renderer(){
#if defined(_WIN32) && defined(TANDEM_NATIVE_D3D11)
    return native_renderer.get();
#else
    return nullptr;
#endif
}
void drawable_size(SDL_Window* window,int& w,int& h){
#if defined(_WIN32) && defined(TANDEM_NATIVE_D3D11)
    if(active==Api::Direct3D11){SDL_GetWindowSizeInPixels(window,&w,&h);return;}
#endif
    SDL_GL_GetDrawableSize(window,&w,&h);
}
bool resize(SDL_Window* window,std::string& error){
    error.clear();
#if defined(_WIN32) && defined(TANDEM_NATIVE_D3D11)
    if(active==Api::Direct3D11){int w=0,h=0;drawable_size(window,w,h);return native_device->resize(unsigned(w>0?w:0),unsigned(h>0?h:0),error);}
#else
    (void)window;
#endif
    return true;
}
bool present(SDL_Window* window,std::string& error){
    error.clear();
#if defined(_WIN32) && defined(TANDEM_NATIVE_D3D11)
    if(active==Api::Direct3D11){bool occluded=false;return native_device->present(vsync,occluded,error);}
#endif
    SDL_GL_SwapWindow(window);return true;
}
bool apply_vsync(bool enabled,std::string& error){
    error.clear();if(active==Api::Direct3D11){vsync=enabled;return true;}
    if(SDL_GL_GetSwapInterval()!=int(enabled) && SDL_GL_SetSwapInterval(int(enabled))<0){error=std::string("Cannot change VSync: ")+SDL_GetError();return false;}return true;
}
int swap_interval(){return active==Api::Direct3D11?int(vsync):SDL_GL_GetSwapInterval();}
void bind_window_target(){
#if defined(_WIN32) && defined(TANDEM_NATIVE_D3D11)
    if(active==Api::Direct3D11){auto* target=native_device->window_target();native_device->context()->OMSetRenderTargets(1,&target,nullptr);}
#endif
}
bool cache_window(bool restore,std::string& error){
    error.clear();
#if defined(_WIN32) && defined(TANDEM_NATIVE_D3D11)
    if(active==Api::Direct3D11)return native_device->cache_window(restore,error);
#else
    (void)restore;
#endif
    return false;
}
bool window_pixels(std::vector<unsigned char>& pixels,std::string& error){
    error.clear();
#if defined(_WIN32) && defined(TANDEM_NATIVE_D3D11)
    if(active==Api::Direct3D11)return native_device->window_pixels(pixels,error);
#else
    (void)pixels;
#endif
    error="Native window readback requires Direct3D 11.";return false;
}
}
