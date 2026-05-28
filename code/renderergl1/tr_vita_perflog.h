/*
=============================================================================
tr_vita_perflog.h - Vita per-frame profiling counters

Goal: in ONE play session, accumulate timing/count data for every hot path
in the render pipeline so we can identify the bottleneck without doing
multiple build-test cycles. Throttled print every 1 sec gives averages
across that interval (much more representative than single-frame samples).

Gated by cvar r_vita_perflog. Default 1 so data is collected; flip 0 via
the in-game perf menu (Select button → DEBUG category) to remove the
microsecond timer overhead when actually playing.

ALL instrumentation cost: 1 Sys_Milliseconds() per measurement + an int
addition. ~µs per call. Negligible compared to the 280μs+ per draw we're
measuring.
=============================================================================
*/

#ifndef TR_VITA_PERFLOG_H
#define TR_VITA_PERFLOG_H

#ifdef __vita__

typedef struct {
    /* Frame-level */
    int       frames;             /* frames counted in window */
    long long us_window;          /* time spent in measurements within window */

    /* RB_RenderDrawSurfList — outer per-view loop */
    int       drawSurfList_calls;
    long long us_drawSurfList;
    int       drawSurfList_surfs; /* sum of numDrawSurfs over calls */

    /* RB_BeginSurface + RB_EndSurface */
    int       endSurface_calls;
    long long us_endSurface;

    /* RB_StageIteratorGeneric — full per-shader iteration */
    int       stageIter_calls;
    long long us_stageIter;
    int       stageIter_total_stages;

    /* DrawMultitextured — single-pass diffuse+lightmap */
    int       multitex_draws;
    long long us_multitex;

    /* Single-texture draws (the else branch in RB_IterateStagesGeneric) */
    int       single_draws;
    long long us_single;

    /* R_DrawElements — actual qglDrawElements wrappers */
    int       drawElems_calls;
    long long us_drawElems;
    int       drawElems_indexes; /* sum of numIndexes over calls */

    /* ComputeTexCoords / ComputeColors (texture coord / color generation) */
    int       computeTC_calls;
    long long us_computeTC;
    int       computeColors_calls;
    long long us_computeColors;

    /* State change counters (already in backEnd.pc but easier to print here) */
    int       glBinds;
    int       glSelectTextures;
    int       glStateChanges;

    /* CollapseMultitexture stats (only updated at shader load, kept here for one-shot print) */
    int       collapse_attempted;     /* # shaders inspected */
    int       collapse_succeeded;     /* # stages collapsed */
} vitaPerfStats_t;

extern vitaPerfStats_t  g_vitaPerf;
extern cvar_t          *r_vita_perflog;

/* Cheap inline timer — Sys_Milliseconds() gives ms not μs but on Vita
 * the overhead is similar to a system call so we use sceKernelGetSystemTimeWide
 * via a thin wrapper. For now we'll measure in ms increments which is enough
 * to spot a draw call costing > 1 ms; sub-ms work is summed over many calls. */
extern int Sys_Milliseconds(void);

#define VITA_PERF_T0(name)  int _vp_##name##_t0 = (r_vita_perflog && r_vita_perflog->integer) ? Sys_Milliseconds() : 0
#define VITA_PERF_T1_ACCUM(name, field, count_field)                                            \
    do {                                                                                       \
        if (r_vita_perflog && r_vita_perflog->integer) {                                       \
            int _vp_##name##_t1 = Sys_Milliseconds();                                          \
            g_vitaPerf.field += (_vp_##name##_t1 - _vp_##name##_t0);                           \
            g_vitaPerf.count_field++;                                                          \
        }                                                                                      \
    } while (0)

void VitaPerf_Init(void);
void VitaPerf_PrintMaybe(void);   /* call once per frame at end-of-frame; throttled 1×/sec */
void VitaPerf_Reset(void);

#endif /* __vita__ */
#endif /* TR_VITA_PERFLOG_H */
