/*
 * OpenMoHAA for PS Vita: the network for multiplayer.
 *
 * This file is part of OpenMoHAA, GPL v2 or later.
 */
#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"

#include <psp2/sysmodule.h>
#include <stdlib.h>
/*
 * The Vita's network for sockets (net_ip.c): the newlib sockets of the vitasdk sit on
 * sceNet, which needs its module, a memory pool and sceNetInit first. Started once, when
 * multiplayer opens the sockets; the update check may have started it already, which is
 * fine ("already initialised"). Returns qfalse when it cannot start or Wi-Fi is off.
 */
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>

#define VITA_NET_POOL (1024 * 1024)

qboolean Sys_VitaNetUp(void)
{
    static void   *pool;
    static qboolean up;
    SceNetInitParam param;
    int             res, state = 0;

    if (up) {
        return qtrue;
    }
    res = sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
    if (res < 0 && res != (int)0x80800002 /* already loaded */) {
        Com_Printf("Sys_VitaNetUp: sceSysmoduleLoadModule(NET) = 0x%08X\n", (unsigned)res);
        return qfalse;
    }
    if (!pool) {
        pool = malloc(VITA_NET_POOL);
        if (!pool) {
            Com_Printf("Sys_VitaNetUp: no memory for the network pool\n");
            return qfalse;
        }
        param.memory = pool;
        param.size   = VITA_NET_POOL;
        param.flags  = 0;
        res          = sceNetInit(&param);
        if (res < 0 && res != (int)0x80410110 /* already initialised (by the update check) */) {
            Com_Printf("Sys_VitaNetUp: sceNetInit = 0x%08X\n", (unsigned)res);
            free(pool);
            pool = NULL;
            return qfalse;
        }
        if (res < 0) {
            free(pool); /* the update check's pool is in use */
            pool = (void *)1;
        }
    }
    res = sceNetCtlInit();
    if (res < 0 && res != (int)0x80412102 /* already initialised */) {
        Com_Printf("Sys_VitaNetUp: sceNetCtlInit = 0x%08X\n", (unsigned)res);
        return qfalse;
    }
    if (sceNetCtlInetGetState(&state) < 0 || state != SCE_NETCTL_STATE_CONNECTED) {
        Com_Printf("Sys_VitaNetUp: not connected to Wi-Fi (state %d)\n", state);
        return qfalse;
    }
    up = qtrue;
    Com_Printf("Sys_VitaNetUp: network up\n");
    return qtrue;
}
