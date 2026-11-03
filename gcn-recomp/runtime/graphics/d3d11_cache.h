#pragma once
#ifdef _WIN32
#include "d3d11_shaders.h"
#include "gx/shadergen.h"
#include <memory>
namespace desktop_d3d11 {
struct CachedShaderKey {gx::ShaderKey shader;ShaderOptions options;};
// Read keys only. OpenGL program binaries and driver identity are never used
// by Direct3D, and the original cache is never modified.
bool read_opengl_shader_keys(const std::string&,std::vector<gx::ShaderKey>&,std::string& error);
// CPU-only, bounded, checksummed native bytecode cache. Never edits GL caches.
class ShaderCache {
public:
    ShaderCache();~ShaderCache();
    bool open(const std::string& utf8_path,std::string& error);
    bool get(const gx::ShaderKey&,ShaderOptions,CompiledProgram&,std::string& error);
    void invalidate(const gx::ShaderKey&,ShaderOptions);
    bool save(std::string& error);
    std::vector<CachedShaderKey> keys()const;
    uint64_t cache_hits()const;uint64_t compiled()const;
private:
    struct Impl;std::unique_ptr<Impl> state;
};
}
#endif
