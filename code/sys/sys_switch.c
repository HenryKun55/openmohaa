/*
 * Nintendo Switch (devkitPro / libnx homebrew) platform shim for OpenMoHAA.
 *
 * Mirrors code/sys/sys_vita.c: the bulk of POSIX-y system code is reused
 * from sys_unix.c via devkitA64's newlib. This file only carries the bits
 * that newlib + SDL2-switch do not already cover:
 *
 *   - libnx service init (romfs is unused — game data lives on the SD card,
 *     not inside the NRO — but socket/applet services are brought up here)
 *   - Path overrides so that all reads/writes happen under
 *     sdmc:/switch/openmohaa/ instead of $HOME, which doesn't exist
 *   - A boot.log redirect for the same FTP-pull debug loop we use on Vita
 *
 * Unlike the Vita, the Switch has ~3-4 GB of user RAM, so there is NO
 * hand-tuned heap budget here: libnx hands the homebrew almost the entire
 * applet heap and newlib grows into it on demand. The Vita's three-way
 * 240/120/100 MiB split (and the std::bad_alloc level-transition crash it
 * caused) simply does not exist on this target.
 */

#ifdef __SWITCH__

#include <switch.h>
#include <sys/stat.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SWITCH_DATA_ROOT "sdmc:/switch/openmohaa"
#define SWITCH_BOOT_LOG  SWITCH_DATA_ROOT "/main/boot.log"

void Sys_SwitchDumpMemSnapshot(const char *who);

/* libnx calls userAppInit() before main() when the application links it.
 * We bring services up here so they are ready before any engine code runs.
 * romfsInit() is intentionally NOT called: the paks (~1 GB) are far too
 * large to embed in the NRO, so they are read straight off the SD card. */
void userAppInit(void)
{
    appletInitialize();
    /* Networking — optional, but the engine's net stack expects a working
     * BSD socket layer to exist even for a pure single-player session. */
    socketInitializeDefault();
    /* Controllers: SDL2-switch initialises HID on its own SDL_Init, so we
     * don't call hidInitialize() here (double-init trips an assert). */
}

void userAppExit(void)
{
    socketExit();
    appletExit();
}

/* libnx fault handler. When the guest faults, libnx calls this with the full
 * register dump instead of silently dying. We print it to boot.log (unbuffered
 * + fsync) so ONE crash run tells us the faulting PC (which function), FAR (the
 * bad address that was dereferenced), and SP (if it's down near the stack guard
 * => stack overflow). error_desc 0x101 = data abort (bad pointer); a tiny/near-
 * guard SP or FAR just below the stack base => stack overflow. */
__attribute__((aligned(16))) u8 __nx_exception_stack[0x4000];
u64 __nx_exception_stack_size = sizeof(__nx_exception_stack);

void __libnx_exception_handler(ThreadExceptionDump *ctx)
{
    int i;
    fprintf(stdout, "\n=== CRASH (libnx exception) ===\n");
    /* Print the handler's own runtime address so the load base can be derived
     * despite ASLR: base = this_runtime_addr - (nm offset of
     * __libnx_exception_handler in openmohaa.elf). Then any PC/LR maps via
     * addr2line at (runtime - base). */
    fprintf(stdout, "handler_runtime=%p\n", (void *)&__libnx_exception_handler);
    fprintf(stdout, "error_desc=0x%x  PC=0x%016llx  LR=0x%016llx\n",
        (unsigned)ctx->error_desc,
        (unsigned long long)ctx->pc.x,
        (unsigned long long)ctx->lr.x);
    fprintf(stdout, "SP=0x%016llx  FAR=0x%016llx  ESR=0x%x\n",
        (unsigned long long)ctx->sp.x,
        (unsigned long long)ctx->far.x,
        (unsigned)ctx->esr);
    for (i = 0; i < 29; i++) {
        fprintf(stdout, "x%-2d=0x%016llx\n", i, (unsigned long long)ctx->cpu_gprs[i].x);
    }
    fflush(stdout);
    fsync(fileno(stdout));
}

