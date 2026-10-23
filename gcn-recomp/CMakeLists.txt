# gcn-recomp: a GameCube static-recompilation toolkit. A game project adds this directory
# and calls gcn_add_game() with what is specific to its game; everything else -- the
# PowerPC -> C recompiler, the hardware runtime, the renderer and the frontends -- is here.
cmake_minimum_required(VERSION 3.20)
project(gcn_recomp C CXX)

# Language levels are set per target: gcn_add_game() runs in the game project's scope,
# where directory-wide settings made here would not apply.
function(_gcn_set_standards target)
  set_target_properties(${target} PROPERTIES C_STANDARD 11 CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
endfunction()

# GCN_BENCH_ONLY builds just the headless benchmark: no renderer, no SDL2, no GL. That
# is what a cross-build for a device uses, since it only needs a C++ toolchain and the
# recompiled code.
option(GCN_BENCH_ONLY "Build only the headless benchmark (no SDL2, no renderer)" OFF)
# See ENTER()/RET() in runtime/recomp.h. ON maintains a guest call stack, which answers
# "which guest function did that?" and gives a crash dump something to print.
option(GCN_TRACE_CALLS "Maintain a guest call stack (diagnostics)" OFF)
option(GCN_GUEST_CHECKS "Call the game project's consistency checks at every poll, DMA and frame (diagnostics)" OFF)

# PC VR: the desktop executable's --vr. Windows only for now: the frontend binds OpenXR to
# the window's WGL context (openxr/xr_desktop.cpp), and no other desktop binding is written.
if(WIN32 AND NOT GCN_BENCH_ONLY)
  set(_gcn_openxr_default ON)
else()
  set(_gcn_openxr_default OFF)
endif()
option(GCN_OPENXR "Build the desktop executable's OpenXR frontend (--vr)" ${_gcn_openxr_default})

# The OpenXR loader, from Khronos' SDK source at the version the Android build's AAR is.
# Static, and nothing but the loader.
macro(_gcn_fetch_openxr_loader)
  if(NOT TARGET openxr_loader)
    include(FetchContent)
    set(DYNAMIC_LOADER OFF CACHE BOOL "" FORCE)
    set(BUILD_API_LAYERS OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(BUILD_CONFORMANCE_TESTS OFF CACHE BOOL "" FORCE)
    set(BUILD_SDK_TESTS OFF CACHE BOOL "" FORCE)
    set(BUILD_ALL_EXTENSIONS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(openxr_sdk
      URL https://github.com/KhronosGroup/OpenXR-SDK/archive/refs/tags/release-1.1.63.tar.gz
      URL_HASH SHA256=5b8dac0608f49498fe8e95f562fe6bcf0bc781e561f137348a8c251d06a88b6f
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(openxr_sdk)
  endif()
endmacro()

# Build the game once without the modules it assembles at run time (GCN_BOOTSTRAP), run
# that headless with GCN_REL_DUMP until the game links each of `missing`, and keep what it
# linked in ${REL_RUNTIME_DIR}. The game's own code produces them, from the user's disc.
macro(_gcn_bootstrap_rels missing)
  set(_boot ${CMAKE_BINARY_DIR}/bootstrap)
  message(STATUS "Bootstrapping ${missing}: building and running the game without them")
  set(_boot_args -S ${CMAKE_SOURCE_DIR} -B ${_boot} -G ${CMAKE_GENERATOR} -DGCN_BOOTSTRAP=ON
                 -DGCN_BENCH_ONLY=ON -DCMAKE_BUILD_TYPE=Release "-DGAME_ISO=${GAME_ISO}"
                 -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER} -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER})
  if(CMAKE_OSX_SYSROOT)
    list(APPEND _boot_args -DCMAKE_OSX_SYSROOT=${CMAKE_OSX_SYSROOT})
  endif()
  execute_process(COMMAND ${CMAKE_COMMAND} ${_boot_args} RESULT_VARIABLE _r OUTPUT_QUIET)
  if(NOT _r EQUAL 0)
    message(FATAL_ERROR "bootstrap: configuring ${_boot} failed")
  endif()
  execute_process(COMMAND ${CMAKE_COMMAND} --build ${_boot} --target ${G_NAME}_bench
                  RESULT_VARIABLE _r OUTPUT_QUIET)
  if(NOT _r EQUAL 0)
    message(FATAL_ERROR "bootstrap: building ${_boot} failed")
  endif()
  file(MAKE_DIRECTORY ${REL_RUNTIME_DIR})
  string(REPLACE ";" "," _want "${missing}")
  execute_process(COMMAND ${CMAKE_COMMAND} -E env GCN_REL_DUMP=${REL_RUNTIME_DIR} GCN_REL_DUMP_WANT=${_want}
                          ${_boot}/${G_NAME}_bench --seconds=600 "${GAME_ISO}"
                  TIMEOUT 600 RESULT_VARIABLE _r OUTPUT_QUIET ERROR_VARIABLE _err)
  foreach(m ${missing})
    if(NOT EXISTS ${REL_RUNTIME_DIR}/${m}.rel)
      message(FATAL_ERROR "bootstrap: the game did not link ${m} (exit ${_r}):\n${_err}")
    endif()
  endforeach()
endmacro()

# gcn_add_game(
#   NAME <target>              executable name; the benchmark is <target>_bench
#   TITLE <string>             the game's name, for messages ("put your <title> .iso in rom/")
#   LABEL <string>             what the window and the headset app are called; defaults
#                              to TITLE. A name of the project's own keeps the game's
#                              trademark off the app itself.
#   DISC_ID <6 chars>          the game's disc ID; the image and the booted disc are checked against it
#   DOL_SHA1 <hex>             the supported build's main.dol
#   SYMBOLS <file>             decomp-toolkit symbols.txt for that DOL
#   TABLES <dir>               hle.txt, names.txt, patches.txt, special_calls.txt, idle.txt
#   FPS <n>                    the game's frame rate, for the benchmark's percentage
#   [DEFAULT_AUDIO_VOLUME <n>] initial master volume, 0..100 (default 100)
#   [IMAGE_DIR <dir>]          where to look for the user's .iso/.ciso (default rom/)
#   [DOL_FIXUP <script>]       a Python script run as `script <main.dol> <out.dol> <image>`
#                              after the DOL is extracted; the recompiler reads its output
#                              instead. For a game whose executable is not laid out the way
#                              it runs: one that carries a second image inside and maps it
#                              elsewhere, or whose data files carry code (the script may
#                              also write `<out.dol>.modules`, see recomp/recomp.py).
#   [RELS <disc paths>...]     relocatable modules the game loads with OSLink, from the disc;
#                              each is compiled at a canonical address and runs wherever
#                              the game links it (docs/rels.md)
#   [REL_RUNTIME <names>...]   modules the game does not keep on the disc as plain files but
#                              builds in memory before linking them (e.g. "game.main"). The
#                              first configure builds the game without them, runs it
#                              headless until the game itself hands each to OSLink, and
#                              keeps them in <build>/rels-runtime/ (docs/rels.md)
#   [REL_SYMBOLS <dir>]        dtk's symbols for each module, <dir>/<module>.txt, with the
#                              splits file beside it as <module>.splits.txt
#   [SOURCES <files>...]       the game project's own runtime sources, compiled into every
#                              target that links the game, the benchmark included, since a
#                              patch in TABLES may call into them: hooks the runtime offers a
#                              game (render_set_eye_hook in runtime/gx/render_gl.h,
#                              vr::GameHooks in runtime/vr_game.h), installed from a static
#                              initializer. The benchmark has no renderer; the hook
#                              installers it may call are stubbed there
#                              (runtime/gx/render_stub.cpp), and anything more of the
#                              renderer a game source needs must go through a hook.
#   [ANDROID_PACKAGE <id>]     e.g. com.example.game. On an Android build, also build the
#                              headset app (lib<target>.so, a NativeActivity that
#                              tools/package-apk.ps1 packages) and <target>_egl, the
#                              renderer headless on a device; see android/
# )
# The user's image may also be given as -DGAME_ISO=<path>.
function(gcn_add_game)
  cmake_parse_arguments(G "" "NAME;TITLE;LABEL;DISC_ID;DOL_SHA1;SYMBOLS;TABLES;FPS;DEFAULT_AUDIO_VOLUME;IMAGE_DIR;ANDROID_PACKAGE;DOL_FIXUP;REL_SYMBOLS" "SOURCES;RELS;REL_RUNTIME" ${ARGN})
  if("${G_DEFAULT_AUDIO_VOLUME}" STREQUAL "")
    set(G_DEFAULT_AUDIO_VOLUME 100)
  endif()
  if(NOT "${G_DEFAULT_AUDIO_VOLUME}" MATCHES "^[0-9]+$" OR G_DEFAULT_AUDIO_VOLUME GREATER 100)
    message(FATAL_ERROR "DEFAULT_AUDIO_VOLUME must be an integer from 0 to 100")
  endif()
  if(NOT G_LABEL)
    set(G_LABEL "${G_TITLE}")
  endif()
  foreach(req NAME TITLE DISC_ID DOL_SHA1 SYMBOLS TABLES FPS)
    if(NOT G_${req})
      message(FATAL_ERROR "gcn_add_game: ${req} is required")
    endif()
  endforeach()
  if(NOT G_IMAGE_DIR)
    set(G_IMAGE_DIR ${CMAKE_SOURCE_DIR}/rom)
  endif()
  # This function runs in the game project's scope, so it locates its own files from
  # where it was defined rather than from any variable set alongside it.
  set(GCN_RECOMP_DIR ${CMAKE_CURRENT_FUNCTION_LIST_DIR})

  find_package(Python3 REQUIRED COMPONENTS Interpreter)

  # The game image is supplied by the user: an uncompressed .iso or a .ciso.
  file(GLOB _rom_images ${G_IMAGE_DIR}/*.iso ${G_IMAGE_DIR}/*.ciso)
  list(LENGTH _rom_images _rom_count)
  if(_rom_count GREATER 0)
    list(GET _rom_images 0 _default_iso)
  else()
    set(_default_iso "")
  endif()
  set(GAME_ISO "${_default_iso}" CACHE FILEPATH "Your ${G_TITLE} disc image (.iso or .ciso)")

  # Without an image we can still compile the runtime, which is enough to check a
  # port. Only the recompiled game code and the final link need the disc.
  if(GAME_ISO AND EXISTS "${GAME_ISO}")
    set(HAVE_GAME_ISO TRUE)
  else()
    set(HAVE_GAME_ISO FALSE)
    if(GAME_ISO)
      message(WARNING "GAME_ISO is set to '${GAME_ISO}' but that file does not exist.")
    endif()
    message(WARNING
      "No game image found, so only the `runtime_check` target (compile the runtime, no link) "
      "is available. Put your own ${G_TITLE} .iso or .ciso in ${G_IMAGE_DIR}/ "
      "or pass -DGAME_ISO=/path/to/game.iso to build the game. See README.md.")
  endif()

  set(GAME_DOL ${CMAKE_BINARY_DIR}/main.dol)
  set(RECOMP_GEN_DIR ${CMAKE_BINARY_DIR}/gen)
  set(RT ${GCN_RECOMP_DIR}/runtime)

  # ---- Recompiled game code ----
  if(HAVE_GAME_ISO)
    execute_process(COMMAND ${Python3_EXECUTABLE} ${GCN_RECOMP_DIR}/recomp/extract_dol.py
                            "${GAME_ISO}" ${GAME_DOL} ${G_DISC_ID} ${G_DOL_SHA1}
                    RESULT_VARIABLE EXTRACT_RESULT)
    if(NOT EXTRACT_RESULT EQUAL 0)
      message(FATAL_ERROR "could not extract main.dol from ${GAME_ISO}")
    endif()

    file(GLOB RECOMP_TOOL_SOURCES ${GCN_RECOMP_DIR}/recomp/*.py ${G_TABLES}/*.txt)
    set(RECOMP_DOL ${GAME_DOL})
    if(G_DOL_FIXUP)
      set(RECOMP_DOL ${CMAKE_BINARY_DIR}/main_recomp.dol)
      execute_process(COMMAND ${Python3_EXECUTABLE} ${G_DOL_FIXUP} ${GAME_DOL} ${RECOMP_DOL} ${GAME_ISO}
                      RESULT_VARIABLE FIXUP_RESULT)
      if(NOT FIXUP_RESULT EQUAL 0)
        message(FATAL_ERROR "${G_DOL_FIXUP} failed")
      endif()
      list(APPEND RECOMP_TOOL_SOURCES ${G_DOL_FIXUP})
    endif()
    if(G_RELS)
      set(REL_DIR ${CMAKE_BINARY_DIR}/rels)
      file(REMOVE_RECURSE ${REL_DIR})
      execute_process(COMMAND ${Python3_EXECUTABLE} ${GCN_RECOMP_DIR}/recomp/rel.py extract
                              "${GAME_ISO}" ${REL_DIR} ${G_RELS}
                      RESULT_VARIABLE REL_RESULT)
      if(NOT REL_RESULT EQUAL 0)
        message(FATAL_ERROR "could not extract the modules from ${GAME_ISO}")
      endif()
      file(GLOB _rel_files ${REL_DIR}/*.rel)
      # Modules the game assembles in memory: taken from the game itself, by a first build
      # without them (GCN_BOOTSTRAP) that runs until it links each one.
      set(REL_RUNTIME_DIR ${CMAKE_BINARY_DIR}/rels-runtime)
      if(G_REL_RUNTIME AND NOT GCN_BOOTSTRAP)
        set(_missing "")
        foreach(m ${G_REL_RUNTIME})
          if(NOT EXISTS ${REL_RUNTIME_DIR}/${m}.rel)
            list(APPEND _missing ${m})
          endif()
        endforeach()
        if(_missing AND CMAKE_CROSSCOMPILING)
          # The bootstrap has to run here, and a cross build's binaries do not: take the
          # modules from a desktop build of the same project instead.
          set(GCN_HOST_BUILD_DIR ${CMAKE_SOURCE_DIR}/build CACHE PATH
              "A desktop build of this project, whose rels-runtime/ a cross build takes")
          foreach(m ${_missing})
            if(NOT EXISTS ${GCN_HOST_BUILD_DIR}/rels-runtime/${m}.rel)
              message(FATAL_ERROR "${m} is made by running the game, which a cross build cannot do here. "
                      "Build for the desktop first (it writes ${GCN_HOST_BUILD_DIR}/rels-runtime/), "
                      "or point -DGCN_HOST_BUILD_DIR at a desktop build that has.")
            endif()
            file(COPY ${GCN_HOST_BUILD_DIR}/rels-runtime/${m}.rel DESTINATION ${REL_RUNTIME_DIR})
          endforeach()
        elseif(_missing)
          _gcn_bootstrap_rels("${_missing}")
        endif()
        foreach(m ${G_REL_RUNTIME})
          list(APPEND _rel_files ${REL_RUNTIME_DIR}/${m}.rel)
        endforeach()
      endif()
      list(SORT _rel_files)
      set(_rel_in ${RECOMP_DOL})
      set(RECOMP_DOL ${CMAKE_BINARY_DIR}/main_rels.dol)
      execute_process(COMMAND ${Python3_EXECUTABLE} ${GCN_RECOMP_DIR}/recomp/rel.py link
                              ${_rel_in} ${RECOMP_DOL} "${G_REL_SYMBOLS}" ${_rel_files}
                      RESULT_VARIABLE REL_RESULT)
      if(NOT REL_RESULT EQUAL 0)
        message(FATAL_ERROR "could not link the modules")
      endif()
      if(G_REL_SYMBOLS)
        file(GLOB _rel_syms ${G_REL_SYMBOLS}/*.txt)
        list(APPEND RECOMP_TOOL_SOURCES ${_rel_syms})
      endif()
    endif()
    execute_process(COMMAND ${Python3_EXECUTABLE} ${GCN_RECOMP_DIR}/recomp/recomp.py
                            ${RECOMP_DOL} ${G_SYMBOLS} ${RECOMP_GEN_DIR} ${G_TABLES}
                    RESULT_VARIABLE RECOMP_RESULT)
    if(NOT RECOMP_RESULT EQUAL 0)
      message(FATAL_ERROR "recompilation failed")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${RECOMP_TOOL_SOURCES} ${GAME_ISO} ${G_SYMBOLS})
    include(${RECOMP_GEN_DIR}/sources.cmake)

    add_library(game_recomp STATIC ${RECOMP_SOURCES})
    _gcn_set_standards(game_recomp)
    target_include_directories(game_recomp PUBLIC ${RT} ${RECOMP_GEN_DIR})
    # PUBLIC because the CPU struct grows with the call trace: the runtime and the
    # generated code must agree on the layout.
    if(GCN_TRACE_CALLS)
      target_compile_definitions(game_recomp PUBLIC GCN_TRACE_CALLS)
    endif()
    if(MSVC)
      # Generated code is enormous and machine-written: keep the optimizer honest about
      # aliasing and floating point, and silence the noise it legitimately produces.
      target_compile_options(game_recomp PRIVATE /O2 /fp:precise /fp:except- /wd4101 /wd4102 /wd4723 /bigobj)
    else()
      target_compile_options(game_recomp PRIVATE -O2 -fno-strict-aliasing -Wno-unused-label -Wno-unused-variable
                             -Wno-parentheses-equality -ffp-contract=off)
    endif()
  endif()

  # ---- Runtime ----
  file(GLOB_RECURSE RUNTIME_SOURCES CONFIGURE_DEPENDS ${RT}/*.cpp)

  # Alternative entry points and the stubs: none of these may land in a target that
  # already has main.cpp, audio.cpp or the renderer.
  list(REMOVE_ITEM RUNTIME_SOURCES ${RT}/bench_main.cpp ${RT}/audio_stub.cpp ${RT}/gx/render_stub.cpp)

  # Everything below the renderer and the SDL frontend -- the CPU, OS, hardware and GX
  # front end, including the batch queue in gx/xf.cpp -- is free of both GL and SDL.
  set(CORE_SOURCES ${RUNTIME_SOURCES})
  list(REMOVE_ITEM CORE_SOURCES
    ${RT}/main.cpp ${RT}/input_map.cpp ${RT}/desktop_gamepad.cpp ${RT}/desktop_controls.cpp ${RT}/desktop_settings.cpp ${RT}/desktop_pointer.cpp ${RT}/desktop_display.cpp ${RT}/desktop_sound.cpp ${RT}/desktop_stats.cpp ${RT}/ffb.cpp ${RT}/audio.cpp ${RT}/audio_sdl.cpp ${RT}/png.cpp ${RT}/record.cpp
    ${RT}/gx/render_gl.cpp ${RT}/gx/gl_load.cpp ${RT}/gx/shadergen.cpp)
  list(APPEND CORE_SOURCES ${RT}/audio_stub.cpp)
  # The benchmark compiles the game's SOURCES too (gcn_add_game), and a game's eye hook
  # installs itself into the renderer; render_stub.cpp answers those calls with nothing.
  set(BENCH_SOURCES ${CORE_SOURCES} ${RT}/bench_main.cpp ${RT}/gx/render_stub.cpp)

  # Render target: desktop OpenGL 3.3 core, or OpenGL ES 3.2 for Android and standalone
  # headsets. One renderer source serves both; see runtime/gx/gl.h. GL entry points are
  # resolved at runtime via a vendored glad loader; macOS links the system framework
  # directly and needs neither.
  if(ANDROID)
    set(_gcn_gl_es_default ON)
  else()
    set(_gcn_gl_es_default OFF)
  endif()
  option(GCN_GL_ES "Target OpenGL ES 3.2 instead of desktop OpenGL 3.3 core" ${_gcn_gl_es_default})
  if(GCN_GL_ES)
    set(GLAD_INCLUDE_DIR ${RT}/gx/glad_es/include)
    set(GLAD_SOURCE ${RT}/gx/glad_es/src/gles2.c)
    set(GCN_GL_DEFS GCN_GL_ES)
  else()
    set(GLAD_INCLUDE_DIR ${RT}/gx/glad/include)
    set(GLAD_SOURCE ${RT}/gx/glad/src/gl.c)
    set(GCN_GL_DEFS "")
  endif()
  if(NOT APPLE)
    list(APPEND RUNTIME_SOURCES ${GLAD_SOURCE})
  endif()

  # SDL2: pkg-config where it exists; on Windows there are no prebuilt ARM64 binaries,
  # so build it from source.
  if(GCN_BENCH_ONLY)
    set(GCN_SDL_TARGET "")
  elseif(WIN32)
    include(FetchContent)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST OFF CACHE BOOL "" FORCE)
    set(SDL2_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
    FetchContent_Declare(SDL2
      URL https://github.com/libsdl-org/SDL/releases/download/release-2.30.9/SDL2-2.30.9.tar.gz
      URL_HASH SHA256=24b574f71c87a763f50704bbb630cbe38298d544a1f890f099a4696b1d6beba4
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(SDL2)
    set(GCN_SDL_TARGET SDL2::SDL2-static)
  else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
    set(GCN_SDL_TARGET PkgConfig::SDL2)
  endif()

  if(NOT GCN_BENCH_ONLY AND NOT ANDROID)
    # Pinned, vendored source: friend installers build without another download.
    set(GCN_IMGUI ${GCN_RECOMP_DIR}/third_party/imgui)
    add_library(gcn_desktop_ui STATIC
      ${GCN_IMGUI}/imgui.cpp ${GCN_IMGUI}/imgui_draw.cpp
      ${GCN_IMGUI}/imgui_tables.cpp ${GCN_IMGUI}/imgui_widgets.cpp
      ${GCN_IMGUI}/backends/imgui_impl_sdl2.cpp ${GCN_IMGUI}/backends/imgui_impl_opengl3.cpp)
    _gcn_set_standards(gcn_desktop_ui)
    target_include_directories(gcn_desktop_ui PUBLIC ${GCN_IMGUI})
    target_link_libraries(gcn_desktop_ui PUBLIC ${GCN_SDL_TARGET} ${CMAKE_DL_LIBS})
  endif()

  if(MSVC)
    set(GCN_RUNTIME_WARNINGS /W3 /wd4244 /wd4267)
  else()
    set(GCN_RUNTIME_WARNINGS -Wall -Wno-unused-function)
  endif()

  # Platform libraries the runtime needs beyond SDL2. The benchmark links none of this.
  set(GCN_PLATFORM_LIBS "")
  if(GCN_BENCH_ONLY)
  elseif(APPLE)
    list(APPEND GCN_PLATFORM_LIBS "-framework OpenGL")
  elseif(WIN32)
    # opengl32 for the GL entry points, dbghelp to symbolize crash backtraces, ws2_32 for
    # telemetry's socket.
    list(APPEND GCN_PLATFORM_LIBS opengl32 dbghelp ws2_32 windowscodecs ole32 uuid)
  else()
    find_package(OpenGL REQUIRED)
    list(APPEND GCN_PLATFORM_LIBS OpenGL::GL)
  endif()

  # What the runtime knows about the game it was built for.
  set(GCN_GAME_DEFS
    GCN_DEFAULT_ISO="${GAME_ISO}"
    GCN_GAME_TITLE="${G_TITLE}"
    GCN_APP_LABEL="${G_LABEL}"
    GCN_DISC_ID="${G_DISC_ID}"
    GCN_GAME_FPS=${G_FPS})
  if(GCN_GUEST_CHECKS)
    list(APPEND GCN_GAME_DEFS GCN_GUEST_CHECKS)
  endif()

  # Headless benchmark: reports how fast the guest advances, with no graphics or audio
  # in the way. Used to compare host CPUs.
  if(HAVE_GAME_ISO)
    add_executable(${G_NAME}_bench ${BENCH_SOURCES} ${G_SOURCES})
    _gcn_set_standards(${G_NAME}_bench)
    target_include_directories(${G_NAME}_bench PRIVATE ${RT} ${RECOMP_GEN_DIR})
    target_link_libraries(${G_NAME}_bench PRIVATE game_recomp)
    target_compile_options(${G_NAME}_bench PRIVATE ${GCN_RUNTIME_WARNINGS})
    target_compile_definitions(${G_NAME}_bench PRIVATE ${GCN_GAME_DEFS})
    if(WIN32)
      target_link_libraries(${G_NAME}_bench PRIVATE dbghelp ws2_32)
    endif()
  endif()

  # Compile-only target: available even without a disc image, so the runtime and any
  # port work can be checked on its own. With an image present the game target already
  # compiles these sources, so keep it out of the default build; `ninja runtime_check`
  # still works.
  if(NOT GCN_BENCH_ONLY)
    if(HAVE_GAME_ISO)
      add_library(runtime_check OBJECT EXCLUDE_FROM_ALL ${RUNTIME_SOURCES})
    else()
      add_library(runtime_check OBJECT ${RUNTIME_SOURCES})
    endif()
    _gcn_set_standards(runtime_check)
    target_include_directories(runtime_check PRIVATE ${RT} ${RECOMP_GEN_DIR} ${GLAD_INCLUDE_DIR})
    target_link_libraries(runtime_check PRIVATE ${GCN_SDL_TARGET})
    if(TARGET gcn_desktop_ui)
      target_link_libraries(runtime_check PRIVATE gcn_desktop_ui)
    endif()
    target_compile_options(runtime_check PRIVATE ${GCN_RUNTIME_WARNINGS})
    target_compile_definitions(runtime_check PRIVATE ${GCN_GAME_DEFS} ${GCN_GL_DEFS})
  endif()

  if(HAVE_GAME_ISO AND NOT GCN_BENCH_ONLY)
    add_executable(${G_NAME} ${RUNTIME_SOURCES} ${G_SOURCES})
    _gcn_set_standards(${G_NAME})
    target_include_directories(${G_NAME} PRIVATE ${GLAD_INCLUDE_DIR})
    target_link_libraries(${G_NAME} PRIVATE game_recomp ${GCN_SDL_TARGET} ${GCN_PLATFORM_LIBS})
    if(TARGET gcn_desktop_ui)
      target_link_libraries(${G_NAME} PRIVATE gcn_desktop_ui)
    endif()
    target_compile_options(${G_NAME} PRIVATE ${GCN_RUNTIME_WARNINGS})
    target_compile_definitions(${G_NAME} PRIVATE ${GCN_GAME_DEFS} ${GCN_GL_DEFS} GCN_DEFAULT_AUDIO_VOLUME=${G_DEFAULT_AUDIO_VOLUME})

    # PC VR (--vr): the headset frontend in openxr/, on the window's own GL context. The
    # loader is Khronos' own, built from source and linked statically, so the executable
    # finds whichever runtime the system has active with nothing beside it.
    if(GCN_OPENXR)
      _gcn_fetch_openxr_loader()
      target_sources(${G_NAME} PRIVATE ${GCN_RECOMP_DIR}/openxr/xr_frontend.cpp ${GCN_RECOMP_DIR}/openxr/xr_desktop.cpp)
      target_include_directories(${G_NAME} PRIVATE ${GCN_RECOMP_DIR}/openxr ${RT})
      target_link_libraries(${G_NAME} PRIVATE openxr_loader $<$<PLATFORM_ID:Windows>:ole32>)
      target_compile_definitions(${G_NAME} PRIVATE GCN_OPENXR)
    endif()
  endif()

  option(GCN_INPUT_TESTS "Build the desktop gamepad integration test" OFF)
  option(GCN_NET_TESTS "Build experimental ordered-input transport tests" OFF)
  if(GCN_NET_TESTS AND NOT ANDROID)
    if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8 AND CMAKE_SYSTEM_PROCESSOR MATCHES "AMD64|amd64|x86_64")
      add_executable(rewind_stack_test ${GCN_RECOMP_DIR}/tests/rewind_stack.cpp ${RT}/rewind_stack.cpp)
      _gcn_set_standards(rewind_stack_test)
      target_include_directories(rewind_stack_test PRIVATE ${RT})
      add_executable(memory_checkpoint_test ${GCN_RECOMP_DIR}/tests/memory_checkpoint.cpp ${RT}/memory_checkpoint.cpp)
      _gcn_set_standards(memory_checkpoint_test)
      target_include_directories(memory_checkpoint_test PRIVATE ${RT})
      add_executable(input_mesh_proxy ${GCN_RECOMP_DIR}/tests/input_mesh_proxy.cpp)
      _gcn_set_standards(input_mesh_proxy)
      target_include_directories(input_mesh_proxy PRIVATE ${RT})
      target_link_libraries(input_mesh_proxy PRIVATE ws2_32 winmm)
    endif()
    add_executable(rollback_timeline_test ${GCN_RECOMP_DIR}/tests/rollback_timeline.cpp)
    add_executable(input_mesh_test ${GCN_RECOMP_DIR}/tests/input_mesh.cpp)
    add_executable(redundant_input_test ${GCN_RECOMP_DIR}/tests/redundant_input.cpp)
    _gcn_set_standards(redundant_input_test)
    target_include_directories(redundant_input_test PRIVATE ${RT})
    _gcn_set_standards(input_mesh_test)
    target_include_directories(input_mesh_test PRIVATE ${RT})
    add_executable(device_checkpoint_test ${GCN_RECOMP_DIR}/tests/device_checkpoint.cpp)
    _gcn_set_standards(device_checkpoint_test)
    target_include_directories(device_checkpoint_test PRIVATE ${RT})
    _gcn_set_standards(rollback_timeline_test)
    target_include_directories(rollback_timeline_test PRIVATE ${RT})
    add_executable(peer_link_test ${GCN_RECOMP_DIR}/tests/peer_link.cpp ${RT}/net/peer_link.cpp)
    add_executable(prediction_link_test ${GCN_RECOMP_DIR}/tests/prediction_link.cpp ${RT}/net/peer_link.cpp)
    _gcn_set_standards(prediction_link_test)
    target_include_directories(prediction_link_test PRIVATE ${RT})
    _gcn_set_standards(peer_link_test)
    target_include_directories(peer_link_test PRIVATE ${RT})
    if(WIN32)
      target_link_libraries(peer_link_test PRIVATE ws2_32)
      target_link_libraries(prediction_link_test PRIVATE ws2_32)
    else()
      find_package(Threads REQUIRED)
      target_link_libraries(peer_link_test PRIVATE Threads::Threads)
      target_link_libraries(prediction_link_test PRIVATE Threads::Threads)
    endif()
  endif()
  if(GCN_INPUT_TESTS AND NOT GCN_BENCH_ONLY AND NOT ANDROID)
    add_executable(gamepad_test ${GCN_RECOMP_DIR}/tests/desktop_gamepad.cpp ${RT}/desktop_gamepad.cpp)
    _gcn_set_standards(gamepad_test)
    target_include_directories(gamepad_test PRIVATE ${RT})
    target_link_libraries(gamepad_test PRIVATE ${GCN_SDL_TARGET})
    add_executable(desktop_controls_test ${GCN_RECOMP_DIR}/tests/desktop_controls.cpp ${RT}/desktop_controls.cpp ${RT}/desktop_gamepad.cpp ${RT}/input_defaults.cpp)
    _gcn_set_standards(desktop_controls_test)
    target_include_directories(desktop_controls_test PRIVATE ${RT})
    target_link_libraries(desktop_controls_test PRIVATE ${GCN_SDL_TARGET})
    add_executable(desktop_settings_test ${GCN_RECOMP_DIR}/tests/desktop_settings.cpp
      ${RT}/desktop_settings.cpp ${RT}/desktop_pointer.cpp ${RT}/desktop_controls.cpp ${RT}/desktop_gamepad.cpp ${RT}/desktop_display.cpp ${RT}/desktop_sound.cpp ${RT}/display_config.cpp ${RT}/input_defaults.cpp
      ${RT}/gx/gl_load.cpp ${RT}/png.cpp ${GLAD_SOURCE})
    _gcn_set_standards(desktop_settings_test)
    target_include_directories(desktop_settings_test PRIVATE ${RT} ${GLAD_INCLUDE_DIR})
    target_compile_definitions(desktop_settings_test PRIVATE ${GCN_GL_DEFS} GCN_DEFAULT_AUDIO_VOLUME=${G_DEFAULT_AUDIO_VOLUME})
    target_link_libraries(desktop_settings_test PRIVATE gcn_desktop_ui ${GCN_PLATFORM_LIBS})
    add_executable(desktop_display_test ${GCN_RECOMP_DIR}/tests/desktop_display.cpp ${RT}/desktop_display.cpp ${RT}/input_defaults.cpp)
    _gcn_set_standards(desktop_display_test)
    target_include_directories(desktop_display_test PRIVATE ${RT})
    target_link_libraries(desktop_display_test PRIVATE ${GCN_SDL_TARGET})
    add_executable(desktop_sound_test ${GCN_RECOMP_DIR}/tests/desktop_sound.cpp ${RT}/desktop_sound.cpp)
    _gcn_set_standards(desktop_sound_test)
    target_include_directories(desktop_sound_test PRIVATE ${RT})
    add_executable(desktop_stats_test ${GCN_RECOMP_DIR}/tests/desktop_stats.cpp ${RT}/desktop_stats.cpp
      ${RT}/gx/gl_load.cpp ${RT}/png.cpp ${GLAD_SOURCE})
    _gcn_set_standards(desktop_stats_test)
    target_include_directories(desktop_stats_test PRIVATE ${RT} ${GLAD_INCLUDE_DIR})
    target_compile_definitions(desktop_stats_test PRIVATE ${GCN_GL_DEFS})
    target_link_libraries(desktop_stats_test PRIVATE ${GCN_SDL_TARGET} ${GCN_PLATFORM_LIBS})
    add_executable(presentation_stats_test ${GCN_RECOMP_DIR}/tests/presentation_stats.cpp)
    _gcn_set_standards(presentation_stats_test)
    target_include_directories(presentation_stats_test PRIVATE ${RT})
  endif()

  # ---- Android headset app and harness ----
  # Built from the same pieces as everything else, with the real audio mix and an AAudio
  # device in place of the stub, and the game's own sources. The OpenXR loader comes from
  # Khronos' official Android AAR on Maven Central: it carries both the headers and a
  # prebuilt libopenxr_loader.so, so nothing has to be committed as a binary.
  if(ANDROID AND HAVE_GAME_ISO AND G_ANDROID_PACKAGE)
    set(AND_DIR ${GCN_RECOMP_DIR}/android)
    set(ANDROID_SOURCES ${CORE_SOURCES} ${RT}/audio.cpp ${AND_DIR}/audio_aaudio.cpp
      ${RT}/gx/render_gl.cpp ${RT}/gx/shadergen.cpp ${RT}/gx/gl_load.cpp ${RT}/png.cpp
      ${GLAD_SOURCE} ${G_SOURCES})
    list(REMOVE_ITEM ANDROID_SOURCES ${RT}/audio_stub.cpp)
    set(ANDROID_DEFS ${GCN_GAME_DEFS} ${GCN_GL_DEFS} GCN_LOG_TAG="${G_NAME}")

    # Headless EGL frontend: the real renderer, offscreen, writing frames as PNGs. Not an
    # APK: it validates the GL ES renderer on a device over adb, and shows the stereo path
    # without anyone wearing the headset.
    add_executable(${G_NAME}_egl ${ANDROID_SOURCES} ${AND_DIR}/egl_main.cpp)
    _gcn_set_standards(${G_NAME}_egl)
    target_include_directories(${G_NAME}_egl PRIVATE ${AND_DIR} ${RT} ${RECOMP_GEN_DIR} ${GLAD_INCLUDE_DIR})
    target_link_libraries(${G_NAME}_egl PRIVATE game_recomp EGL log aaudio)
    target_compile_options(${G_NAME}_egl PRIVATE ${GCN_RUNTIME_WARNINGS})
    target_compile_definitions(${G_NAME}_egl PRIVATE ${ANDROID_DEFS})

    include(FetchContent)
    FetchContent_Declare(openxr_aar
      URL https://repo1.maven.org/maven2/org/khronos/openxr/openxr_loader_for_android/1.1.63/openxr_loader_for_android-1.1.63.aar
      URL_HASH SHA256=622419d2f6741c3443a3beb4779af0764318edd01830de967f24c741ebcded73
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(openxr_aar)
    set(OPENXR_INCLUDE ${openxr_aar_SOURCE_DIR}/prefab/modules/headers/include)
    set(OPENXR_LIB ${openxr_aar_SOURCE_DIR}/prefab/modules/openxr_loader/libs/android.${ANDROID_ABI}/libopenxr_loader.so)

    add_library(native_app_glue STATIC
      ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c)
    target_include_directories(native_app_glue PUBLIC
      ${ANDROID_NDK}/sources/android/native_app_glue)

    # The installable app: android.app.NativeActivity loads lib<name>.so and calls
    # android_main(), so there is no Java source at all. The library name must match the
    # manifest's android.app.lib_name, which the template below fills in.
    add_library(${G_NAME} SHARED ${ANDROID_SOURCES} ${AND_DIR}/openxr_main.cpp ${GCN_RECOMP_DIR}/openxr/xr_frontend.cpp)
    _gcn_set_standards(${G_NAME})
    target_include_directories(${G_NAME} PRIVATE ${AND_DIR} ${GCN_RECOMP_DIR}/openxr ${RT} ${RECOMP_GEN_DIR} ${GLAD_INCLUDE_DIR} ${OPENXR_INCLUDE})
    target_link_libraries(${G_NAME} PRIVATE game_recomp native_app_glue ${OPENXR_LIB} EGL android log aaudio)
    target_compile_options(${G_NAME} PRIVATE ${GCN_RUNTIME_WARNINGS})
    # No default image: the app finds game.iso or game.ciso in its own files directory.
    set(APP_DEFS ${ANDROID_DEFS})
    list(FILTER APP_DEFS EXCLUDE REGEX "^GCN_DEFAULT_ISO=")
    target_compile_definitions(${G_NAME} PRIVATE ${APP_DEFS} GCN_DEFAULT_ISO="")
    # NativeActivity resolves this symbol by name; without it the linker drops the glue.
    target_link_options(${G_NAME} PRIVATE -u ANativeActivity_onCreate)

    configure_file(${AND_DIR}/AndroidManifest.xml.in ${CMAKE_BINARY_DIR}/AndroidManifest.xml @ONLY)
  endif()

  # For a game project that adds frontends of its own -- a headset app, a headless harness
  # on a device -- the pieces such a target is assembled from. GCN_CORE_SOURCES is the
  # guest and the hardware with no renderer, no SDL and the silent audio stub;
  # GCN_AUDIO_SOURCES is the real mix (a frontend adds its own device backend);
  # GCN_RENDER_SOURCES is the GL renderer. Every target also needs ${GCN_GLAD_SOURCE} off
  # Apple, the include directories GCN_RT, GCN_GEN_DIR and GCN_GLAD_INCLUDE_DIR, and the
  # definitions GCN_GAME_DEFS and GCN_GL_DEFS; it links game_recomp.
  set(GCN_RT ${RT} PARENT_SCOPE)
  set(GCN_GEN_DIR ${RECOMP_GEN_DIR} PARENT_SCOPE)
  set(GCN_HAVE_GAME_ISO ${HAVE_GAME_ISO} PARENT_SCOPE)
  set(GCN_CORE_SOURCES ${CORE_SOURCES} PARENT_SCOPE)
  set(GCN_AUDIO_SOURCES ${RT}/audio.cpp PARENT_SCOPE)
  set(GCN_RENDER_SOURCES ${RT}/gx/render_gl.cpp ${RT}/gx/shadergen.cpp ${RT}/gx/gl_load.cpp ${RT}/png.cpp PARENT_SCOPE)
  set(GCN_GLAD_INCLUDE_DIR ${GLAD_INCLUDE_DIR} PARENT_SCOPE)
  set(GCN_GLAD_SOURCE ${GLAD_SOURCE} PARENT_SCOPE)
  set(GCN_RUNTIME_WARNINGS ${GCN_RUNTIME_WARNINGS} PARENT_SCOPE)
  set(GCN_GAME_DEFS ${GCN_GAME_DEFS} PARENT_SCOPE)
  set(GCN_GL_DEFS ${GCN_GL_DEFS} PARENT_SCOPE)
endfunction()
