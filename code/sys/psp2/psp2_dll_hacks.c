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

/* Per-module libc heap. The host engine has its own (256 MiB on Vita
 * — see sys_vita.c). Game/cgame use the engine's malloc through the
 * sysfuncs_t indirection, this 2 MiB is just for libc's own bookkeeping
 * before / between SYS_* calls. */
unsigned int _newlib_heap_size_user = 2 * 1024 * 1024;

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
