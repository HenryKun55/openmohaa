/*
 * tr_vita_smp.c -- render thread for the PS Vita (Quake 3 "SMP"-style).
 *
 * The front end (cgame scene building, R_RenderView, 2D recording) fills one
 * backEndData frame on the main thread while this thread executes the previous
 * frame's command list (RB_ExecuteRenderCommands: every GL / vitaGL call of a frame,
 * vglSwapBuffers included). Profiling on hardware showed the two halves take ~25-40
 * and ~55 ms and ran back to back on one core; the Vita has three.
 *
 * Contract:
 *  - At most one frame is in flight. R_SmpHandoff (end of frame) waits for it, then
 *    hands over the new one and returns; RE_EndFrame then flips to the other
 *    backEndData buffer.
 *  - Anything on the main thread that issues GL or changes data the backend reads
 *    (image/shader creation, world loading, mid-frame R_IssuePendingRenderCommands,
 *    registration) calls R_SyncRenderThread() first, so the two never overlap.
 *  - Collision traces are serialized by CM_VitaLock (the backend traces for
 *    spherical lighting and lens flares).
 *
 * r_vita_smp 0 restores the single-threaded path (latched).
 */

#include "tr_local.h"

#ifdef __vita__

#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>

cvar_t *r_vita_smp;
cvar_t *r_vita_smp_serial;	// diagnostic: wait for each frame right after handing it off

static SceUID		s_thread = -1;
static SceUID		s_semWork = -1;
static SceUID		s_semDone = -1;
static int			s_outstanding;		// main thread only: a frame was handed off and not waited for
static volatile int	s_quit;
static const void	*s_cmds;
static backEndData_t *s_data;
static int			s_smpFrame;

static int R_RenderThread(SceSize args, void *argp)
{
	(void)args;
	(void)argp;

	for (;;) {
		sceKernelWaitSema(s_semWork, 1, NULL);
		if (s_quit) {
			break;
		}
		backEnd.data     = s_data;
		backEnd.smpFrame = s_smpFrame;
		RB_ExecuteRenderCommands(s_cmds);
		sceKernelSignalSema(s_semDone, 1);
	}

	sceKernelSignalSema(s_semDone, 1);
	return sceKernelExitDeleteThread(0);
}

qboolean R_SmpActive(void)
{
	return s_thread >= 0;
}

// SMP-PROF: which main-thread callers had to wait for the render thread, and how
// long, printed every 60 frames. A mid-frame wait serializes the two threads.
#define SMP_PROF_CALLERS 8
static struct { void *caller; int count; int usec; } s_prof[SMP_PROF_CALLERS];
static int s_profFrames, s_profHandoffWaitUs;

static void R_SmpProfWait(void *caller, int usec)
{
	int i;
	for (i = 0; i < SMP_PROF_CALLERS; i++) {
		if (s_prof[i].caller == caller || !s_prof[i].caller) {
			s_prof[i].caller = caller;
			s_prof[i].count++;
			s_prof[i].usec += usec;
			return;
		}
	}
}

static void R_SyncRenderThread_Wait(void *caller)
{
	SceUInt64 t0;

	if (!s_outstanding) {
		return;
	}
	t0 = sceKernelGetProcessTimeWide();
	sceKernelWaitSema(s_semDone, 1, NULL);
	s_outstanding = 0;
	if (caller) {
		R_SmpProfWait(caller, (int)(sceKernelGetProcessTimeWide() - t0));
	} else {
		s_profHandoffWaitUs += (int)(sceKernelGetProcessTimeWide() - t0);
	}
}

void *r_smpSyncCaller;	// set by R_IssuePendingRenderCommands: who asked for the flush

void R_SyncRenderThread(void)
{
	R_SyncRenderThread_Wait(r_smpSyncCaller ? r_smpSyncCaller : __builtin_return_address(0));
}

void R_SmpHandoff(const void *cmds)
{
	R_SyncRenderThread_Wait(NULL);

	if (++s_profFrames >= 60) {
		char line[512];
		int  i, n;

		n = Com_sprintf(line, sizeof(line), "SMP-PROF: 60 frames, end-of-frame wait %d ms; mid-frame waits:",
			s_profHandoffWaitUs / 1000);
		for (i = 0; i < SMP_PROF_CALLERS && s_prof[i].caller; i++) {
			n += Com_sprintf(line + n, sizeof(line) - n, " %p x%d %dms", s_prof[i].caller, s_prof[i].count,
				s_prof[i].usec / 1000);
		}
		ri.Printf(PRINT_ALL, "%s\n", line);
		memset(s_prof, 0, sizeof(s_prof));
		s_profFrames = 0;
		s_profHandoffWaitUs = 0;
	}

	s_cmds        = cmds;
	s_data        = backEndData;
	s_smpFrame    = tr.smpFrame;
	s_outstanding = 1;
	sceKernelSignalSema(s_semWork, 1);

	if (r_vita_smp_serial->integer) {
		// Same buffers and thread, no overlap: tells a race from a buffering bug.
		R_SyncRenderThread_Wait(NULL);
	}
}

void R_SmpInit(void)
{
	r_vita_smp = ri.Cvar_Get("r_vita_smp", "1", CVAR_ARCHIVE | CVAR_LATCH);
	r_vita_smp_serial = ri.Cvar_Get("r_vita_smp_serial", "0", 0);
	if (!r_vita_smp->integer || s_thread >= 0) {
		return;
	}

	s_quit        = 0;
	s_outstanding = 0;
	s_semWork     = sceKernelCreateSema("r_smp_work", 0, 0, 1, NULL);
	s_semDone     = sceKernelCreateSema("r_smp_done", 0, 0, 1, NULL);

	// Render thread on core 1, the main thread (front end, game, cgame) on core 0.
	s_thread = sceKernelCreateThread("OpenMoHAA render", R_RenderThread, 0x10000100, 256 * 1024, 0,
		SCE_KERNEL_CPU_MASK_USER_1, NULL);
	if (s_thread < 0 || s_semWork < 0 || s_semDone < 0) {
		ri.Printf(PRINT_WARNING, "R_SmpInit: render thread creation failed (0x%08X), running single-threaded\n", s_thread);
		if (s_semWork >= 0) sceKernelDeleteSema(s_semWork);
		if (s_semDone >= 0) sceKernelDeleteSema(s_semDone);
		s_semWork = s_semDone = -1;
		s_thread = -1;
		return;
	}
	sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_0);
	sceKernelStartThread(s_thread, 0, NULL);
	ri.Printf(PRINT_ALL, "R_SmpInit: render thread started (core 1)\n");
}

void R_SmpShutdown(void)
{
	if (s_thread < 0) {
		return;
	}
	R_SyncRenderThread();

	s_quit = 1;
	sceKernelSignalSema(s_semWork, 1);
	sceKernelWaitSema(s_semDone, 1, NULL);

	sceKernelDeleteSema(s_semWork);
	sceKernelDeleteSema(s_semDone);
	s_semWork = s_semDone = -1;
	s_thread  = -1;
	sceKernelChangeThreadCpuAffinityMask(0, SCE_KERNEL_CPU_MASK_USER_ALL);
}

#else

qboolean R_SmpActive(void) { return qfalse; }
void R_SyncRenderThread(void) {}
void R_SmpHandoff(const void *cmds) { (void)cmds; }
void R_SmpInit(void) {}
void R_SmpShutdown(void) {}

#endif // __vita__
