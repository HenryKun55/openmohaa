/*
 * g_vitaprof.h -- G-PROF: server (game module) frame breakdown on the PS Vita.
 * gp_acc (g_main.cpp) accumulates microseconds per slot; G_RunFrame prints the
 * per-frame averages every 60 server frames.
 */
#pragma once

#ifdef __vita__
#include <psp2/kernel/processmgr.h>

enum {
    GP_EVENTS,      // L_ProcessPendingEvents (all calls)
    GP_PREANIMATE,  // Actor::PreAnimate
    GP_SCRIPTS,     // Director.Unpause: script threads
    GP_NAV,         // G_Navigation_Frame
    GP_SMOKE,       // G_UpdateSmokeSprites + bad places
    GP_ACTORS,      // G_RunEntity for actors (AI, animation, movement)
    GP_PLAYER,      // G_RunEntity for players
    GP_OTHERS,      // G_RunEntity for everything else
    GP_ENDFRAMES,   // earthquakes + G_ClientEndServerFrames
    GP_TOTAL,
    // Actor::Think split (inside GP_ACTORS)
    GP_AI_PARMS,    // FixAIParameters + UpdateEnableEnemy + think state transitions
    GP_AI_STATE,    // the think state function (sensing, decisions, includes PostThink)
    GP_AI_ANGLES,   // PostThink: UpdateAngles
    GP_AI_ANIM,     // PostThink: UpdateAnim
    GP_AI_MOVE,     // PostThink: DoMove
    GP_AI_BONES,    // PostThink: UpdateBoneControllers
    GP_AI_STEPS,    // PostThink: UpdateFootsteps
    GP_NACTORS,     // counter
    GP_NENTS,       // counter
    GP_COUNT
};

extern unsigned int gp_acc[GP_COUNT];

#define GP(slot, stmt) \
    do { \
        unsigned int _gpT = sceKernelGetProcessTimeLow(); \
        stmt; \
        gp_acc[slot] += sceKernelGetProcessTimeLow() - _gpT; \
    } while (0)
#else
#define GP(slot, stmt) stmt
#endif
