/*
 * vita_prof.h -- per-frame CPU time breakdown for the PS Vita (FRAME-PROF log line).
 *
 * Each slot accumulates microseconds; Com_Frame prints the per-frame averages every
 * 60 frames and clears them. Slots nest (cl contains cg, cg contains scene, ...), so
 * the numbers are not meant to add up across levels.
 */
#ifndef VITA_PROF_H
#define VITA_PROF_H

#ifdef __vita__

#include <psp2/kernel/processmgr.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
	VP_FRAME,		// whole Com_Frame, wall time between frames
	VP_IDLE,		// waiting for com_maxfps at the top of Com_Frame
	VP_SV,			// SV_Frame: game logic, AI, scripts
	VP_EV,			// event loop + command buffer
	VP_CL,			// CL_Frame
	VP_CG,			// CG_DrawActiveFrame (cgame, includes its RE_RenderScene calls)
	VP_SCENE,		// RE_RenderScene, all scenes of the frame
	VP_WORLD,		// R_RecursiveWorldNode + R_MarkLeaves
	VP_TERRAIN,		// R_AddTerrainSurfaces (tessellation + snapshot)
	VP_STATIC,		// R_AddStaticModelSurfaces
	VP_ENTS,		// R_AddEntitySurfaces (skeletal models: bones, LOD)
	VP_SORT,		// R_SortDrawSurfs (includes the sky portal view below)
	VP_SKY,			// R_Sky_Render: the sky portal's whole view
	VP_RTHREAD,		// render thread busy (runs in parallel)
	VP_COUNT
};

extern unsigned int vp_acc[VP_COUNT];

// Render thread breakdown (RT-PROF line). Tessellation and shading/draw time per
// surface type (surfaceType_t order, 17 types; the draw set has an extra "other" slot
// for batches outside the 3D surface list), per command group, plus counters.
#define VPR_NSURF 17
enum {
	VPR_TESS     = 0,
	VPR_DRAW     = VPR_TESS + VPR_NSURF,
	VPR_SURFS    = VPR_DRAW + VPR_NSURF + 1,	// RC_DRAW_SURFS (3D views, incl. flares/sky)
	VPR_SPRITES,								// RC_SPRITE_SURFS
	VPR_2D,										// 2D commands (HUD, menus, text)
	VPR_SWAP,									// RC_SWAP_BUFFERS (vglSwapBuffers)
	VPR_DLIGHTS,								// RC_UPLOAD_DLIGHTS
	VPR_OTHER,
	VPR_BATCHES,								// counter: RB_EndSurface draws
	VPR_VERTS,									// counter: vertexes drawn
	VPR_LIST,									// RB_RenderDrawSurfList (tess + draw + per-entity setup)
	VPR_SKIN_GPU,								// counter: skeletal surfaces drawn by GPU skinning
	VPR_SKIN_CPU,								// counter: ... that fell back to the CPU path
	VPR_SKINFAIL,								// counters per fallback reason (6 slots, vita_skin_fail)
	VPR_SKINFAIL_END = VPR_SKINFAIL + 6,
	VPR_COUNT = VPR_SKINFAIL_END
};

extern unsigned int vp_rt[VPR_COUNT];

static inline unsigned int VP_Now( void ) {
	return sceKernelGetProcessTimeLow();
}

#define VP_BEGIN( var )			unsigned int var = VP_Now()
#define VP_END( slot, var )		( vp_acc[slot] += VP_Now() - ( var ) )

#ifdef __cplusplus
}
#endif

#else

#define VP_BEGIN( var )
#define VP_END( slot, var )

#endif // __vita__

#endif // VITA_PROF_H
