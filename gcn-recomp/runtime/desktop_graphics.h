#pragma once
#include "diagnostic_log.h"
#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>

// Accessed only by the desktop render/UI thread. Contains no local file paths.
namespace desktop_graphics {
struct Device {std::string vendor,renderer,version,shading,platform,sdl;std::string api="OpenGL";};
struct Output {
    int window_w=0,window_h=0,drawable_w=0,drawable_h=0,scale=0,swap_interval=0,refresh_hz=0;
    bool operator==(const Output&) const=default;
};
inline Device device;
inline Output output;
inline std::string clean(const char* value) {
    std::string result=value?value:"Unavailable";
    if(result.size()>512)result.resize(512);
    for(char& c:result)if(static_cast<unsigned char>(c)<32 || c==127)c=' ';
    return result;
}
inline bool software_renderer() {
    auto name=device.renderer;
    std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c){return char(std::tolower(c));});
    for(const auto* token:{"llvmpipe","softpipe","swrast","software rasterizer","gdi generic","swiftshader"})
        if(name.find(token)!=std::string::npos)return true;
    return false;
}
inline std::string report() {
    std::ostringstream text;
    text<<"Graphics API: "<<device.api<<"\nGPU vendor: "<<device.vendor<<"\nGPU renderer: "<<device.renderer
        <<"\n"<<(device.api=="OpenGL"?"OpenGL / driver: ":"API details: ")<<device.version<<"\n"<<(device.api=="OpenGL"?"GLSL: ":"Shader model: ")<<device.shading
        <<"\nPlatform: "<<device.platform<<"\nSDL: "<<device.sdl
        <<"\nSoftware renderer detected: "<<(software_renderer()?"yes":"no")
        <<"\nWindow: "<<output.window_w<<'x'<<output.window_h
        <<"\nDrawable: "<<output.drawable_w<<'x'<<output.drawable_h
        <<"\nActive internal scale: "<<(output.scale>0?std::to_string(output.scale)+"x":"Not yet presented")
        <<"\nVSync interval: "<<output.swap_interval<<"\nInterpolation target: "<<output.refresh_hz<<" Hz\n";
    return text.str();
}
inline void initialize(const char* vendor,const char* renderer,const char* version,const char* shading,const char* platform,const char* sdl,const char* api="OpenGL") {
    device={clean(vendor),clean(renderer),clean(version),clean(shading),clean(platform),clean(sdl),clean(api)};
    output={};
    if(device.api=="OpenGL")diagnostic_log("[graphics-device] api=OpenGL vendor=%s renderer=%s version=%s glsl=%s platform=%s SDL=%s known_software=%d\n",
        device.vendor.c_str(),device.renderer.c_str(),device.version.c_str(),device.shading.c_str(),device.platform.c_str(),device.sdl.c_str(),software_renderer());
    else diagnostic_log("[graphics-device] api=Direct3D11 vendor=%s renderer=%s version=%s shading=%s platform=%s SDL=%s known_software=%d\n",
        device.vendor.c_str(),device.renderer.c_str(),device.version.c_str(),device.shading.c_str(),device.platform.c_str(),device.sdl.c_str(),software_renderer());
}
inline void update(Output next) {
    if(next==output)return;
    output=next;
    diagnostic_log("[graphics-output] window=%dx%d drawable=%dx%d internal_scale=%d vsync_interval=%d interpolation_hz=%d\n",
        next.window_w,next.window_h,next.drawable_w,next.drawable_h,next.scale,next.swap_interval,next.refresh_hz);
}
}
