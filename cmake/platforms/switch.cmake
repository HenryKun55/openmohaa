# Nintendo Switch (devkitPro / libnx homebrew) specific settings.
#
# Activated when configuring with the devkitPro Switch toolchain
# (-DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/Switch.cmake). That toolchain
# sets CMAKE_SYSTEM_NAME=NintendoSwitch and Platform/NintendoSwitch.cmake
# sets NINTENDO_SWITCH=TRUE and adds -D__SWITCH__ to the common flags, which
# we use to detect that we are cross-compiling for the Switch.
#
# Mirrors cmake/platforms/vita.cmake. The big differences from Vita:
#   - aarch64 (ARM64) instead of armv7
#   - ~3-4 GB user RAM, so no hand-tuned heap split (sys_switch.c)
#   - real EGL + GLES2 via mesa/nouveau, so renderergl1 runs on actual
#     GL ES instead of vitaGL's GXM translation layer
#   - game/cgame are STATIC-linked into the NRO (libnx has no general
#     runtime code loader) instead of dlopen'd .suprx modules

if(NOT NINTENDO_SWITCH)
    return()
endif()

set(SWITCH TRUE)

message(STATUS "Configuring for Nintendo Switch (devkitPro: $ENV{DEVKITPRO})")

# Reuse the unix system layer as the base, then add the Switch shim that
# brings up libnx services and remaps the game data path to sdmc:.
list(APPEND SYSTEM_PLATFORM_SOURCES
    ${SOURCE_DIR}/sys/sys_unix.c
    ${SOURCE_DIR}/sys/new/sys_unix_new.c
    ${SOURCE_DIR}/sys/con_passive.c
    ${SOURCE_DIR}/sys/sys_switch.c
)

# Disable features that don't make sense (or aren't ready) on Switch.
set(BUILD_SERVER OFF CACHE INTERNAL "")
set(BUILD_RENDERER_GL2 OFF CACHE INTERNAL "")
# Renderer is static-linked into the NRO; no dlopen on Switch.
set(USE_RENDERER_DLOPEN OFF CACHE INTERNAL "")
set(USE_OPENAL_DLOPEN OFF CACHE INTERNAL "")
# Game/cgame built as STATIC libs and pulled into the NRO (see the
# SWITCH branch in basegame.cmake). No .suprx/.so dynamic path.
set(BUILD_GAME_LIBRARIES ON CACHE INTERNAL "")
set(BUILD_GAME_QVMS OFF CACHE INTERNAL "")
set(USE_HTTP OFF CACHE INTERNAL "")

# devkitPro portlibs cover SDL2 / OpenAL / ogg / opus / png / z, so use the
# system ones. Vorbisfile and mad are NOT in portlibs (only tremor), so
# build those two from the in-tree sources.
set(USE_INTERNAL_LIBS OFF CACHE INTERNAL "")
set(USE_INTERNAL_SDL OFF CACHE INTERNAL "")
set(USE_INTERNAL_ZLIB OFF CACHE INTERNAL "")
set(USE_INTERNAL_JPEG ON CACHE INTERNAL "")
set(USE_INTERNAL_OGG OFF CACHE INTERNAL "")
set(USE_INTERNAL_VORBIS ON CACHE INTERNAL "")
set(USE_INTERNAL_OPUS OFF CACHE INTERNAL "")
set(USE_INTERNAL_MAD ON CACHE INTERNAL "")
set(USE_VOIP OFF CACHE INTERNAL "")
set(USE_MUMBLE OFF CACHE INTERNAL "")
set(USE_FREETYPE OFF CACHE INTERNAL "")

# devkitA64's prebuilt libs aren't LTO objects; keep IPO off to avoid
# random ABI errors at link, same rationale as Vita.
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION FALSE)

