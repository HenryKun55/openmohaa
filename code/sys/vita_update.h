/*
 * OpenMoHAA for PS Vita: checks GitHub for a newer release, downloads it (with the
 * language pack in use) and hands over to updater.bin to install it. See vita_update.c.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    VU_IDLE,        // nothing checked yet
    VU_CHECKING,    // asking GitHub for the latest release
    VU_UPTODATE,    // this is the latest version
    VU_AVAILABLE,   // a newer version can be installed
    VU_DOWNLOADING, // downloading the .vpk / language pack
    VU_READY,       // downloaded and verified: VitaUpdate_Launch() installs it
    VU_ERROR        // VitaUpdate_Error() says what went wrong
} vitaUpdateState_t;

// Starts a check in the background (does nothing while one is running).
void VitaUpdate_Check(const char *language);
// Downloads the version found by the check, then goes to VU_READY.
void VitaUpdate_Download(void);
// Stops a download.
void VitaUpdate_Cancel(void);
// Replaces the game with updater.bin, which installs the download. Returns only on error.
void VitaUpdate_Launch(void);

vitaUpdateState_t VitaUpdate_State(void);
const char       *VitaUpdate_CurrentVersion(void);
const char       *VitaUpdate_NewVersion(void);
// Downloaded and total bytes of the current download.
void              VitaUpdate_Progress(long long *done, long long *total);
// What went wrong: an English sentence (a translation key) and the technical detail.
const char       *VitaUpdate_Error(void);
const char       *VitaUpdate_ErrorDetail(void);

// What updater.bin reported after the last install, read once at start-up:
// returns 1 on success, -1 on failure (with the message in 'error'/'detail'), 0 if none.
int VitaUpdate_TakeInstallResult(char *version, int versionSize, char *error, int errorSize, char *detail,
                                 int detailSize);

#ifdef __cplusplus
}
#endif
