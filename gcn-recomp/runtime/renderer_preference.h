#pragma once
#include <string>
namespace desktop_renderer {
enum class Api {OpenGL=0,Direct3D11=1};
// Read before creating a window/context. Missing or invalid settings select
// OpenGL; reading never writes or migrates the user's other display settings.
bool read_preference(const std::string& utf8_path,Api& result,std::string& error);
bool write_preference(const std::string& utf8_path,Api,std::string& error);
const char* label(Api);
}
