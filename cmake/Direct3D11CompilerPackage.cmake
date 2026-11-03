# Explicit player-package path. Check the archive and ABI/source policy before
# linking it; a damaged/stale package must never silently disable the feature.
file(READ "${TANDEM_D3D11_COMPILER_PACKAGE_DIR}/manifest.json" TANDEM_D3D11_PACKAGE)
string(JSON TANDEM_D3D11_SCHEMA GET "${TANDEM_D3D11_PACKAGE}" schema)
string(JSON TANDEM_D3D11_ABI GET "${TANDEM_D3D11_PACKAGE}" compiler)
string(JSON TANDEM_D3D11_TARGET GET "${TANDEM_D3D11_PACKAGE}" target)
execute_process(COMMAND "${CMAKE_CXX_COMPILER}" -dumpmachine
  OUTPUT_VARIABLE TANDEM_D3D11_ACTUAL_TARGET OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET RESULT_VARIABLE TANDEM_D3D11_TARGET_RESULT)
string(JSON TANDEM_D3D11_ARCHIVE GET "${TANDEM_D3D11_PACKAGE}" library)
string(JSON TANDEM_D3D11_EXPECTED GET "${TANDEM_D3D11_PACKAGE}" sha256)
if(NOT TANDEM_D3D11_SCHEMA EQUAL 1 OR NOT TANDEM_D3D11_TARGET_RESULT EQUAL 0 OR
   NOT TANDEM_D3D11_TARGET STREQUAL TANDEM_D3D11_ACTUAL_TARGET OR
   NOT TANDEM_D3D11_ABI STREQUAL "${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SIZEOF_VOID_P}" OR
   NOT TANDEM_D3D11_ARCHIVE STREQUAL "tandem_d3d11_compiler.a")
  message(FATAL_ERROR "Direct3D compiler package/toolchain mismatch. Repair the package or use the documented source build.")
endif()
file(SHA256 "${TANDEM_D3D11_COMPILER_PACKAGE_DIR}/${TANDEM_D3D11_ARCHIVE}" TANDEM_D3D11_ACTUAL)
if(NOT TANDEM_D3D11_ACTUAL STREQUAL TANDEM_D3D11_EXPECTED)
  message(FATAL_ERROR "Direct3D compiler archive checksum mismatch. Repair this installation.")
endif()
foreach(TANDEM_D3D11_FILE graphics/d3d11_shaders.cpp graphics/d3d11_shaders.h
    graphics/d3d11_cache.cpp graphics/d3d11_cache.h gx/shadergen.h)
  string(JSON TANDEM_D3D11_EXPECTED GET "${TANDEM_D3D11_PACKAGE}" sources "${TANDEM_D3D11_FILE}")
  file(READ "${CMAKE_CURRENT_SOURCE_DIR}/gcn-recomp/runtime/${TANDEM_D3D11_FILE}" TANDEM_D3D11_TEXT)
  string(REPLACE "\r\n" "\n" TANDEM_D3D11_TEXT "${TANDEM_D3D11_TEXT}")
  string(SHA256 TANDEM_D3D11_ACTUAL "${TANDEM_D3D11_TEXT}")
  if(NOT TANDEM_D3D11_ACTUAL STREQUAL TANDEM_D3D11_EXPECTED)
    message(FATAL_ERROR "Direct3D compiler package has stale source: ${TANDEM_D3D11_FILE}. Rebuild the compiler package.")
  endif()
endforeach()
add_library(tandem_d3d11_shaders STATIC IMPORTED GLOBAL)
set_target_properties(tandem_d3d11_shaders PROPERTIES
  IMPORTED_LOCATION "${TANDEM_D3D11_COMPILER_PACKAGE_DIR}/${TANDEM_D3D11_ARCHIVE}"
  INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/gcn-recomp/runtime"
  INTERFACE_COMPILE_FEATURES cxx_std_20
  INTERFACE_LINK_LIBRARIES "d3dcompiler;dxguid")
message(STATUS "Using verified packaged native Direct3D shader compiler")
