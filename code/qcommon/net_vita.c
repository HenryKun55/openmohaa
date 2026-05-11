/*
 * Minimal networking shim for the PlayStation Vita port of OpenMoHAA.
 *
 * vitasdk's newlib does not provide BSD sockets — Vita applications
 * have to talk to the sceNet API instead. The full Q3 net_ip.c is ~1700
 * lines of socket plumbing, so for the bring-up build we stub it to no-op
 * (single-player still works; multiplayer over IP is disabled).
 *
 * A future pass should replace this with a real sceNet implementation
 * mirroring vitaQuakeIII's code/psp2/net_psp2.c.
 */

#ifdef __vita__

#include "q_shared.h"
#include "qcommon.h"

qboolean Sys_StringToAdr(const char *s, netadr_t *a, netadrtype_t family)
{
    (void)s; (void)family;
    if (a) memset(a, 0, sizeof(*a));
    return qfalse;
}

qboolean NET_CompareBaseAdrMask(netadr_t a, netadr_t b, int netmask)
{
    (void)a; (void)b; (void)netmask;
    return qfalse;
}

qboolean NET_CompareBaseAdr(netadr_t a, netadr_t b)
{
    (void)a; (void)b;
    return qfalse;
}

const char *NET_AdrToString(netadr_t a)
{
    (void)a;
    return "0.0.0.0";
}

const char *NET_AdrToStringwPort(netadr_t a)
{
    (void)a;
    return "0.0.0.0:0";
}

qboolean NET_CompareAdr(netadr_t a, netadr_t b)
{
    (void)a; (void)b;
    return qfalse;
}

qboolean NET_IsLocalAddress(netadr_t adr)
{
    (void)adr;
    return qtrue;
}

qboolean NET_GetPacket(netadr_t *net_from, msg_t *net_message, fd_set *fdr)
{
    (void)net_from; (void)net_message; (void)fdr;
    return qfalse;
}

void Sys_SendPacket(int length, const void *data, netadr_t to)
{
    (void)length; (void)data; (void)to;
}

qboolean Sys_IsLANAddress(netadr_t adr)
{
    (void)adr;
    return qfalse;
}

void Sys_ShowIP(void)
{
}

void NET_SetMulticast6(void)
{
}

void NET_JoinMulticast6(void)
{
}

void NET_LeaveMulticast6(void)
{
}

void NET_OpenSocks(int port)
{
    (void)port;
}

void NET_OpenIP(void)
{
}

void NET_Config(qboolean enableNetworking)
{
    (void)enableNetworking;
}

void NET_Init(void)
{
}

void NET_Shutdown(void)
{
}

void NET_Event(fd_set *fdr)
{
    (void)fdr;
}

void NET_Sleep(int msec)
{
    if (msec > 0) {
        struct timespec ts = { msec / 1000, (msec % 1000) * 1000000 };
        nanosleep(&ts, NULL);
    }
}

void NET_Restart_f(void)
{
}

#endif /* __vita__ */
