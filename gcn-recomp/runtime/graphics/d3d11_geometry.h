#pragma once
#ifdef _WIN32
#include <cstdint>
#include <span>
#include <string>
#include <vector>
namespace desktop_d3d11 {
enum class Expansion {Points,Lines};
// Construct a native GS from the actual reflected GX vertex-stage interface.
// Constant buffer b1: float2 inverse target size, float line width, padding.
bool compile_geometry(std::span<const uint8_t> vertex,Expansion,
    std::vector<uint8_t>& bytecode,std::string& error);
bool geometry_compatible(std::span<const uint8_t> vertex,
    std::span<const uint8_t> geometry,std::string& error);
}
#endif
