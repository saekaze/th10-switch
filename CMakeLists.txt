cmake_minimum_required(VERSION 3.16)
project(touhou10 C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# wasm2c runtime + generated game (32-bit memory model preserved)
set(WASM2C_SOURCES
    wasm2c/th10_wasm.c
    wasm2c/wasm-rt-impl.c
    wasm2c/wasm-rt-mem-impl.c
)
# th10_wasm.c is ~9MB of generated C. -O3 + debug info OOMs a 2GB box;
# -O2 -g0 is the same codegen class without blowing RAM. Host code stays -O3.
set_source_files_properties(${WASM2C_SOURCES} PROPERTIES COMPILE_FLAGS "-w -O2 -g0")

set(HOST_SOURCES
    src/main.cpp
    src/host_files.cpp
    src/host_time.cpp
    src/host_input.cpp
    src/host_graphics.cpp
    src/gl_renderer.cpp
    src/host_audio.cpp
    src/audio_mixer.cpp
    src/host_fonts.cpp
)

add_executable(touhou10 ${WASM2C_SOURCES} ${HOST_SOURCES})
target_include_directories(touhou10 PRIVATE wasm2c src)

if(NINTENDO_SWITCH)
    # ---- Nintendo Switch (devkitA64; configure with the devkitPro
    # Switch toolchain file, which defines NINTENDO_SWITCH) ----
    # Layout/naming follows the Saekaze Touhou ports: touhou10.nro +
    # sdmc:/switch/th10/ (th10.dat, thbgm.dat, msgothic.ttc).
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    if(NOT DEFINED ENV{DEVKITPRO})
        set(ENV{DEVKITPRO} "/opt/devkitpro")
    endif()
    set(TH10_DEVKITPRO "$ENV{DEVKITPRO}")
    if(NOT EXISTS "${TH10_DEVKITPRO}/cmake/Switch.cmake")
        message(FATAL_ERROR "devkitPro not found at ${TH10_DEVKITPRO}; set $DEVKITPRO")
    endif()
    if(NOT EXISTS "${CMAKE_SOURCE_DIR}/platform/switch/icon.jpg")
        message(FATAL_ERROR "platform/switch/icon.jpg (256x256 JPEG) not found.")
    endif()

    target_compile_definitions(touhou10 PRIVATE
        __SWITCH__
        # No mmap/signal-handler backends on Horizon: malloc-based wasm
        # memory. Per-access bounds/stack checks are elided in Release —
        # the game is a known-good module and the extra traps were the
        # lunatic-danmaku CPU bottleneck.
        WASM_RT_USE_MMAP=0
        WASM_RT_INSTALL_SIGNAL_HANDLER=0
        WASM_RT_NONCONFORMING_UNCHECKED_STACK_EXHAUSTION=1
        TH10_FAST_MEMCHECK=1
    )
    target_compile_options(touhou10 PRIVATE
        -O3
        -fno-strict-aliasing
    )
    target_include_directories(touhou10 PRIVATE
        ${CMAKE_SOURCE_DIR}/switch_include
        ${TH10_DEVKITPRO}/portlibs/switch/include
        ${TH10_DEVKITPRO}/portlibs/switch/include/SDL2
        ${TH10_DEVKITPRO}/portlibs/switch/include/freetype2
        ${TH10_DEVKITPRO}/libnx/include
    )
    target_link_directories(touhou10 PRIVATE
        ${TH10_DEVKITPRO}/portlibs/switch/lib
        ${TH10_DEVKITPRO}/libnx/lib
    )
    # Static link order matters: dependents first, dependencies after.
    target_link_libraries(touhou10 PRIVATE
        SDL2
        EGL
        glapi
        drm_nouveau
        GLESv2
        freetype
        harfbuzz
        png
        bz2
        z
        nx
        m
    )

    set_target_properties(touhou10 PROPERTIES OUTPUT_NAME "touhou10")

    # Default (all languages): romanised title. The original Japanese title is
    # patched into the Japanese slot only, so it shows up exclusively on
    # consoles whose system language is set to 日本語 — same as saekaze/th07.
    nx_generate_nacp(OUTPUT "${CMAKE_BINARY_DIR}/touhou10-base.nacp"
        NAME "Touhou 10: Mountain of Faith"
        AUTHOR "ZUN / Team Shanghai Alice"
        VERSION "1.00a-r3")
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
        VERBATIM
    )
    nx_create_nro(touhou10
        ICON "${CMAKE_SOURCE_DIR}/platform/switch/icon.jpg"
        NACP "${CMAKE_BINARY_DIR}/touhou10.nacp")
else()
    # ---- Linux test build ----
    target_compile_definitions(touhou10 PRIVATE
        WASM_RT_NONCONFORMING_UNCHECKED_STACK_EXHAUSTION=1
        TH10_FAST_MEMCHECK=1
    )
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2 REQUIRED sdl2)
    pkg_check_modules(FT REQUIRED freetype2)
    find_package(Threads REQUIRED)
    target_include_directories(touhou10 PRIVATE ${SDL2_INCLUDE_DIRS} ${FT_INCLUDE_DIRS})
    target_link_libraries(touhou10 PRIVATE
        ${SDL2_LIBRARIES} ${FT_LIBRARIES} GLESv2 EGL Threads::Threads m
    )
    target_compile_options(touhou10 PRIVATE -Wall -Wextra)
endif()
