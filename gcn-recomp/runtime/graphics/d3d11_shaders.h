#pragma once
#ifdef _WIN32
#include <cstdint>
#include <string>
#include <vector>
namespace gx {struct ShaderKey;}
namespace desktop_d3d11 {
enum class UniformScalar {Float,Signed,Unsigned,Boolean};
struct Uniform {
    std::string name;
    uint32_t offset=0,size=0,array_count=1,array_stride=0,matrix_stride=0;
    uint32_t components=1,columns=1;
    UniformScalar scalar=UniformScalar::Float;
    bool operator==(const Uniform&)const=default;
};
struct Sampler {
    std::string name;
    uint32_t binding=0,count=1;
    bool operator==(const Sampler&)const=default;
};
struct CompiledProgram {
    std::vector<uint8_t> vertex,pixel;
    std::vector<Uniform> uniforms;
    std::vector<Sampler> samplers;
    uint32_t uniform_bytes=0;
};
struct ShaderOptions {
    // Offscreen targets may use GL's row-address orientation; window scanout
    // does not. The render backend chooses this explicitly for each program.
    bool flip_vertex_y=false;
};
// CPU/compiler work only. Call outside active race rendering or use a validated
// bytecode cache. Compilation never needs an OpenGL context or driver.
bool compile_program(const std::string& vertex,const std::string& pixel,
    CompiledProgram& result,std::string& error,ShaderOptions options={});
bool compile_gx_program(const gx::ShaderKey&,CompiledProgram&,std::string&,ShaderOptions options={});
}
#endif
