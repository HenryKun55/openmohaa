if(NOT BUILD_GAME_LIBRARIES AND NOT BUILD_GAME_QVMS)
    return()
endif()

include(utils/set_output_dirs)
include(shared_script)

file(GLOB_RECURSE BG_SOURCES
    ${SOURCE_DIR}/fgame/bg_misc.cpp
    ${SOURCE_DIR}/fgame/bg_pmove.cpp
    ${SOURCE_DIR}/fgame/bg_slidemove.cpp
    ${SOURCE_DIR}/fgame/bg_voteoptions.cpp
)

file(GLOB_RECURSE CGAME_SOURCES
    ${SOURCE_DIR}/cgame/*.c ${SOURCE_DIR}/cgame/*.cpp
)

set(CGAME_BINARY_SOURCES)
set(CGAME_QVM_SOURCES)

file(GLOB_RECURSE GAME_SOURCES
    ${SOURCE_DIR}/fgame/*.c ${SOURCE_DIR}/fgame/*.cpp
    ${SOURCE_DIR}/script/*.c ${SOURCE_DIR}/script/*.cpp
	${SOURCE_DIR}/parser/parsetree.cpp
)

# Compile lexer and grammar files

if (FLEX_FOUND)
	flex_target(fgame-lexer ${CMAKE_SOURCE_DIR}/code/parser/lex_source.txt ${CMAKE_SOURCE_DIR}/code/parser/generated/yyLexer.cpp DEFINES_FILE ${CMAKE_SOURCE_DIR}/code/parser/generated/yyLexer.h COMPILE_FLAGS "-Cem --nounistd")
endif()

if (BISON_FOUND)
	bison_target(fgame-parser ${CMAKE_SOURCE_DIR}/code/parser/bison_source.txt ${CMAKE_SOURCE_DIR}/code/parser/generated/yyParser.cpp)
endif()

file(MAKE_DIRECTORY "${CMAKE_SOURCE_DIR}/code/parser/generated")

list(APPEND GAME_SOURCES
    ${BISON_fgame-parser_OUTPUTS}
    ${FLEX_fgame-lexer_OUTPUTS}
)

set(GAME_BINARY_SOURCES)
set(GAME_QVM_SOURCES)

set(UI_SOURCES)

set(UI_BINARY_SOURCES)
set(UI_QVM_SOURCES)

set(GAME_MODULE_SHARED_SOURCES
    ${SOURCE_DIR}/qcommon/q_math.c
    ${SOURCE_DIR}/qcommon/q_shared.c
    ${SCRIPT_SYSTEM_SOURCES}
)


set(CGAME_SOURCES_BASEGAME ${CGAME_SOURCES} ${GAME_MODULE_SHARED_SOURCES})
set(GAME_SOURCES_BASEGAME ${GAME_SOURCES} ${GAME_MODULE_SHARED_SOURCES})
set(UI_SOURCES_BASEGAME ${UI_SOURCES} ${GAME_MODULE_SHARED_SOURCES})

if(BUILD_GAME_LIBRARIES)
    set(CGAME_MODULE_BINARY ${CGAME_MODULE})
    set(GAME_MODULE_BINARY ${GAME_MODULE})
    set(UI_MODULE_BINARY ${UI_MODULE})

    if (BASEGAME)
        set(CGAME_MODULE_BINARY_BASEGAME ${CGAME_MODULE_BINARY}_${BASEGAME})
        set(GAME_MODULE_BINARY_BASEGAME ${GAME_MODULE_BINARY}_${BASEGAME})
        set(UI_MODULE_BINARY_BASEGAME ${UI_MODULE_BINARY}_${BASEGAME})
    else()
        set(CGAME_MODULE_BINARY_BASEGAME ${CGAME_MODULE_BINARY})
        set(GAME_MODULE_BINARY_BASEGAME ${GAME_MODULE_BINARY})
        set(UI_MODULE_BINARY_BASEGAME ${UI_MODULE_BINARY})
    endif()

    # On Vita the modules are built as real .suprx PRX files loaded via
    # sceKernelLoadStartModule (see code/sys/psp2/). Each module gets the
    # psp2_dll_hacks.c shim that provides module_start / module_stop and
    # the psp2_exports[] table, plus a -include of psp2_dll_defs.h to
    # remap libc allocation/IO to the engine's heap via sysfuncs_t.
    #
    # The recipe mirrors what vitaQuakeIII and vitaRTCW use. Output is
    # game.suprx / cgame.suprx, packed into the VPK at module/ for the
    # engine to dlopen at runtime.
    if(VITA)
        # vita_create_self / vita_create_stubs etc. live in vita.cmake,
        # which isn't auto-loaded by the toolchain — pull it in here.
        include("${VITASDK}/share/vita.cmake")
        set(VITA_PSP2_DIR ${SOURCE_DIR}/sys/psp2)
        set(VITA_MODULE_HACKS  ${VITA_PSP2_DIR}/psp2_dll_hacks.c)
        set(VITA_MODULE_DEFS   ${VITA_PSP2_DIR}/psp2_dll_defs.h)
        set(VITA_MODULE_FLAGS  -include ${VITA_MODULE_DEFS} -nostartfiles)
        # cgame .suprx ---------------------------------------------------
        add_executable(${CGAME_MODULE_BINARY_BASEGAME}.elf
            ${CGAME_SOURCES_BASEGAME} ${BG_SOURCES} ${CGAME_BINARY_SOURCES}
            ${VITA_MODULE_HACKS})
        target_compile_definitions(${CGAME_MODULE_BINARY_BASEGAME}.elf PRIVATE CGAME_DLL)
        target_compile_options(    ${CGAME_MODULE_BINARY_BASEGAME}.elf PRIVATE ${VITA_MODULE_FLAGS})
        target_include_directories(${CGAME_MODULE_BINARY_BASEGAME}.elf PRIVATE ${VITA_PSP2_DIR})
        target_link_options(       ${CGAME_MODULE_BINARY_BASEGAME}.elf PRIVATE -nostartfiles)
        target_link_libraries(     ${CGAME_MODULE_BINARY_BASEGAME}.elf PRIVATE
            SceLibKernel_stub_weak SceKernelModulemgr_stub_weak)
        vita_create_self(cgame_suprx ${CGAME_MODULE_BINARY_BASEGAME}.elf
            UNSAFE STRIPPED
            MODULE_ENTRY "module_start,module_stop,module_exit")
        # game .suprx ----------------------------------------------------
        add_executable(${GAME_MODULE_BINARY_BASEGAME}.elf
            ${GAME_SOURCES_BASEGAME} ${BG_SOURCES} ${GAME_BINARY_SOURCES}
            ${VITA_MODULE_HACKS})
        target_compile_definitions(${GAME_MODULE_BINARY_BASEGAME}.elf
            PRIVATE GAME_DLL WITH_SCRIPT_ENGINE ARCHIVE_SUPPORTED)
        target_compile_options(    ${GAME_MODULE_BINARY_BASEGAME}.elf PRIVATE ${VITA_MODULE_FLAGS})
        target_include_directories(${GAME_MODULE_BINARY_BASEGAME}.elf PRIVATE ${VITA_PSP2_DIR})
        target_link_options(       ${GAME_MODULE_BINARY_BASEGAME}.elf PRIVATE -nostartfiles)
        target_link_libraries(     ${GAME_MODULE_BINARY_BASEGAME}.elf PRIVATE
            RecastNavigation::Detour RecastNavigation::DetourCrowd RecastNavigation::Recast
            SceLibKernel_stub_weak SceKernelModulemgr_stub_weak)
        vita_create_self(game_suprx ${GAME_MODULE_BINARY_BASEGAME}.elf
            UNSAFE STRIPPED
            MODULE_ENTRY "module_start,module_stop,module_exit")
        # vita_create_self creates suffixed targets (foo-velf / foo-self).
        # The final -self target produces the .suprx file we ship.
        if(TARGET ${CLIENT_BINARY})
            add_dependencies(${CLIENT_BINARY} cgame_suprx-self game_suprx-self)
        endif()
    else()
        add_library(                ${CGAME_MODULE_BINARY_BASEGAME} SHARED ${CGAME_SOURCES_BASEGAME} ${BG_SOURCES} ${CGAME_BINARY_SOURCES})
        target_compile_definitions( ${CGAME_MODULE_BINARY_BASEGAME} PRIVATE CGAME_DLL)
        target_link_libraries(      ${CGAME_MODULE_BINARY_BASEGAME} PRIVATE ${COMMON_LIBRARIES})
        set_target_properties(      ${CGAME_MODULE_BINARY_BASEGAME} PROPERTIES OUTPUT_NAME ${CGAME_MODULE_BINARY})
        set_output_dirs(            ${CGAME_MODULE_BINARY_BASEGAME} SUBDIRECTORY ${BASEGAME})
        INSTALL(TARGETS ${CGAME_MODULE_BINARY_BASEGAME} DESTINATION ${INSTALL_LIBDIR_FULL})
        if(MSVC)
            INSTALL(FILES $<TARGET_PDB_FILE:${CGAME_MODULE_BINARY_BASEGAME}> DESTINATION ${INSTALL_LIBDIR_FULL} OPTIONAL)
        endif()

        add_library(                ${GAME_MODULE_BINARY_BASEGAME} SHARED ${GAME_SOURCES_BASEGAME} ${BG_SOURCES} ${GAME_BINARY_SOURCES})
        target_compile_definitions( ${GAME_MODULE_BINARY_BASEGAME} PRIVATE GAME_DLL WITH_SCRIPT_ENGINE ARCHIVE_SUPPORTED)
        target_link_libraries(      ${GAME_MODULE_BINARY_BASEGAME} PRIVATE RecastNavigation::Detour RecastNavigation::DetourCrowd RecastNavigation::Recast)
        target_link_libraries(      ${GAME_MODULE_BINARY_BASEGAME} PRIVATE ${COMMON_LIBRARIES})
        set_target_properties(      ${GAME_MODULE_BINARY_BASEGAME} PROPERTIES OUTPUT_NAME ${GAME_MODULE_BINARY})
        set_output_dirs(            ${GAME_MODULE_BINARY_BASEGAME} SUBDIRECTORY ${BASEGAME})
        INSTALL(TARGETS ${GAME_MODULE_BINARY_BASEGAME} DESTINATION ${INSTALL_LIBDIR_FULL})
        if(MSVC)
            INSTALL(FILES $<TARGET_PDB_FILE:${GAME_MODULE_BINARY_BASEGAME}> DESTINATION ${INSTALL_LIBDIR_FULL} OPTIONAL)
        endif()
    endif()

    #add_library(                ${UI_MODULE_BINARY_BASEGAME} SHARED ${UI_SOURCES_BASEGAME} ${UI_BINARY_SOURCES})
    #target_compile_definitions( ${UI_MODULE_BINARY_BASEGAME} PRIVATE UI)
    #set_target_properties(      ${UI_MODULE_BINARY_BASEGAME} PROPERTIES OUTPUT_NAME ${UI_MODULE_BINARY})
    #set_output_dirs(            ${UI_MODULE_BINARY_BASEGAME} SUBDIRECTORY ${BASEGAME})
endif()
