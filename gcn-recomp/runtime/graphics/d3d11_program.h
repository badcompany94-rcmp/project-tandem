#pragma once
#ifdef _WIN32
#include "d3d11_shaders.h"
#include <memory>
#include <span>
struct ID3D11Device;
struct ID3D11DeviceContext;
namespace desktop_d3d11 {
// Native shader objects and a reflected constant buffer. Uniform slots are
// looked up once per cached program, avoiding per-draw string searches.
class Program {
public:
    Program();~Program();
    Program(const Program&)=delete;Program& operator=(const Program&)=delete;
    bool initialize(ID3D11Device*,CompiledProgram,std::string& error);
    int uniform(const std::string& name)const;
    bool floats(int slot,std::span<const float>,std::string& error);
    bool integers(int slot,std::span<const int32_t>,std::string& error);
    bool bind(ID3D11DeviceContext*,std::string& error);
    bool upload_uniforms(ID3D11DeviceContext*,std::string& error);
    std::span<const uint8_t> vertex_code()const;
    uint64_t uniform_uploads()const;
private:
    struct Impl;
    std::unique_ptr<Impl> state;
};
}
#endif
