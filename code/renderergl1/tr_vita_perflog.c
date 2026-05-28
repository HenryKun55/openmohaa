/*
=============================================================================
tr_vita_perflog.c
=============================================================================
*/

#ifdef __vita__

#include "tr_local.h"
#include "tr_vita_perflog.h"

vitaPerfStats_t g_vitaPerf;
cvar_t         *r_vita_perflog = NULL;

void VitaPerf_Init(void)
{
    r_vita_perflog = ri.Cvar_Get("r_vita_perflog", "1", CVAR_ARCHIVE);
    Com_Memset(&g_vitaPerf, 0, sizeof(g_vitaPerf));
}

void VitaPerf_Reset(void)
{
    int saved_attempted = g_vitaPerf.collapse_attempted;
    int saved_succeeded = g_vitaPerf.collapse_succeeded;
    Com_Memset(&g_vitaPerf, 0, sizeof(g_vitaPerf));
    /* Preserve collapse stats — they are computed once at shader load. */
    g_vitaPerf.collapse_attempted = saved_attempted;
    g_vitaPerf.collapse_succeeded = saved_succeeded;
}

/* Print accumulated stats every 1 sec.
 * The numbers shown are TOTALS over the 1-sec window. Divide by `frames`
 * for per-frame average; divide ms by call count for per-call average. */
void VitaPerf_PrintMaybe(void)
{
    if (!r_vita_perflog || !r_vita_perflog->integer) return;

    static int s_lastPrint = 0;
    int        now         = Sys_Milliseconds();
    g_vitaPerf.frames++;

    if (now - s_lastPrint < 1000) return;
    s_lastPrint = now;

    /* If we haven't drawn anything yet (menu only), don't pollute. */
    if (g_vitaPerf.drawSurfList_calls == 0 && g_vitaPerf.endSurface_calls == 0) {
        VitaPerf_Reset();
        return;
    }

    int frames = g_vitaPerf.frames ? g_vitaPerf.frames : 1;

    ri.Printf(PRINT_ALL,
        "\n[VITA-PERF] === 1-sec window: %d frames ===\n", frames);

    /* High-level: where does the time go? */
    ri.Printf(PRINT_ALL,
        "[VITA-PERF] drawSurfList: %d calls (%d surfs) total=%lld ms (%lld ms/frame)\n",
        g_vitaPerf.drawSurfList_calls,
        g_vitaPerf.drawSurfList_surfs,
        g_vitaPerf.us_drawSurfList,
        g_vitaPerf.us_drawSurfList / frames);

    ri.Printf(PRINT_ALL,
        "[VITA-PERF] stageIter:    %d calls (%d stages total) total=%lld ms\n",
        g_vitaPerf.stageIter_calls,
        g_vitaPerf.stageIter_total_stages,
        g_vitaPerf.us_stageIter);

    ri.Printf(PRINT_ALL,
        "[VITA-PERF] endSurface:   %d calls total=%lld ms\n",
        g_vitaPerf.endSurface_calls,
        g_vitaPerf.us_endSurface);

    /* Draw call breakdown */
    ri.Printf(PRINT_ALL,
        "[VITA-PERF] drawElems:    %d calls (%d indexes) total=%lld ms (avg %lld μs/call)\n",
        g_vitaPerf.drawElems_calls,
        g_vitaPerf.drawElems_indexes,
        g_vitaPerf.us_drawElems,
        g_vitaPerf.drawElems_calls > 0 ?
            (g_vitaPerf.us_drawElems * 1000) / g_vitaPerf.drawElems_calls : 0);

    ri.Printf(PRINT_ALL,
        "[VITA-PERF] multitex:     %d calls total=%lld ms (avg %lld μs/call)\n",
        g_vitaPerf.multitex_draws,
        g_vitaPerf.us_multitex,
        g_vitaPerf.multitex_draws > 0 ?
            (g_vitaPerf.us_multitex * 1000) / g_vitaPerf.multitex_draws : 0);

    ri.Printf(PRINT_ALL,
        "[VITA-PERF] single-tex:   %d calls total=%lld ms (avg %lld μs/call)\n",
        g_vitaPerf.single_draws,
        g_vitaPerf.us_single,
        g_vitaPerf.single_draws > 0 ?
            (g_vitaPerf.us_single * 1000) / g_vitaPerf.single_draws : 0);

    /* Texcoord / color generation */
    ri.Printf(PRINT_ALL,
        "[VITA-PERF] computeTC:    %d calls total=%lld ms | computeColors: %d calls total=%lld ms\n",
        g_vitaPerf.computeTC_calls, g_vitaPerf.us_computeTC,
        g_vitaPerf.computeColors_calls, g_vitaPerf.us_computeColors);

    /* GL state */
    ri.Printf(PRINT_ALL,
        "[VITA-PERF] glBinds=%d glSelTex=%d glStateChg=%d\n",
        g_vitaPerf.glBinds, g_vitaPerf.glSelectTextures, g_vitaPerf.glStateChanges);

    /* Shader collapse — one-shot info from level load */
    ri.Printf(PRINT_ALL,
        "[VITA-PERF] collapse: attempted=%d succeeded=%d (Phase 3 multitex collapse rate)\n",
        g_vitaPerf.collapse_attempted,
        g_vitaPerf.collapse_succeeded);

    /* Per-frame derived: total render time = drawSurfList + endSurface (rough) */
    long long total_render = g_vitaPerf.us_drawSurfList + g_vitaPerf.us_endSurface;
    ri.Printf(PRINT_ALL,
        "[VITA-PERF] derived: ~%lld ms/frame in render, ~%lld μs/draw average\n\n",
        total_render / frames,
        g_vitaPerf.drawElems_calls > 0 ?
            (total_render * 1000) / g_vitaPerf.drawElems_calls : 0);

    VitaPerf_Reset();
}

#endif /* __vita__ */
