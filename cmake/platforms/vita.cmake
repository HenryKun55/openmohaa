# PlayStation Vita specific settings.
#
# Activated when configuring with the vitasdk toolchain
# (-DCMAKE_TOOLCHAIN_FILE=$VITASDK/share/vita.toolchain.cmake).
# The toolchain file sets CMAKE_SYSTEM_NAME=Generic and defines the
# VITA_ELF_CREATE/VITA_MAKE_FSELF/VITA_MKSFOEX cache variables, which we
# use to detect that we are cross-compiling for the Vita.

if(NOT DEFINED VITA_ELF_CREATE)
    return()
endif()

set(VITA TRUE)

message(STATUS "Configuring for PlayStation Vita (vitasdk: ${VITASDK})")

# Reuse the unix system layer as the base, then add a tiny Vita shim that
# initialises sceCtrl/sceTouch and remaps the game data path to ux0:.
list(APPEND SYSTEM_PLATFORM_SOURCES
    ${SOURCE_DIR}/sys/sys_unix.c
    ${SOURCE_DIR}/sys/new/sys_unix_new.c
    ${SOURCE_DIR}/sys/con_passive.c
    ${SOURCE_DIR}/sys/sys_vita.c
    # New version check / download (the install is done by misc/vita/updater).
    ${SOURCE_DIR}/sys/vita_update.c
    ${SOURCE_DIR}/sys/vita_net.c
    ${SOURCE_DIR}/sys/vita_bootui.c
    ${SOURCE_DIR}/sys/vita_corepp_shims.cpp
    # Custom dlopen wrapping sceKernelLoadStartModule for .suprx — used
    # by sys_loadlib.h on Vita (SDL_LoadObject is stubbed in SDL2-Vita).
    # Same pattern as vitaQuakeIII / vitaRTCW.
    ${SOURCE_DIR}/sys/psp2/dll_psp2.c
)

# Provide GL stubs that vitaGL doesn't expose (used by the renderer's
# function-pointer table even when those entry points are never called).
list(APPEND CLIENT_PLATFORM_SOURCES
    ${SOURCE_DIR}/sdl/vita_gl_stubs.c
    ${SOURCE_DIR}/sdl/vita_boot_splash.c
)

# Disable features that don't make sense (or aren't ready) on Vita.
# These are forced regardless of the user's command line, similar to
# how emscripten.cmake hard-overrides them.
set(BUILD_SERVER OFF CACHE INTERNAL "")
set(BUILD_RENDERER_GL2 OFF CACHE INTERNAL "")
set(USE_RENDERER_DLOPEN OFF CACHE INTERNAL "")
set(USE_OPENAL_DLOPEN OFF CACHE INTERNAL "")
# Game modules are built as STATIC libraries on Vita (see basegame.cmake)
# and pulled into the eboot via client.cmake's target_link_libraries.
# SDL_LoadObject() isn't implemented on SDL2-Vita, so the desktop
# .suprx/.so dlopen path is unreachable.
set(BUILD_GAME_LIBRARIES ON CACHE INTERNAL "")
set(BUILD_GAME_QVMS OFF CACHE INTERNAL "")
set(USE_HTTP OFF CACHE INTERNAL "")
# Note on audio: snd_openal_new.cpp is OpenMoHAA's new sound layer and
# isn't gated by USE_OPENAL — disabling OpenAL leaves hundreds of
# unresolved alXxx symbols. We therefore stay on vitasdk's OpenAL Soft
# even though the Vita3K emulator currently routes the samples through
# its sceAudio stub and produces no host audio.
set(USE_INTERNAL_LIBS OFF CACHE INTERNAL "")
set(USE_INTERNAL_SDL OFF CACHE INTERNAL "")
set(USE_INTERNAL_ZLIB OFF CACHE INTERNAL "")
set(USE_INTERNAL_JPEG OFF CACHE INTERNAL "")
set(USE_INTERNAL_OGG OFF CACHE INTERNAL "")
set(USE_INTERNAL_VORBIS OFF CACHE INTERNAL "")
set(USE_INTERNAL_OPUS OFF CACHE INTERNAL "")
set(USE_INTERNAL_MAD OFF CACHE INTERNAL "")
set(USE_VOIP OFF CACHE INTERNAL "")
set(USE_MUMBLE OFF CACHE INTERNAL "")
set(USE_FREETYPE OFF CACHE INTERNAL "")

