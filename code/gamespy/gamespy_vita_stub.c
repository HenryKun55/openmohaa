/*
 * GameSpy stub for the PlayStation Vita port of OpenMoHAA.
 *
 * The original GameSpy SDK relies on the BSD sockets API (sys/socket.h,
 * netinet/in.h, sys/ioctl.h, netdb.h) which is not provided by the
 * vitasdk newlib — Vita applications use the proprietary sceNet API.
 * Rather than write a full BSD-on-sceNet shim just to keep a master
 * server protocol that has been offline since 2014 working, we link
 * a no-op implementation of every public GameSpy symbol the rest of
 * OpenMoHAA references.
 *
 * Multiplayer browsing through the legacy GameSpy master is therefore
 * unavailable on Vita; direct-IP and LAN play remain functional via
 * the stock net_chan / net_ip layer.
 */

#if defined(__vita__) || defined(__SWITCH__)

#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"
#include "q_gamespy.h"
#include "cl_gamespy.h"
#include "sv_gamespy.h"

void     Com_InitGameSpy(void)                                  {}
qboolean Com_RefreshGameSpyMasters(void)                        { return qfalse; }
unsigned int Com_GetNumMasterEntries(void)                      { return 0; }
void     Com_GetMasterEntry(int index, master_entry_t* entry)   { (void)index; if (entry) memset(entry, 0, sizeof(*entry)); }
const char *Com_GetMasterHost(void)                             { return ""; }
int      Com_GetMasterQueryPort(void)                           { return 0; }
int      Com_GetMasterHeartbeatPort(void)                       { return 0; }

void     CL_RestartGamespy_f(void)                              {}

void     SV_CreateGamespyChallenge(char* challenge)             { if (challenge) challenge[0] = '\0'; }
void     SV_GamespyAuthorize(netadr_t from, const char* resp)   { (void)from; (void)resp; }
void     SV_GamespyHeartbeat(void)                              {}
void     SV_ProcessGamespyQueries(void)                         {}
qboolean SV_InitGamespy(void)                                   { return qfalse; }
void     SV_ShutdownGamespy(void)                               {}
void     SV_RestartGamespy(void)                                {}
void     SV_RestartGamespy_f(void)                              {}
void     SV_TryRestartGamespy(void)                             {}
void     SV_GamespyClientDisconnect(int gamespyId)              { (void)gamespyId; }

/* ----- GameSpy SDK call sites referenced by the MP server browser ----- */
/* All return safe defaults — the MP browser is unreachable on Vita. */

#include "gcdkey/gcdkeyc.h"
#include "goaceng.h"

const char *GS_GetGameKey(unsigned int index)  { (void)index; return ""; }
const char *GS_GetGameName(unsigned int index) { (void)index; return ""; }

void gcd_compute_response(char *cdkey, char *challenge, char response[73], CDResponseMethod method)
{
    (void)cdkey; (void)challenge; (void)method;
    if (response) response[0] = '\0';
}

GServerList ServerListNew(const char *gamename, const char *enginename, const char *seckey,
                          int maxconcupdates, void *cb, int cbtype, void *instance)
{
    (void)gamename; (void)enginename; (void)seckey;
    (void)maxconcupdates; (void)cb; (void)cbtype; (void)instance;
    return NULL;
}
void ServerListFree(GServerList sl)                      { (void)sl; }
GError ServerListUpdate(GServerList sl, gbool a)         { (void)sl; (void)a; return 0; }
GError ServerListThink(GServerList sl)                   { (void)sl; return 0; }
GError ServerListHalt(GServerList sl)                    { (void)sl; return 0; }
GError ServerListClear(GServerList sl)                   { (void)sl; return 0; }
GServerListState ServerListState(GServerList sl)         { (void)sl; return sl_idle; }
int ServerListCount(GServerList sl)                      { (void)sl; return 0; }

int   ServerGetPing(GServer s)                           { (void)s; return 0; }
char *ServerGetAddress(GServer s)                        { (void)s; return ""; }
char *ServerGetStringValue(GServer s, char *k, char *d)  { (void)s; (void)k; return d; }
int   ServerGetIntValue(GServer s, char *k, int d)       { (void)s; (void)k; return d; }

#endif /* __vita__ */
