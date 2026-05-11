/*
 * PlayStation Vita platform shim for OpenMoHAA.
 *
 * The bulk of POSIX-y system code is reused from sys_unix.c via vitasdk's
 * newlib. This file only carries the bits that newlib + SDL2-Vita do not
 * already cover:
 *
 *   - SDK-wide heap/stack budget (vitasdk reads these symbols at startup,
 *     defaults are too small to load a Q3-class engine)
 *   - SceSysmodule loads we want eagerly available (network, motion)
 *   - Path overrides so that all reads/writes happen under
 *     ux0:data/openmohaa/ instead of $HOME, which doesn't exist on Vita
 *
 * SDL2-Vita already calls sceCtrl / sceTouch / sceAudioOut init from its
 * own bootstrap, so we don't redo those here.
 */

#ifdef __vita__

#include <psp2/kernel/processmgr.h>
#include <psp2/sysmodule.h>
#include <psp2/io/stat.h>
#include <psp2/touch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

unsigned int _newlib_heap_size_user = 256 * 1024 * 1024;
unsigned int sceLibcHeapSize         = 16  * 1024 * 1024;
unsigned int _pthread_stack_default_user = 2 * 1024 * 1024;

#define VITA_DATA_ROOT "ux0:data/openmohaa"
#define VITA_BOOT_LOG  VITA_DATA_ROOT "/main/boot.log"

void Sys_PlatformInit(void)
{
    sceSysmoduleLoadModule(SCE_SYSMODULE_NET);

    sceIoMkdir("ux0:data", 0777);
    sceIoMkdir(VITA_DATA_ROOT, 0777);
    sceIoMkdir(VITA_DATA_ROOT "/main", 0777);

    /* Start touch sampling on both pads. SDL2-Vita exposes the touch
     * devices (SDL_GetNumTouchDevices returned 2 in our boot.log) but its
     * own pump doesn't call sceTouchPeek, so no FINGER/MOUSEMOTION events
     * ever fire from finger taps. We poll the hardware ourselves from
     * sdl_input.c::IN_VitaPollTouch and synthesise SDL mouse events. */
    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
    sceTouchSetSamplingState(SCE_TOUCH_PORT_BACK,  SCE_TOUCH_SAMPLING_STATE_START);

    /* Redirect stdout to boot.log; vitasdk newlib defines `stderr` as the
     * lvalue `_REENT->_stderr`, so just point it at stdout so engine
     * output via CON_Print (fputs(stderr, …)) and our own fprintf(stdout)
     * share one FILE * and one offset. Two FILE *s over the same path
     * would stomp each other's offsets and we'd lose data. */
    freopen(VITA_BOOT_LOG, "w", stdout);
    setvbuf(stdout, NULL, _IOLBF, 0);
    stderr = stdout;

    fprintf(stdout, "=== OpenMoHAA Vita boot.log ===\n");
    fflush(stdout);

    /* Tell Sys_LoadDll where to find the game / cgame .suprx modules.
     * They live at the root of the installed app (app0:/) alongside
     * eboot.bin — see vita.cmake's VPK packaging step. FS_Startup also
     * uses Sys_BinaryPath as the install-side base, appending "/main"
     * to find autoexec.cfg there; "app0:" satisfies both. */
    extern void Sys_SetBinaryPath(const char *path);
    Sys_SetBinaryPath("app0:");
}

void Sys_PlatformExit(void)
{
    sceKernelExitProcess(0);
}

/* Sys_DefaultHomePath / InstallPath / HomeConfigPath etc. all live in
 * sys_unix.c under a __vita__ branch — keeping them in one place avoids
 * signature drift with qcommon.h. */

#endif /* __vita__ */