void Sys_PlatformInit(void)
{
    /* SD-card data tree, created on first boot. mkdir is harmless if the
     * directories already exist. */
    mkdir("sdmc:/switch", 0777);
    mkdir(SWITCH_DATA_ROOT, 0777);
    mkdir(SWITCH_DATA_ROOT "/main", 0777);

    /* Redirect stdout/stderr to boot.log. FULLY UNBUFFERED (_IONBF): libnx's
     * newlib does not honour _IOLBF with a NULL/zero buffer the way vitasdk
     * does, so during bring-up we write every byte straight to disk — a
     * crash then leaves the complete log up to the faulting line. */
    freopen(SWITCH_BOOT_LOG, "w", stdout);
    setvbuf(stdout, NULL, _IONBF, 0);
    stderr = stdout;

    fprintf(stdout, "=== OpenMoHAA Switch boot.log ===\n");
    /* Print the load-base anchor at boot too: the exception handler does NOT
     * fire under Ryujinx, so this is the only way to get the base there.
     * base = this - 0x126150 (nm offset of __libnx_exception_handler). */
    fprintf(stdout, "[boot] handler_runtime=%p\n", (void *)&__libnx_exception_handler);
    fprintf(stdout, "[boot] PlatformInit: stdout redirected\n");

    /* Game data + the in-binary autoexec live under the SD-card data root.
     * Sys_BinaryPath doubles as FS_Startup's install-side base (it appends
     * "/main" to find autoexec.cfg), so point it at the data root. Unlike
     * the Vita there is no separate app0: read-only mount — everything is
     * on writable SD. */
    fprintf(stdout, "[boot] PlatformInit: setting binary path\n");
    extern void Sys_SetBinaryPath(const char *path);
    Sys_SetBinaryPath(SWITCH_DATA_ROOT);

    fprintf(stdout, "[boot] PlatformInit: mem snapshot\n");
    Sys_SwitchDumpMemSnapshot("T0 boot");

    /* Force the libnx fs layer to commit — sdmc: writes are cached and a
     * hard crash otherwise drops everything after the last block boundary,
     * making boot.log lie about where we died. */
    fflush(stdout);
    fsync(fileno(stdout));

    fprintf(stdout, "[boot] PlatformInit: done\n");
    fflush(stdout);
    fsync(fileno(stdout));
}

/* Mirrors Sys_VitaDumpMemSnapshot. mallinfo() reports the newlib arena;
 * libnx exposes the remaining heap headroom and the total applet pool. */
