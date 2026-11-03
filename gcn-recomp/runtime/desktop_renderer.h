#pragma once
#include "renderer_preference.h"
#include <vector>
struct SDL_Window;
namespace gx {class D3D11Renderer;}
namespace desktop_d3d11 {class Device;}
namespace desktop_renderer {
bool supported();
bool load(const std::string& config,std::string& error);
Api preference();Api running();
bool preview(Api,std::string& error);
bool save(std::string& error);
bool initialize_native(SDL_Window*,int scale,std::string& error);
void shutdown();
desktop_d3d11::Device* device();gx::D3D11Renderer* renderer();
void drawable_size(SDL_Window*,int& width,int& height);
bool resize(SDL_Window*,std::string& error);
bool present(SDL_Window*,std::string& error);
bool apply_vsync(bool,std::string& error);
int swap_interval();
void bind_window_target();
bool cache_window(bool restore,std::string& error);
bool window_pixels(std::vector<unsigned char>& rgba,std::string& error);
}