add_compile_definitions(
    __SWITCH__
    USE_ICON
    ARCH_STRING="aarch64"
    HAVE_VM_COMPILED=0
    # renderergl1 talks to GLES2 via mesa; gl* entry points are declared
    # through the GLES2 headers and resolved by SDL_GL_GetProcAddress
    # (eglGetProcAddress under the hood), so we do NOT define
    # __SDL_NOGETPROCADDR__ here the way the Vita does for vitaGL.
    GL_GLEXT_PROTOTYPES
)

# devkitA64 already injects the correct -march/-mtune for the Switch's
# Cortex-A57; we only add engine-side tuning here.
add_compile_options(
    -fsigned-char
    # -ffast-math removed (test): it reassociates float ops and drops NaN/inf
    # handling, which can turn a normally-terminating float comparison loop
    # (AI pathfinding / navigation is float-heavy) into a non-terminating one
    # on aarch64. The level-load hang is always on AI entities, so rule this
    # out first.
    $<$<COMPILE_LANGUAGE:C>:-Wno-error=incompatible-pointer-types>
    $<$<COMPILE_LANGUAGE:C>:-Wno-incompatible-pointer-types>
)

# NOTE: game / cgame are static-linked into the single NRO but compile the
# corepp / script classes with different layouts than the engine, so they must
# NOT be merged. switch_isolate.sh (see basegame.cmake) gives each module a
# private symbol namespace, so the client links cleanly with no duplicate
# symbols — no --allow-multiple-definition needed.

# libnx + mesa/nouveau GL stack + codecs. Order matters for static link:
# SDL2 before EGL/GLES, GL stack before nx.
list(APPEND COMMON_LIBRARIES
    SDL2
    EGL
    GLESv2
    glapi
    drm_nouveau
    openal
    opusfile
    opus
    ogg
    png
    z
    m
    nx
)

set(CMAKE_DEFAULT_INSTALL_RUNTIME_DIR bin)
set(BIN_INSTALL_SUBDIR "")
set(LIB_INSTALL_SUBDIR "")

# NRO metadata.
set(SWITCH_APP_TITLE  "OpenMoHAA" CACHE STRING "Switch app title")
set(SWITCH_APP_AUTHOR "OpenMoHAA team" CACHE STRING "Switch app author")
set(SWITCH_APP_VERSION "1.0.0" CACHE STRING "Switch app version")

list(APPEND POST_CONFIGURE_FUNCTIONS package_switch_nro)

function(package_switch_nro)
    if(NOT TARGET ${CLIENT_BINARY})
        return()
    endif()

    set(ELF_FILE  $<TARGET_FILE:${CLIENT_BINARY}>)
    set(NACP_FILE ${CMAKE_BINARY_DIR}/${SWITCH_APP_TITLE}.nacp)
    set(NRO_FILE  ${CMAKE_BINARY_DIR}/${SWITCH_APP_TITLE}.nro)
    set(ICON_FILE ${CMAKE_SOURCE_DIR}/misc/switch/icon.jpg)

    find_program(NX_ELF2NRO_EXE  NAMES elf2nro  HINTS "$ENV{DEVKITPRO}/tools/bin")
    find_program(NX_NACPTOOL_EXE NAMES nacptool HINTS "$ENV{DEVKITPRO}/tools/bin")

    # nacptool builds the application metadata blob; elf2nro wraps the
    # stripped ELF + nacp + 256x256 JPEG icon into the final .nro that the
    # hbmenu (and Ryujinx) load.
    add_custom_command(TARGET ${CLIENT_BINARY} POST_BUILD
        COMMAND ${NX_NACPTOOL_EXE} --create
                "${SWITCH_APP_TITLE}" "${SWITCH_APP_AUTHOR}" "${SWITCH_APP_VERSION}"
                ${NACP_FILE}
        COMMAND ${NX_ELF2NRO_EXE} ${ELF_FILE} ${NRO_FILE}
                --nacp=${NACP_FILE}
                --icon=${ICON_FILE}
        COMMENT "Packaging ${NRO_FILE}"
        VERBATIM
    )
endfunction()
