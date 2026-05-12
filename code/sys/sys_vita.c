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
#include <psp2/kernel/sysmem.h>
#include <psp2/sysmodule.h>
#include <psp2/io/stat.h>
#include <psp2/touch.h>
#include <psp2/power.h>
#include <psp2/appmgr.h>
#include <psp2/apputil.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "psp2/dll_psp2.h"

/* OpenMoHAA's m1l1 (Mission 1 Level 1 — Submarine) is much larger than the
 * mission briefings: ~20k animation channels, dozens of unique tikis and
 * a 50MB BSP. The first 256 MiB heap hit std::bad_alloc partway through
 * the level load. 340 MiB was rejected by the Vita kernel (the app
 * failed to even start — module loader saw the request exceeded the
 * user-mode budget once SDL2/vitaGL/OpenAL reservations were factored
 * in). 300 MiB clears that hurdle; m1l1 still needs r_picmip 2 to keep
 * vitaGL's GPU-shared allocator from competing with us for the rest. */
/* Memory budget on Vita is split across THREE newlib heaps that all
 * pre-reserve from the same ~360 MiB user-RAM pool:
 *   - main process (this file)
 *   - fgame.suprx  (psp2_dll_hacks.c, GAME_DLL build)
 *   - cgame.suprx  (psp2_dll_hacks.c, CGAME_DLL build)
 *
 * Earlier the main heap was 300 MiB and the .suprx heaps were 2 MiB each;
 * empirically the engine only commits ~50 MiB of its 300 MiB cap during
 * m1l1 load while fgame's C++ `new` (tikis, animations) exhausts its
 * tiny 2 MiB and throws std::bad_alloc.
 *
 * Rebalanced: drop the engine cap to 96 MiB (covers observed peak with
 * headroom) so the .suprx modules can claim 120 + 100 MiB for their own
 * libstdc++ allocations. Total budget ≈ 320 MiB user — fits with the
 * rest of the runtime (SDL2 / vitaGL / OpenAL / sceLibc) inside 360 MiB. */
/* Cap the engine's newlib heap at 240 MiB instead of the previous 300.
 * The C++-allocator override in psp2_cpp_alloc.cpp now routes the
 * .suprx modules' `new`/`delete` straight to the engine heap, so fgame
 * + cgame no longer need 2 MiB-each side heaps + the engine's gi.Malloc
 * usage stays well under 240 MiB during m1l1 load (peaks observed
 * around 60 MiB in the last run). The 60 MiB we give back lets us
 * pre-load both .suprx in PlatformInit AND leave vitaGL room to init.
 *
 * Budget at T1 (post-preload) with 240 MiB cap:
 *   user_free  ≈ 18 (init) − 16 (suprx) = ~2 MiB     ❌ too tight
 *   user_free  ≈ 78 (init) − 16 (suprx) = ~62 MiB    ✅ headroom
 */
unsigned int _newlib_heap_size_user      = 240 * 1024 * 1024;
unsigned int sceLibcHeapSize             = 4   * 1024 * 1024;
unsigned int _pthread_stack_default_user = 2   * 1024 * 1024;

/* Q3-class engines have a few functions with HUGE on-stack locals
 * (CM_GeneratePatchCollide alone reserves ~333 KiB of stack for the
 * patch-grid scratch). Vita's default main-thread stack is far smaller
 * — vitasdk samples ship with 4 KiB. Make it 8 MiB so the engine can
 * recurse through BSP/script loading without trampling the stack. */
SceSize sceUserMainThreadStackSize = 8 * 1024 * 1024;

#define VITA_DATA_ROOT "ux0:data/openmohaa"
#define VITA_BOOT_LOG  VITA_DATA_ROOT "/main/boot.log"

void Sys_VitaDumpMemSnapshot(const char *who);
void Sys_VitaDumpMemAndAbort(const char *who, int size, int tag);

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

    /* Snapshot memory state at three critical points so we can see how
     * the budget evolves at startup:
     *   T0  — before any of our deliberate allocations, but AFTER vitasdk
     *         newlib + sceLibc + module loader / Sce stubs have already
     *         consumed their share. Reveals how much the runtime took
     *         before main() got control.
     *   T1  — after sceSysmoduleLoadModule(NET) returns. Net subsystem
     *         is notorious for grabbing several MiB of system RAM.
     * Later Sys_VitaDumpMemSnapshot calls (Z_TagMalloc tracer + a manual
     * dump from sdl_glimp.c after vglInit*) extend the timeline. */
    Sys_VitaDumpMemSnapshot("T0 boot");

    /* Tell Sys_LoadDll where to find the game / cgame .suprx modules.
     * They live at the root of the installed app (app0:/) alongside
     * eboot.bin — see vita.cmake's VPK packaging step. FS_Startup also
     * uses Sys_BinaryPath as the install-side base, appending "/main"
     * to find autoexec.cfg there; "app0:" satisfies both. */
    extern void Sys_SetBinaryPath(const char *path);
    Sys_SetBinaryPath("app0:");

    /* Pre-load both PRX modules NOW, before vitaGL grabs the system
     * PHYCONT pool. sceKernelLoadStartModule needs a few MiB of
     * physically-contiguous RAM per module; once vglInit runs, only a
     * few MiB are left and the second load (cgame.suprx) fails with
     * SCE_KERNEL_ERROR_NO_PHY_CONT_MEM (0x80024302). Loading both
     * eagerly while PHYCONT is full (~26 MiB) guarantees they succeed.
     * Our custom dlopen wrapper (sys/psp2/dll_psp2.c) caches the open
     * handle by filename, so when Sys_LoadDll() later asks for the
     * same module the engine just gets a refcounted reference. */
    void *h = dlopen("app0:/game.suprx", 0);
    fprintf(stdout, "[preload] game.suprx -> %p\n", h);
    h = dlopen("app0:/cgame.suprx", 0);
    fprintf(stdout, "[preload] cgame.suprx -> %p\n", h);
    fflush(stdout);

    Sys_VitaDumpMemSnapshot("T1 after PlatformInit");
}

void Sys_VitaDumpMemSnapshot(const char *who)
{
    struct mallinfo mi = mallinfo();
    SceKernelFreeMemorySizeInfo info;
    info.size = sizeof(info);
    sceKernelGetFreeMemorySize(&info);
    fprintf(stdout,
        "[MEM %s] arena=%6.1fMB in_use=%6.1fMB intern_free=%6.1fMB | sys_free user=%5.1fMB cdram=%5.1fMB phycont=%5.1fMB\n",
        who,
        mi.arena    / 1048576.0,
        mi.uordblks / 1048576.0,
        mi.fordblks / 1048576.0,
        info.size_user    / 1048576.0,
        info.size_cdram   / 1048576.0,
        info.size_phycont / 1048576.0);
    fflush(stdout);
}

void Sys_VitaDumpMemAndAbort(const char *who, int size, int tag)
{
    struct mallinfo mi = mallinfo();
    fprintf(stdout,
        "[OOM] %s failed: requested=%d tag=%d | heap_in_use=%.1fMB free=%.1fMB\n",
        who, size, tag,
        mi.uordblks / 1048576.0,
        mi.fordblks / 1048576.0);
    fflush(stdout);
    abort();
}

void Sys_PlatformExit(void)
{
    sceKernelExitProcess(0);
}

/* Sys_DefaultHomePath / InstallPath / HomeConfigPath etc. all live in
 * sys_unix.c under a __vita__ branch — keeping them in one place avoids
 * signature drift with qcommon.h. */

#endif /* __vita__ */
