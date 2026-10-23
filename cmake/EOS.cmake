# Use the SDK downloaded by the developer from Epic. Never fetch an unofficial
# SDK mirror or put SDK headers / credentials into the source distribution.
option(MKDD_EOS "Build the Epic Online Services helper" OFF)
set(MKDD_EOS_SDK_ROOT "" CACHE PATH "Extracted Epic C SDK root (SDK/Include, SDK/Lib, SDK/Bin)")
if(MKDD_EOS)
  if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "The current EOS helper targets 64-bit Windows")
  endif()
  set(_sdk "${MKDD_EOS_SDK_ROOT}")
  if(EXISTS "${_sdk}/SDK/Include/eos_sdk.h")
    set(_sdk "${_sdk}/SDK")
  endif()
  foreach(_file Include/eos_sdk.h Include/eos_version.h Lib/EOSSDK-Win64-Shipping.lib Bin/EOSSDK-Win64-Shipping.dll)
    if(NOT EXISTS "${_sdk}/${_file}")
      message(FATAL_ERROR "EOS SDK missing ${_file}. Set MKDD_EOS_SDK_ROOT to your extracted official Windows C SDK")
    endif()
  endforeach()
  add_library(mkdd_eos_sdk SHARED IMPORTED)
  set_target_properties(mkdd_eos_sdk PROPERTIES
    IMPORTED_IMPLIB "${_sdk}/Lib/EOSSDK-Win64-Shipping.lib"
    IMPORTED_LOCATION "${_sdk}/Bin/EOSSDK-Win64-Shipping.dll"
    INTERFACE_INCLUDE_DIRECTORIES "${_sdk}/Include")
  add_executable(mkdd_eos src/eos_main.cpp src/eos_config.cpp src/eos_runtime.cpp src/eos_bridge.cpp src/eos_local_http.cpp src/eos_service.cpp)
  target_compile_features(mkdd_eos PRIVATE cxx_std_17)
  target_link_libraries(mkdd_eos PRIVATE mkdd_eos_sdk ws2_32 winhttp bcrypt)
  add_custom_command(TARGET mkdd_eos POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:mkdd_eos_sdk>" "$<TARGET_FILE_DIR:mkdd_eos>")
endif()
