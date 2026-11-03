#pragma once
#ifdef _WIN32
#include <memory>
#include <string>
struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11RenderTargetView;

namespace desktop_d3d11 {
struct DeviceInfo {
    std::string adapter;
    unsigned vendor=0,device=0;
    unsigned feature_level=0;
    bool flip_presentation=false;
};
// Render-thread-owned native Direct3D resources. No game state or network work.
class Device {
public:
    Device();
    ~Device();
    Device(const Device&)=delete;
    Device& operator=(const Device&)=delete;
    bool initialize(void* window,unsigned width,unsigned height,std::string& error);
    bool resize(unsigned width,unsigned height,std::string& error);
    bool present(bool vsync,bool& occluded,std::string& error);
    ID3D11Device* device()const;
    ID3D11DeviceContext* context()const;
    ID3D11RenderTargetView* window_target()const;
    const DeviceInfo& info()const;
    unsigned width()const;
    unsigned height()const;
private:
    struct Impl;
    std::unique_ptr<Impl> state;
};
}
#endif