void Sys_SwitchDumpMemSnapshot(const char *who)
{
    /* Bring-up note: svcGetInfo + the by-value struct mallinfo return were
     * suspected of smashing the stack on return (no checkpoint printed after
     * this call). Pared down to a single integer query to isolate; expand
     * once boot is stable. */
    u64 total = 0, used = 0;
    svcGetInfo(&total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
    svcGetInfo(&used,  InfoType_UsedMemorySize,  CUR_PROCESS_HANDLE, 0);
    fprintf(stdout, "[MEM %s] proc total=%lluMB used=%lluMB\n",
        who,
        (unsigned long long)(total / 1048576),
        (unsigned long long)(used  / 1048576));
}

void Sys_PlatformExit(void)
{
    exit(0);
}

/* Sys_DefaultHomePath / InstallPath / BinaryPath etc. live in sys_unix.c
 * under a __SWITCH__ branch, alongside the matching __vita__ one, so the
 * path strings stay in one place and can't drift from qcommon.h. */

/*
=================================================================
Gamepad input via the libnx HID (`pad`) API.

SDL2-switch's SDL_GameController state never updates under Ryujinx (the emulator
doesn't pump HID into the SDL layer for a homebrew NRO — the controller opens but
GetButton/GetAxis return 0 and no events fire). So sdl_input.c reads the pad
through these thin wrappers, which talk to the libnx HID directly. Works the same
on real hardware and in Ryujinx, and keeps SDL/switch.h header conflicts out of
sdl_input.c (only int enum values cross the boundary).
=================================================================
*/
static PadState s_switchPad;
static int      s_switchPadInit = 0;

static void Switch_PadEnsure(void)
{
    if (!s_switchPadInit) {
        padConfigureInput(8, HidNpadStyleSet_NpadStandard);
        padInitializeDefault(&s_switchPad);
        s_switchPadInit = 1;
    }
}

void Switch_PadUpdate(void)
{
    Switch_PadEnsure();
    padUpdate(&s_switchPad);
}

/* sdlBtn == SDL_CONTROLLER_BUTTON_* (stable SDL2 enum int). 1 if held.
 * SDL uses an Xbox-positional layout; Nintendo swaps A/B and X/Y, so the
 * face buttons are crossed here to keep the existing PAD0_* binds correct. */
int Switch_PadButtonPressed(int sdlBtn)
{
    u64 b;
    if (!s_switchPadInit) {
        return 0;
    }
    b = padGetButtons(&s_switchPad);
    switch (sdlBtn) {
    case 0:  return (b & HidNpadButton_B)      ? 1 : 0; /* A  (bottom) = Nintendo B */
    case 1:  return (b & HidNpadButton_A)      ? 1 : 0; /* B  (right)  = Nintendo A */
    case 2:  return (b & HidNpadButton_Y)      ? 1 : 0; /* X  (left)   = Nintendo Y */
    case 3:  return (b & HidNpadButton_X)      ? 1 : 0; /* Y  (top)    = Nintendo X */
    case 4:  return (b & HidNpadButton_Minus)  ? 1 : 0; /* BACK  */
    case 6:  return (b & HidNpadButton_Plus)   ? 1 : 0; /* START */
    case 7:  return (b & HidNpadButton_StickL) ? 1 : 0; /* LEFTSTICK click  */
    case 8:  return (b & HidNpadButton_StickR) ? 1 : 0; /* RIGHTSTICK click */
    case 9:  return (b & HidNpadButton_L)      ? 1 : 0; /* LEFTSHOULDER  */
    case 10: return (b & HidNpadButton_R)      ? 1 : 0; /* RIGHTSHOULDER */
    case 11: return (b & HidNpadButton_Up)     ? 1 : 0; /* DPAD_UP    */
    case 12: return (b & HidNpadButton_Down)   ? 1 : 0; /* DPAD_DOWN  */
    case 13: return (b & HidNpadButton_Left)   ? 1 : 0; /* DPAD_LEFT  */
    case 14: return (b & HidNpadButton_Right)  ? 1 : 0; /* DPAD_RIGHT */
    default: return 0;
    }
}

/* sdlAxis == SDL_CONTROLLER_AXIS_* (stable SDL2 enum int). Range -32768..32767. */
int Switch_PadAxis(int sdlAxis)
{
    u64                 b;
    HidAnalogStickState l, r;
    if (!s_switchPadInit) {
        return 0;
    }
    b = padGetButtons(&s_switchPad);
    l = padGetStickPos(&s_switchPad, 0);
    r = padGetStickPos(&s_switchPad, 1);
    switch (sdlAxis) {
    case 0:  return l.x;                                  /* LEFTX  */
    case 1:  return -l.y;                                 /* LEFTY  (SDL +Y = down, libnx +y = up) */
    case 2:  return r.x;                                  /* RIGHTX */
    case 3:  return -r.y;                                 /* RIGHTY */
    case 4:  return (b & HidNpadButton_ZL) ? 32767 : 0;   /* TRIGGERLEFT  */
    case 5:  return (b & HidNpadButton_ZR) ? 32767 : 0;   /* TRIGGERRIGHT */
    default: return 0;
    }
}

/* 1 when docked (TV mode), 0 in handheld. Used to pick the render resolution:
 * 1080p docked, 720p handheld. */
int Switch_IsDocked(void)
{
    return appletGetOperationMode() == AppletOperationMode_Console;
}

#endif /* __SWITCH__ */
