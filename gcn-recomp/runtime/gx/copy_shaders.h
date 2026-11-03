#pragma once
#include <string>
namespace gx {
// Shared GX copy semantics. Native backends compile the same source with their
// explicit clip-space convention; the OpenGL default source stays unchanged.
std::string copy_vertex_shader();
std::string copy_pixel_shader();
}