# vitasdk's GCC supports IPO/LTO but the prebuilt vita libs aren't
# LTO-enabled, so the link will fail with random ABI errors. Force off.
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION FALSE)

# Compile flags tuned for the Cortex-A9 with NEON. -Wl,-q is required by
# vita-elf-create so that ELF relocation entries survive into the .velf.
add_compile_definitions(
    __PSP2__
    __vita__
    USE_ICON
    ARCH_STRING="arm"
    HAVE_VM_COMPILED=0
    # vitaGL is statically linked; qgl* must resolve to direct gl*
    # symbols, not via SDL_GL_GetProcAddress (which has nothing to load).
    __SDL_NOGETPROCADDR__
    # SDL_opengl_glext.h gates the actual gl* declarations behind this
    # macro — without it only PFN*PROC typedefs are visible, and our
    # qgl##name = gl##name assignments fail to compile.
    GL_GLEXT_PROTOTYPES
)

add_compile_options(
    -mfpu=neon
    -mcpu=cortex-a9
    -mfloat-abi=hard
    -fsigned-char
    -fno-short-enums
    -ffast-math
    -Wl,-q
    # Suppress C++ unwind table generation. fgame's scriptmaster.cpp and
    # cgame's cg_commands.cpp use try/catch around ScriptException; the
    # resulting .ARM.exidx / .ARM.extab sections produce R_ARM_BASE_PREL
    # (reloc type 25) which vita-elf-create rejects. Without these
    # tables an unhandled C++ exception terminates the process — which
    # is what a Vita would do anyway.
    -fno-unwind-tables
    -fno-asynchronous-unwind-tables
    # Per-function/data sections + --gc-sections in the linker so dead
    # code (and the GOT references it generates) actually gets dropped
    # from the final ELF. Without this, every helper in fgame stays in
    # and pulls along stuff that emits R_ARM_BASE_PREL (reloc 25)
    # against _GLOBAL_OFFSET_TABLE_, which vita-elf-create rejects.
    -ffunction-sections
    -fdata-sections
    # Belt-and-braces against GOT-emitting codegen. Newer GCCs default to
    # PIC on ARM in places where vitasdk's older vita-elf-create can't
    # cope — force everything to absolute addressing.
    -fno-pic
    -fno-PIC
    -fno-pie
    # vitaGL drops a few `const` qualifiers and uses uint32_t where desktop
    # headers use GLenum; the qgl* assignments still bind to the same
    # callable, but -Werror would otherwise reject them. Gate to C only —
    # the flag is meaningless for C++ and cc1plus warns about it.
    $<$<COMPILE_LANGUAGE:C>:-Wno-error=incompatible-pointer-types>
    $<$<COMPILE_LANGUAGE:C>:-Wno-incompatible-pointer-types>
)

add_link_options(
    -Wl,-q
    -Wl,--allow-multiple-definition
    # Static executable, no PIE — keeps the linker from synthesising
    # GOT slots that would emit R_ARM_BASE_PREL (reloc 25) into
    # .rel.text. vita-elf-create only knows the absolute reloc types
    # (TARGET1/TARGET2/ABS32/THM_*/PREL31). With the larger fgame +
    # cgame static libs now linked in, the GOT was getting populated
    # and triggering the reject.
    -static
    -Wl,--no-eh-frame-hdr
    # Match the compile-side -fno-short-enums so the linker emits the
    # final ELF with 32-bit enums too. Without this, every engine .obj
    # triggers "uses 32-bit enums yet output uses variable-size enums".
    -fno-short-enums
    # vitasdk's libstdc++.a / libm.a ship built with ARM EABI default
    # (short enums), so after the engine side is fixed they complain
    # the other way. Silence the residual lib mismatch — no enum
    # values are passed across this boundary in our code (C++ runtime
    # + scalar math, neither shares enum types with the engine).
    -Wl,--no-warn-mismatch
)

