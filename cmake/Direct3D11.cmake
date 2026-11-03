# Native device layer; the game remains OpenGL until GX/backend integration is
# complete. Building this target does not enable an unfinished renderer setting.
if(WIN32)
  add_library(tandem_d3d11_device STATIC EXCLUDE_FROM_ALL
    gcn-recomp/runtime/graphics/d3d11_device.cpp)
  target_compile_features(tandem_d3d11_device PUBLIC cxx_std_20)
  target_include_directories(tandem_d3d11_device PUBLIC gcn-recomp/runtime)
  target_compile_definitions(tandem_d3d11_device PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00)
  target_link_libraries(tandem_d3d11_device PUBLIC d3d11 dxgi dxguid)
  option(TANDEM_D3D11_TESTS "Build native Direct3D device correctness fixture" OFF)
  if(TANDEM_D3D11_TESTS)
    add_executable(d3d11_device_test tests/d3d11_device.cpp)
    target_link_libraries(d3d11_device_test PRIVATE tandem_d3d11_device d3dcompiler)
  endif()
endif()
