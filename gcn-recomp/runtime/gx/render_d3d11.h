#pragma once
#ifdef _WIN32
#include "render.h"
#include <memory>
#include <span>
namespace desktop_d3d11 {class Device;}
struct ID3D11ShaderResourceView;
namespace gx {
// Native flat GX backend. Construction and uploads are separate from draw
// submission so startup warming can prepare pipelines before racing.
class D3D11Renderer {
public:
    struct Replacement {unsigned width=0,height=0;std::vector<uint8_t> rgba;};
    using TextureLoader=bool(*)(const TexData&,Replacement&);
    D3D11Renderer();~D3D11Renderer();
    D3D11Renderer(const D3D11Renderer&)=delete;D3D11Renderer& operator=(const D3D11Renderer&)=delete;
    bool initialize(desktop_d3d11::Device&,int scale,std::string& error,bool force_logic_fallback=false);
    void set_texture_loader(TextureLoader);
    // Startup only: bounded work per call, before simulation starts. A missing
    // or invalid disk cache is recoverable; encountered shaders compile normally.
    void open_shader_cache(const std::string& native_path,const std::string& opengl_keys_path);
    bool warm_shader_cache(double budget_ms,int& done,int& total,std::string& error);
    bool save_shader_cache(std::string& error);
    uint64_t texture_hash(uint32_t id)const;
    bool prepare(const Batch&,std::string& error);
    bool draw(const Batch&,const Cmd&,float overlay_scale,float overlay_offset,std::string& error);
    // Native GPU conversion and optional GX tiled bytes for a CPU writeback.
    // The caller owns applying bytes to guest RAM and signaling the frontend.
    bool copy(const EfbCopyCmd&,std::vector<uint8_t>* writeback,std::string& error);
    bool read_texture(uint32_t id,std::vector<uint8_t>& rgba,std::string& error);
    // Assemble partial XFBs and paint a completed scanout into the native window
    // target. Swap/presentation timing belongs to the desktop loop.
    bool scanout(const EfbCopyCmd&,unsigned display_height,float aspect,bool publish,
        bool& complete,std::string& error);
    struct Snapshot;
    std::shared_ptr<const Snapshot> capture(bool verify,std::string& error);
    bool restore(const Snapshot&,std::string& error);
    bool matches(const Snapshot&,std::string& error);
    void clear(uint32_t rgba,float depth);
    bool read_color(std::vector<uint8_t>& rgba,std::string& error);
    ID3D11ShaderResourceView* color_view()const;
    unsigned width()const;unsigned height()const;
    bool native_logic_operations()const;
private:
    struct Impl;std::unique_ptr<Impl> state;
};
}
#endif
