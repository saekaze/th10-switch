cmake_minimum_required(VERSION 3.16)
project(touhou10 C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

# Upstream TH10 (YomotsuHisami/th10, th10_web/cpp): game logic in game/, the
# browser platform layer in platform/, the x87 SoftFloat in rebuild/, the
# shared GLES renderer, and the Switch host in src/ (replacing cpp/sdl).
file(GLOB GAME_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/game/*.cpp ${CMAKE_SOURCE_DIR}/platform/*.cpp)
# Wasm-module glue for the old freestanding build, not part of the native game.
list(REMOVE_ITEM GAME_SOURCES ${CMAKE_SOURCE_DIR}/game/LegacyBridge.cpp ${CMAKE_SOURCE_DIR}/game/LegacyCallbacks.cpp
    ${CMAKE_SOURCE_DIR}/game/Exports.cpp ${CMAKE_SOURCE_DIR}/game/Freestanding.cpp)
file(GLOB HOST_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/*.cpp)
list(REMOVE_ITEM HOST_SOURCES ${CMAKE_SOURCE_DIR}/src/main.cpp)
list(APPEND HOST_SOURCES portable/sdl/Renderer.cpp rebuild/third_party/softfloat.c)
set_source_files_properties(rebuild/third_party/softfloat.c PROPERTIES COMPILE_DEFINITIONS "SOFTFLOAT_FAST_INT64;INLINE_LEVEL=5")

# Game + host objects, shared by the game and the host tests.
add_library(th10_core OBJECT ${GAME_SOURCES} ${HOST_SOURCES})
target_include_directories(th10_core PUBLIC src portable/sdl)

# Same code-generation contract as the upstream web build: no FMA
# contraction (bit-exact float order with the original), no strict aliasing,
# no exceptions/RTTI. GCC defaults to -ffp-contract=fast on AArch64, so this
# flag is load-bearing for replay/RNG determinism.
target_compile_options(th10_core PUBLIC
    -O2 -ffp-contract=off -fno-strict-aliasing
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-rtti>
    $<$<CXX_COMPILER_ID:GNU>:-Wno-class-memaccess> -Wno-misleading-indentation -Wno-parentheses)
target_compile_definitions(th10_core PUBLIC TH_NATIVE_PLATFORM=1 TH_SDL3=1)

add_executable(touhou10 src/main.cpp)
target_link_libraries(touhou10 PRIVATE th10_core)

if(NINTENDO_SWITCH)
    # ---- Nintendo Switch (devkitA64; configure with the devkitPro Switch
    # toolchain file, which defines NINTENDO_SWITCH) ----
    # Layout/naming follows the Saekaze Touhou ports: touhou10.nro +
    # sdmc:/switch/touhou/touhou10/ (th10.dat, thbgm.dat, msgothic.ttc).
    if(NOT DEFINED ENV{DEVKITPRO})
        set(ENV{DEVKITPRO} "/opt/devkitpro")
    endif()
    set(TH_DEVKITPRO "$ENV{DEVKITPRO}")
    if(NOT EXISTS "${CMAKE_SOURCE_DIR}/platform/switch/icon.jpg")
        message(FATAL_ERROR "platform/switch/icon.jpg (256x256 JPEG) not found.")
    endif()
    target_compile_definitions(th10_core PUBLIC __SWITCH__)
    target_include_directories(th10_core PUBLIC
        ${TH_DEVKITPRO}/portlibs/switch/include
        ${TH_DEVKITPRO}/portlibs/switch/include/SDL2
        ${TH_DEVKITPRO}/portlibs/switch/include/freetype2
        ${TH_DEVKITPRO}/libnx/include)
    target_link_directories(touhou10 PRIVATE
        ${TH_DEVKITPRO}/portlibs/switch/lib
        ${TH_DEVKITPRO}/libnx/lib)
    # Static link order matters: dependents first, dependencies after.
    target_link_libraries(touhou10 PRIVATE
        SDL2 EGL glapi drm_nouveau GLESv2 freetype harfbuzz png bz2 z nx m)
    set_target_properties(touhou10 PROPERTIES OUTPUT_NAME "touhou10")

    # Default (all languages): romanised title. The original Japanese title is
    # patched into the Japanese slot only - same as saekaze/th07 and th10.
    nx_generate_nacp(OUTPUT "${CMAKE_BINARY_DIR}/touhou10-base.nacp"
        NAME "Touhou 10: Mountain of Faith"
        AUTHOR "ZUN / Team Shanghai Alice"
        VERSION "1.00a-r6")
    add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/touhou10.nacp"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/scripts/nacp_lang.py"
                "${CMAKE_BINARY_DIR}/touhou10-base.nacp" "${CMAKE_BINARY_DIR}/touhou10.nacp"
                --lang Japanese
                --name "東方風神録　～ Mountain of Faith"
                --author "上海アリス幻樂団"
                --fill-empty-name "Touhou 10: Mountain of Faith"
                --fill-empty-author "ZUN / Team Shanghai Alice"
        DEPENDS "${CMAKE_BINARY_DIR}/touhou10-base.nacp"
                "${CMAKE_SOURCE_DIR}/scripts/nacp_lang.py"
        COMMENT "Patching Japanese title into NACP"
        VERBATIM)
    nx_create_nro(touhou10
        ICON "${CMAKE_SOURCE_DIR}/platform/switch/icon.jpg"
        NACP "${CMAKE_BINARY_DIR}/touhou10.nacp")
else()
    # ---- Linux test build (same sources, SDL2 + GLES3 desktop) ----
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2 REQUIRED sdl2)
    pkg_check_modules(FT REQUIRED freetype2)
    target_include_directories(th10_core PUBLIC ${SDL2_INCLUDE_DIRS} ${FT_INCLUDE_DIRS})
    target_link_libraries(touhou10 PRIVATE ${SDL2_LIBRARIES} ${FT_LIBRARIES} GLESv2 m)

    # Host tests: ./build/th10_host_tests (run under xvfb-run for the renderer)
    add_executable(th10_host_tests tests/test_host.cpp)
    target_link_libraries(th10_host_tests PRIVATE th10_core ${SDL2_LIBRARIES} ${FT_LIBRARIES} GLESv2 m)

endif()
