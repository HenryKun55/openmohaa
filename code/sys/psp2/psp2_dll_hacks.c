/*
 * psp2_dll_hacks.c — module-side shim for OpenMoHAA fgame/cgame .suprx
 *
 * sceKernelLoadStartModule calls our `module_start` when the engine
 * dlopens this module. We initialise newlib (private reent + heap),
 * run the static initialisers via __libc_init_array, then publish the
 * `psp2_exports` table that maps "GetGameAPI" / "GetCGameAPI" to the
 * C++ symbols fgame / cgame define. The engine's custom dlopen
 * (sys/psp2/dll_psp2.c) walks this table on dlsym().
 *
 * Source: adapted from vitaRTCW's `code/psp2/psp2_dll_hacks.c`. The
 * differences for OpenMoHAA are the exports (GetGameAPI vs vmMain) and
 * splitting fgame from cgame via GAME_DLL / CGAME_DLL.
 */
#include <stdint.h>
#include <vitasdk.h>
#include <psp2/kernel/clib.h>
#include <stdio.h>
#include <stdlib.h>

#include "psp2_dll_imports.h"

void* __dso_handle = (void*) &__dso_handle;

extern void _init_vita_reent( void );
extern void _free_vita_reent( void );
extern void _init_vita_heap( void );
extern void _free_vita_heap( void );

extern void __libc_init_array( void );
extern void __libc_fini_array( void );

void _init_vita_newlib( void )
{
    _init_vita_heap( );
    _init_vita_reent( );
}

void _free_vita_newlib( void )
{
    _free_vita_reent( );
    _free_vita_heap( );
}

void _fini( void ) { }
void _init( void ) { }

/* Per-module libc heap. The host engine reserves 96 MiB (see sys_vita.c)
 * so each .suprx gets a large chunk for libstdc++'s `operator new` —
 * tikis, animations, scripts. Pre-`#define malloc SYS_MALLOC` engine
 * paths still bypass via the imports table, but libstdc++ landed here
 * because it was compiled separately. Sizes:
 *   GAME_DLL  (fgame)  → 120 MiB — heaviest C++ allocator
 *   CGAME_DLL (cgame)  → 100 MiB — second heaviest, mostly model/anim
 * 96 + 120 + 100 ≈ 316 MiB which together with vitaGL / SDL2 /
 * OpenAL / sceLibc fits inside the ~360 MiB user-RAM budget. */
unsigned int _newlib_heap_size_user = 2 * 1024 * 1024;

/* (Removed: operator new/delete override.) Re-routing the C++ allocator
 * symbols (_Znwj/_Znaj/_ZdlPv/_ZdaPv) caused the static-link layout to
 * leave libstdc++'s __cxa_guard_release with unrelocated pointers,
 * which crashed in module_start's static-init guard handshake before
 * fgame had a chance to run. We're back on the .suprx module's tiny
 * libc heap for C++ new — the memory cap stays at ~2 MiB until we
 * find a different route (e.g. a libstdc++-aware allocator hook). */

typedef struct modarg_s
{
    sysfuncs_t imports;
    dllexport_t *exports;
} modarg_t;

sysfuncs_t g_engsysfuncs;

/* The actual entry points: each module's main TU defines exactly one. */
#if defined(GAME_DLL)
extern void *GetGameAPI(void *import);
dllexport_t psp2_exports[] = {
    { "GetGameAPI", (void *)GetGameAPI },
    { NULL, NULL },
};
#elif defined(CGAME_DLL)
extern void *GetCGameAPI(void);
dllexport_t psp2_exports[] = {
    { "GetCGameAPI", (void *)GetCGameAPI },
    { NULL, NULL },
};
#else
#error "psp2_dll_hacks.c must be compiled with GAME_DLL or CGAME_DLL"
#endif

int module_stop( SceSize argc, const void *args )
{
    (void)argc; (void)args;
    __libc_fini_array( );
    _free_vita_newlib( );
    return SCE_KERNEL_STOP_SUCCESS;
}

int module_exit( void )
{
    __libc_fini_array( );
    _free_vita_newlib( );
    return SCE_KERNEL_STOP_SUCCESS;
}

void _start() __attribute__ ((weak, alias ("module_start")));
int module_start( SceSize argc, void *args )
{
    (void)argc;
    _init_vita_newlib( );
    __libc_init_array( );

    modarg_t *arg = *(modarg_t **)args;
    arg->exports = psp2_exports;
    g_engsysfuncs = arg->imports;

    return SCE_KERNEL_START_SUCCESS;
}
