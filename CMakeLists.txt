cmake_minimum_required(VERSION 3.20)
project(mkdd C CXX)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# Everything that is not specific to Mario Kart: Double Dash!! lives in gcn-recomp (a
# submodule): the recompiler, the GameCube runtime, the renderer and the frontends. See its
# README.
add_subdirectory(gcn-recomp)
include(cmake/EOS.cmake)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
# Local ISO builds have different paths and PE timestamps. Compare their source
# and compiler policy, rather than rejecting friends because EXE bytes differ.
file(GLOB_RECURSE MKDD_ID_INPUTS CONFIGURE_DEPENDS
  src/*.cpp src/*.h src/*.inc services/*.py analysis/*.txt analysis/*.yml recomp/*.txt cmake/*.cmake
  gcn-recomp/runtime/*.cpp gcn-recomp/runtime/*.h gcn-recomp/runtime/*.c gcn-recomp/runtime/*.inc
  gcn-recomp/third_party/libco/*.c gcn-recomp/third_party/libco/*.h
  gcn-recomp/recomp/*.py gcn-recomp/openxr/*.cpp gcn-recomp/openxr/*.h)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  ${MKDD_ID_INPUTS} tools/build_identity.py gcn-recomp/CMakeLists.txt)
string(TOUPPER "${CMAKE_BUILD_TYPE}" MKDD_BUILD_TYPE_UPPER)
set(MKDD_POLICY "${CMAKE_BINARY_DIR}/compatibility-policy.json")
# Escape flags as JSON, including user-supplied backslashes and quotes.
set(MKDD_POLICY_FLAGS "${CMAKE_C_FLAGS}|${CMAKE_CXX_FLAGS}|${CMAKE_C_FLAGS_${MKDD_BUILD_TYPE_UPPER}}|${CMAKE_CXX_FLAGS_${MKDD_BUILD_TYPE_UPPER}}")
string(REPLACE "\\" "\\\\" MKDD_POLICY_FLAGS "${MKDD_POLICY_FLAGS}")
string(REPLACE "\"" "\\\"" MKDD_POLICY_FLAGS "${MKDD_POLICY_FLAGS}")
string(REPLACE "\n" "\\n" MKDD_POLICY_FLAGS "${MKDD_POLICY_FLAGS}")
file(WRITE "${MKDD_POLICY}" "{\"c\":\"${CMAKE_C_COMPILER_ID}-${CMAKE_C_COMPILER_VERSION}\",\"cxx\":\"${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}\",\"platform\":\"${CMAKE_SYSTEM_NAME}\",\"bits\":\"${CMAKE_SIZEOF_VOID_P}\",\"build_type\":\"${CMAKE_BUILD_TYPE}\",\"flags\":\"${MKDD_POLICY_FLAGS}\",\"openxr\":\"${GCN_OPENXR}\",\"gl_es\":\"${GCN_GL_ES}\",\"checks\":\"${GCN_GUEST_CHECKS}\",\"trace\":\"${GCN_TRACE_CALLS}\"}\n")
execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/build_identity.py"
  --root "${CMAKE_SOURCE_DIR}" --policy "${MKDD_POLICY}" --output "${CMAKE_BINARY_DIR}/identity"
  RESULT_VARIABLE MKDD_ID_RESULT OUTPUT_VARIABLE MKDD_BUILD_ID OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT MKDD_ID_RESULT EQUAL 0)
  message(FATAL_ERROR "Could not generate the local-build compatibility identity")
endif()
message(STATUS "Online build compatibility: ${MKDD_BUILD_ID}")

gcn_add_game(
  NAME mkdd
  TITLE "Mario Kart: Double Dash!!"
  LABEL "Project Tandem"
  DISC_ID GM4E01
  DOL_SHA1 e389e50357d4a5076ab3d38672204658f411123d
  SYMBOLS ${CMAKE_SOURCE_DIR}/analysis/symbols.txt
  TABLES ${CMAKE_SOURCE_DIR}/recomp
  FPS 60
  DEFAULT_AUDIO_VOLUME 25
  # The game's half of the headset and the motion rig: when to be in stereo, the world's
  # scale, what the eyes leave out, and the kart's motion (docs/dev/vr.md); and where its
  # sticks respond, for controls.txt (docs/dev/controls.md).
  SOURCES ${CMAKE_SOURCE_DIR}/src/race.cpp ${CMAKE_SOURCE_DIR}/src/vr_mkdd.cpp ${CMAKE_SOURCE_DIR}/src/motion.cpp
          ${CMAKE_SOURCE_DIR}/src/controls.cpp ${CMAKE_SOURCE_DIR}/src/widescreen.cpp
          ${CMAKE_SOURCE_DIR}/src/online_sync.cpp
          ${CMAKE_SOURCE_DIR}/src/rollback_probe.cpp
          ${CMAKE_SOURCE_DIR}/src/full_frame_probe.cpp
          ${CMAKE_SOURCE_DIR}/src/native_checkpoint.cpp
          ${CMAKE_SOURCE_DIR}/src/online_menu.cpp
          ${CMAKE_SOURCE_DIR}/src/online_tags.cpp
          ${CMAKE_SOURCE_DIR}/src/online_room.cpp
  # The headset app and its harness, on an Android build.
  ANDROID_PACKAGE com.ethannicholas.mkdd)

if(TARGET mkdd)
  target_include_directories(mkdd PRIVATE "${CMAKE_BINARY_DIR}/identity")
endif()
if(TARGET mkdd_bench)
  target_include_directories(mkdd_bench PRIVATE "${CMAKE_BINARY_DIR}/identity")
endif()

if(WIN32)
  target_link_libraries(mkdd PRIVATE winhttp)
  option(MKDD_ONLINE_TESTS "Build native room-protocol fixture" OFF)
  if(MKDD_ONLINE_TESTS)
    add_executable(diagnostic_log_test tests/diagnostic_log.cpp)
    target_include_directories(diagnostic_log_test PRIVATE gcn-recomp/runtime)
    target_compile_features(diagnostic_log_test PRIVATE cxx_std_17)
    add_executable(eos_send_queue_test tests/eos_send_queue.cpp)
    target_include_directories(eos_send_queue_test PRIVATE src)
    target_compile_features(eos_send_queue_test PRIVATE cxx_std_17)
    add_executable(adaptive_input_delay_test tests/adaptive_input_delay.cpp)
    target_include_directories(adaptive_input_delay_test PRIVATE src)
    target_compile_features(adaptive_input_delay_test PRIVATE cxx_std_17)
    add_executable(eos_protocol_test tests/eos_protocol.cpp)
    target_include_directories(eos_protocol_test PRIVATE src)
    target_compile_features(eos_protocol_test PRIVATE cxx_std_17)
    add_executable(online_room_test tests/online_room.cpp src/online_room.cpp)
    target_include_directories(online_room_test PRIVATE src gcn-recomp/runtime)
    target_compile_features(online_room_test PRIVATE cxx_std_17)
    target_link_libraries(online_room_test PRIVATE winhttp)
  endif()
endif()