# vitasdk libraries OpenMoHAA links against. The renderer pulls in vitaGL
# (OpenGL ES wrapper to GXM) plus the shader compiler, the Sce* stubs are
# the platform syscall trampolines that vita-elf-create resolves later.
list(APPEND COMMON_LIBRARIES
    SDL2
    pthread
    vitaGL
    vitashark
    SceShaccCgExt
    mathneon
    openal
    vorbisfile
    vorbis
    ogg
    opusfile
    opus
    mad
    curl
    ssl
    crypto
    z
    zstd # used by curl
    bz2
    png
    jpeg
    m
    SceShaccCg_stub
    SceCtrl_stub
    SceTouch_stub
    SceMotion_stub
    SceAudio_stub
    SceAudioIn_stub
    SceDisplay_stub
    SceGxm_stub
    SceCommonDialog_stub
    SceSysmodule_stub
    ScePower_stub
    SceRtc_stub
    SceNet_stub
    SceNetCtl_stub
    SceAppMgr_stub
    SceAppUtil_stub
    ScePgf_stub
    SceFiber_stub
    SceHid_stub
    SceLibKernel_stub
    SceProcessmgr_stub
    SceKernelDmacMgr_stub
    SceSsl_stub
    taihen_stub
    kubridge_stub
    dl
)

# Strip the system installation paths used by linux.cmake. Output goes
# next to the build directory and is then packed into a .vpk by the
# post-configure step below.
set(CMAKE_DEFAULT_INSTALL_RUNTIME_DIR bin)
set(BIN_INSTALL_SUBDIR "")
set(LIB_INSTALL_SUBDIR "")

# VPK metadata. TITLE_ID must be 9 chars (4 letters + 5 digits) and
# unique on the device. OMHA00001 is unused on the public title-id list.
set(VITA_TITLEID "OMHA00001" CACHE STRING "Vita title id (9 chars)")
set(VITA_VERSION "01.00" CACHE STRING "Vita app version")
# The port's release (the GitHub tags are v<this>-vita). The update check installs a
# release only when its version is higher: raise it for every release.
set(VITA_PORT_VERSION "0.4" CACHE STRING "Vita port release (GitHub tag v<this>-vita)")
set_source_files_properties(${SOURCE_DIR}/sys/vita_update.c PROPERTIES
    COMPILE_DEFINITIONS VITA_PORT_VERSION="${VITA_PORT_VERSION}")
set(VITA_APP_NAME "OpenMoHAA" CACHE STRING "LiveArea app name")

list(APPEND POST_CONFIGURE_FUNCTIONS package_vita_vpk)

function(package_vita_vpk)
    if(NOT TARGET ${CLIENT_BINARY})
        return()
    endif()

    set(ELF_FILE $<TARGET_FILE:${CLIENT_BINARY}>)
    set(VELF_FILE ${CMAKE_BINARY_DIR}/${CLIENT_BINARY}.velf)
    set(EBOOT_FILE ${CMAKE_BINARY_DIR}/eboot.bin)
    set(SFO_FILE ${CMAKE_BINARY_DIR}/param.sfo)
    set(VPK_FILE ${CMAKE_BINARY_DIR}/${VITA_APP_NAME}.vpk)

    set(VITA_SCE_DIR ${CMAKE_SOURCE_DIR}/misc/vita/sce_sys)

    # Bubble / LiveArea / loading-screen art. misc/vita/make_art.py builds it from the
    # user's own game data into misc/vita/art_local (git-ignored: it is EA's art);
    # without it the generic OpenMoHAA art in misc/vita/sce_sys is packed.
    set(VITA_ART_DIR ${CMAKE_SOURCE_DIR}/misc/vita/art_local)
    set(VITA_BUBBLE_TITLE "${VITA_APP_NAME}")
    set(VITA_ICON_DIR ${VITA_SCE_DIR})
    set(VITA_SPLASH_ADD "")
    set(VITA_ART_DEPENDS "")
    if(EXISTS ${VITA_ART_DIR}/sce_sys/icon0.png)
        set(VITA_ICON_DIR ${VITA_ART_DIR}/sce_sys)
        set(VITA_BUBBLE_TITLE "Medal of Honor: Allied Assault")
        list(APPEND VITA_ART_DEPENDS
            ${VITA_ART_DIR}/sce_sys/icon0.png
            ${VITA_ART_DIR}/sce_sys/livearea/contents/bg.png
            ${VITA_ART_DIR}/sce_sys/livearea/contents/startup.png)
    endif()
    set(VITA_PIC0_ADD "")
    if(EXISTS ${VITA_ART_DIR}/sce_sys/pic0.png)
        # Boot picture: shown by the Vita from the bubble tap, kept by sys_vita.c until
        # vitaGL starts.
        set(VITA_PIC0_ADD --add ${VITA_ART_DIR}/sce_sys/pic0.png=sce_sys/pic0.png)
        list(APPEND VITA_ART_DEPENDS ${VITA_ART_DIR}/sce_sys/pic0.png)
    endif()
    if(EXISTS ${VITA_ART_DIR}/splash.png)
        # Shown by sdl/vita_boot_splash.c during boot and level loads.
        set(VITA_SPLASH_ADD --add ${VITA_ART_DIR}/splash.png=splash.png)
        list(APPEND VITA_ART_DEPENDS ${VITA_ART_DIR}/splash.png)
    endif()

    # vita-elf-create chokes on R_ARM_BASE_PREL (reloc 25) emitted by the
    # C++ unwind tables (.ARM.exidx / .ARM.extab) — fgame and cgame both
    # use try/catch for ScriptException, which is enough to generate
    # those sections. Strip them before vita-elf-create; the engine
    # doesn't depend on stack-unwinding (an unhandled C++ exception
    # would terminate the process anyway on a constrained device).
    add_custom_command(TARGET ${CLIENT_BINARY} POST_BUILD
        COMMAND ${CMAKE_STRIP} -g ${ELF_FILE}
        COMMAND ${VITA_ELF_CREATE} ${ELF_FILE} ${VELF_FILE}
        COMMAND ${VITA_MAKE_FSELF} -s ${VELF_FILE} ${EBOOT_FILE}
        COMMAND ${VITA_MKSFOEX} -s TITLE_ID=${VITA_TITLEID}
                                -d ATTRIBUTE2=12
                                "${VITA_BUBBLE_TITLE}" ${SFO_FILE}
        COMMENT "Creating ${EBOOT_FILE}"
        VERBATIM
    )

    # Packaging is its own target so the VPK is rebuilt when only game/cgame change
    # (as a POST_BUILD step of the client it only ran when the eboot relinked, and a
    # cgame-only fix silently shipped the old cgame.suprx).
    # Text translations (misc/vita/lang/<code>.txt, UTF-8 so they are easy to edit) are
    # packed as main/vita/lang/<code>.txt in Latin-1, the encoding of the game's fonts.
    file(GLOB VITA_LANG_SOURCES ${CMAKE_SOURCE_DIR}/misc/vita/lang/*.txt)
    set(VITA_LANG_ADD "")
    set(VITA_LANG_FILES "")
    foreach(LANG_SRC ${VITA_LANG_SOURCES})
        get_filename_component(LANG_NAME ${LANG_SRC} NAME)
        set(LANG_OUT ${CMAKE_BINARY_DIR}/vita_lang/${LANG_NAME})
        add_custom_command(OUTPUT ${LANG_OUT}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/vita_lang
            # Fails on a character the fonts cannot show (outside Latin-1).
            COMMAND python3 -c "import sys; open(sys.argv[2], 'wb').write(open(sys.argv[1], encoding='utf-8').read().encode('latin-1'))" ${LANG_SRC} ${LANG_OUT}
            DEPENDS ${LANG_SRC}
            COMMENT "Converting ${LANG_NAME} to Latin-1"
            VERBATIM)
        list(APPEND VITA_LANG_ADD --add ${LANG_OUT}=main/vita/lang/${LANG_NAME})
        list(APPEND VITA_LANG_FILES ${LANG_OUT})
    endforeach()

    # updater.bin: puts a downloaded release in place of the game's files (misc/vita/updater).
    # A separate program, so the game is closed while its files are replaced; not a "safe"
    # self (-s), as only unsafe homebrew may write to ux0:app.
    set(UPDATER_DIR ${CMAKE_SOURCE_DIR}/misc/vita/updater)
    add_executable(vita_updater ${UPDATER_DIR}/updater.c ${UPDATER_DIR}/debugScreen.c)
    target_link_libraries(vita_updater
        png crypto z m
        SceDisplay_stub SceCtrl_stub SceSysmodule_stub ScePromoterUtil_stub SceAppMgr_stub
        ScePower_stub SceLibKernel_stub SceKernelThreadMgr_stub SceIofilemgr_stub SceProcessmgr_stub)
    set(UPDATER_BIN ${CMAKE_BINARY_DIR}/updater.bin)
    add_custom_command(TARGET vita_updater POST_BUILD
        COMMAND ${CMAKE_STRIP} -g $<TARGET_FILE:vita_updater>
        COMMAND ${VITA_ELF_CREATE} $<TARGET_FILE:vita_updater> ${CMAKE_BINARY_DIR}/vita_updater.velf
        COMMAND ${VITA_MAKE_FSELF} ${CMAKE_BINARY_DIR}/vita_updater.velf ${UPDATER_BIN}
        COMMENT "Creating ${UPDATER_BIN}"
        VERBATIM
    )

    add_custom_command(OUTPUT ${VPK_FILE}
        COMMAND ${VITASDK}/bin/vita-pack-vpk
                    -s ${SFO_FILE}
                    -b ${EBOOT_FILE}
                    --add ${VITA_ICON_DIR}/icon0.png=sce_sys/icon0.png
                    --add ${VITA_ICON_DIR}/livearea/contents/bg.png=sce_sys/livearea/contents/bg.png
                    --add ${VITA_ICON_DIR}/livearea/contents/startup.png=sce_sys/livearea/contents/startup.png
                    ${VITA_SPLASH_ADD}
                    ${VITA_PIC0_ADD}
                    --add ${VITA_SCE_DIR}/livearea/contents/template.xml=sce_sys/livearea/contents/template.xml
                    --add ${CMAKE_SOURCE_DIR}/misc/vita/main/vita_autoexec.cfg=main/vita_autoexec.cfg
                    --add ${CMAKE_SOURCE_DIR}/misc/vita/main/vita_defaults.cfg=main/vita_defaults.cfg
                    # Settings menu font with accented letters (misc/vita/make_font.py).
                    --add ${CMAKE_SOURCE_DIR}/misc/vita/main/fonts/vita-14.RitualFont=main/fonts/vita-14.RitualFont
                    --add ${CMAKE_SOURCE_DIR}/misc/vita/main/fonts/DejaVu-LICENSE.txt=main/fonts/DejaVu-LICENSE.txt
                    # Start-up screens font (misc/vita/make_boot_font.py, Oswald: OFL)
                    --add ${CMAKE_SOURCE_DIR}/misc/vita/main/fonts/vita-boot.font=main/fonts/vita-boot.font
                    --add ${CMAKE_SOURCE_DIR}/misc/vita/fonts/OFL.txt=main/fonts/Oswald-OFL.txt
                    --add ${CMAKE_SOURCE_DIR}/misc/vita/main/gfx/fonts/vita-14.tga=main/gfx/fonts/vita-14.tga
                    --add ${CMAKE_SOURCE_DIR}/misc/vita/main/scripts/vita.shader=main/scripts/vita.shader
                    ${VITA_LANG_ADD}
                    # Game / cgame PRX modules at the VPK root so they
                    # sit next to eboot.bin and Sys_LoadDll finds them
                    # via Sys_BinaryPath = "app0:" (set in sys_vita.c).
                    --add ${CMAKE_BINARY_DIR}/game_suprx=game.suprx
                    --add ${CMAKE_BINARY_DIR}/cgame_suprx=cgame.suprx
                    --add ${UPDATER_BIN}=updater.bin
                    # CA certificates for the update check's HTTPS (curl.se/ca, MPL 2.0)
                    --add ${UPDATER_DIR}/cacert.pem=cacert.pem
                    ${VPK_FILE}
        # entries v0.3's in-game updater could not unpack are stored uncompressed
        COMMAND python3 ${CMAKE_SOURCE_DIR}/misc/vita/vpk_compat.py ${VPK_FILE}
        DEPENDS ${EBOOT_FILE}
                ${CMAKE_BINARY_DIR}/game_suprx
                ${CMAKE_BINARY_DIR}/cgame_suprx
                vita_updater
                ${UPDATER_DIR}/cacert.pem
                ${CMAKE_SOURCE_DIR}/misc/vita/vpk_compat.py
                ${CMAKE_SOURCE_DIR}/misc/vita/main/vita_autoexec.cfg
                ${CMAKE_SOURCE_DIR}/misc/vita/main/vita_defaults.cfg
                ${CMAKE_SOURCE_DIR}/misc/vita/main/fonts/vita-14.RitualFont
                ${CMAKE_SOURCE_DIR}/misc/vita/main/gfx/fonts/vita-14.tga
                ${CMAKE_SOURCE_DIR}/misc/vita/main/fonts/vita-boot.font
                ${CMAKE_SOURCE_DIR}/misc/vita/main/scripts/vita.shader
                ${VITA_LANG_FILES}
                ${VITA_ART_DEPENDS}
        COMMENT "Packaging ${VPK_FILE}"
        VERBATIM
    )
    add_custom_target(vita_vpk ALL DEPENDS ${VPK_FILE})
    add_dependencies(vita_vpk ${CLIENT_BINARY})
endfunction()
