/*
 * OpenMoHAA for PS Vita - updater.bin
 *
 * The Vita's installer refuses to install over a title that is running (0x80101114), and
 * this program runs as the game's title when the game hands over to it. So it works in
 * three modes, by the title it runs as and the files it finds in ux0:data/openmohaa/update:
 *
 *  - stage (OMHA00001, started by the game with sceAppMgrLoadExec, plan.txt present):
 *    unpacks the downloaded .vpk and writes its package header, installs a helper title
 *    (OMHA00002: this same program, shipped in app0:updater/helper), and opens it.
 *  - helper (OMHA00002): with the game closed, installs the new game over it (settings and
 *    saves in ux0:data are kept), puts the new language pack in place, writes result.txt
 *    (shown, translated, by the game) and opens the game again.
 *  - cleanup (OMHA00001, started by the game at boot when "cleanup" is present): removes
 *    the helper title so no extra bubble stays on the LiveArea, then goes back to the game.
 *
 * Every step reports the exact error on screen, in result.txt and in update.log.
 *
 * This file is part of OpenMoHAA, GPL v2 or later. head_bin_template.h comes from
 * VitaShell (GPLv3); debugScreen*.c/h are the Vita SDK samples (PSPSDK BSD license).
 */
#include <psp2/appmgr.h>
#include <psp2/apputil.h>
#include <psp2/ctrl.h>
#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/power.h>
#include <psp2/promoterutil.h>
#include <psp2/sysmodule.h>

#include <openssl/sha.h>
#include <png.h>
#include <psp2/display.h>
#include <psp2/kernel/sysmem.h>
#include <zlib.h>

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "debugScreen.h"
#include "head_bin_template.h"

#define DATA_DIR    "ux0:data/openmohaa"
#define UPDATE_DIR  DATA_DIR "/update"
#define PKG_DIR     UPDATE_DIR "/pkg"
#define HELPER_DIR  UPDATE_DIR "/helper"
#define CLEANUP_FLAG UPDATE_DIR "/cleanup"   /* the helper title is to be removed */
#define CLEANED_FLAG UPDATE_DIR "/cleanup_ran" /* the game starts again from the cleanup */
#define GAME_TITLE   "OMHA00001"
#define HELPER_TITLE "OMHA00002"
#define PLAN_FILE   UPDATE_DIR "/plan.txt"
#define RESULT_FILE UPDATE_DIR "/result.txt"
#define LOG_FILE    UPDATE_DIR "/update.log"

#define Say psvDebugScreenPrintf

static char g_version[32];

/* ---------- log and result ---------- */

static void Log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void Log(const char *fmt, ...)
{
    char    line[512];
    va_list ap;
    FILE   *f;

    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    f = fopen(LOG_FILE, "a");
    if (f) {
        fprintf(f, "[updater] %s\n", line);
        fclose(f);
    }
}

/* result.txt: read by the game at its next start (see code/sys/vita_update.c). */
static void WriteResult(const char *status, const char *step, int code, const char *detail)
{
    FILE *f = fopen(RESULT_FILE, "w");
    if (!f) {
        return;
    }
    fprintf(f, "status=%s\nversion=%s\nstep=%s\ncode=0x%08X\ndetail=%s\n", status, g_version, step, (unsigned)code,
            detail ? detail : "");
    fclose(f);
}

static void WaitCrossAndExit(void) __attribute__((noreturn));
static void WaitCrossAndExit(void)
{
    SceCtrlData pad, old;

    memset(&old, 0xFF, sizeof(old));
    Say("\n\e[37;1mPress X to close. Then start OpenMoHAA again from the LiveArea.\e[0m\n");
    for (;;) {
        sceCtrlPeekBufferPositive(0, &pad, 1);
        if ((pad.buttons & SCE_CTRL_CROSS) && !(old.buttons & SCE_CTRL_CROSS)) {
            break;
        }
        old = pad;
        sceKernelDelayThread(16 * 1000);
    }
    sceKernelExitProcess(0);
    for (;;) {
    }
}

/* Every failure ends here: shown on screen, logged, and saved for the game. */
static void Fail(const char *step, int code, const char *fmt, ...) __attribute__((format(printf, 3, 4), noreturn));
static void Fail(const char *step, int code, const char *fmt, ...)
{
    char    detail[384];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(detail, sizeof(detail), fmt, ap);
    va_end(ap);
    Log("FAILED at %s: 0x%08X %s", step, (unsigned)code, detail);
    WriteResult("error", step, code, detail);
    Say("\n\e[31;1mThe update failed (step: %s, code 0x%08X).\e[0m\n%s\n", step, (unsigned)code, detail);
    Say("Nothing was removed: the installed game still works.\n");
    Say("The details are in " LOG_FILE "\n");
    WaitCrossAndExit();
}

/* ---------- files ---------- */

static int MkdirAll(const char *path)
{
    char tmp[512];
    int  res = 0;

    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 5; *p; p++) { /* skip "ux0:/" */
        if (*p == '/') {
            *p = 0;
            sceIoMkdir(tmp, 0777);
            *p = '/';
        }
    }
    res = sceIoMkdir(tmp, 0777);
    return (res < 0 && (unsigned)res != 0x80010011) ? res : 0; /* 0x80010011: already exists */
}

static void RemoveTree(const char *path)
{
    SceUID       dir = sceIoDopen(path);
    SceIoDirent  ent;
    char         child[512];

    if (dir < 0) {
        sceIoRemove(path);
        return;
    }
    memset(&ent, 0, sizeof(ent));
    while (sceIoDread(dir, &ent) > 0) {
        snprintf(child, sizeof(child), "%s/%s", path, ent.d_name);
        if (SCE_S_ISDIR(ent.d_stat.st_mode)) {
            RemoveTree(child);
        } else {
            sceIoRemove(child);
        }
        memset(&ent, 0, sizeof(ent));
    }
    sceIoDclose(dir);
    sceIoRmdir(path);
}

static int ReadWhole(const char *path, unsigned char **out, int *size)
{
    SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
    int    len, got;

    if (fd < 0) {
        return fd;
    }
    len = (int)sceIoLseek(fd, 0, SCE_SEEK_END);
    sceIoLseek(fd, 0, SCE_SEEK_SET);
    *out = malloc(len + 1);
    if (!*out) {
        sceIoClose(fd);
        return -1;
    }
    got = sceIoRead(fd, *out, len);
    sceIoClose(fd);
    if (got != len) {
        free(*out);
        return got < 0 ? got : -1;
    }
    (*out)[len] = 0;
    *size       = len;
    return 0;
}

/* ---------- .vpk (zip) extraction ---------- */

static uint16_t U16(const unsigned char *p) { return p[0] | (p[1] << 8); }
static uint32_t U32(const unsigned char *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

/* Unpacks one entry whose data starts at 'data' (stored or deflated) into 'path'. */
static int ExtractEntry(SceUID zfd, uint32_t dataOff, int method, uint32_t csize, uint32_t usize, uint32_t crc,
                        const char *path)
{
    static unsigned char in[64 * 1024], out[64 * 1024];
    z_stream             zs;
    uint32_t             left = csize, check = crc32(0, NULL, 0), written = 0;
    SceUID               ofd;
    int                  zr = Z_OK, res = 0;

    ofd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    if (ofd < 0) {
        Fail("extract", ofd, "Could not create %s (the memory card may be full or read-only).", path);
    }
    sceIoLseek(zfd, dataOff, SCE_SEEK_SET);
    memset(&zs, 0, sizeof(zs));
    if (method == 8 && inflateInit2(&zs, -MAX_WBITS) != Z_OK) {
        Fail("extract", 0, "zlib could not start (out of memory).");
    }
    while (left > 0 || (method == 8 && zr != Z_STREAM_END && zs.avail_in)) {
        int n = sceIoRead(zfd, in, left > sizeof(in) ? sizeof(in) : left);
        if (n <= 0) {
            res = n < 0 ? n : -1;
            break;
        }
        left -= n;
        if (method == 0) {
            if (sceIoWrite(ofd, in, n) != n) {
                res = -2;
                break;
            }
            check = crc32(check, in, n);
            written += n;
            continue;
        }
        zs.next_in  = in;
        zs.avail_in = n;
        do {
            zs.next_out  = out;
            zs.avail_out = sizeof(out);
            zr           = inflate(&zs, Z_NO_FLUSH);
            if (zr != Z_OK && zr != Z_STREAM_END) {
                res = -3;
                break;
            }
            {
                int got = sizeof(out) - zs.avail_out;
                if (got && sceIoWrite(ofd, out, got) != got) {
                    res = -2;
                    break;
                }
                check = crc32(check, out, got);
                written += got;
            }
        } while (zs.avail_out == 0 && zr != Z_STREAM_END);
        if (res || zr == Z_STREAM_END) {
            break;
        }
    }
    if (method == 8) {
        inflateEnd(&zs);
    }
    sceIoClose(ofd);
    if (res == -2) {
        Fail("extract", 0, "Writing %s failed: the memory card is probably full.", path);
    }
    if (res) {
        Fail("extract", res, "The downloaded package is damaged (reading %s failed).", path);
    }
    if (written != usize || check != crc) {
        Fail("extract", 0, "The downloaded package is damaged (%s: wrong size or checksum).", path);
    }
    return 0;
}

static void ExtractVpk(const char *vpk, const char *dir)
{
    unsigned char tail[22 + 65535], hdr[46], local[30];
    char          name[512], path[600];
    SceUID        fd;
    int           size, tailLen, i;
    uint32_t      cdOff, cdCount, pos;

    fd = sceIoOpen(vpk, SCE_O_RDONLY, 0);
    if (fd < 0) {
        Fail("extract", fd, "The downloaded package %s is missing.", vpk);
    }
    size    = (int)sceIoLseek(fd, 0, SCE_SEEK_END);
    tailLen = size < (int)sizeof(tail) ? size : (int)sizeof(tail);
    sceIoLseek(fd, size - tailLen, SCE_SEEK_SET);
    if (sceIoRead(fd, tail, tailLen) != tailLen) {
        Fail("extract", 0, "Could not read the downloaded package.");
    }
    for (i = tailLen - 22; i >= 0 && U32(tail + i) != 0x06054b50; i--) {
    }
    if (i < 0) {
        Fail("extract", 0, "The downloaded package is not a valid .vpk (no zip directory).");
    }
    cdCount = U16(tail + i + 10);
    cdOff   = U32(tail + i + 16);

    RemoveTree(dir);
    if (MkdirAll(dir) < 0) {
        Fail("extract", 0, "Could not create %s.", dir);
    }

    pos = cdOff;
    for (uint32_t e = 0; e < cdCount; e++) {
        uint32_t nameLen, extraLen, commentLen, localOff, csize, usize, crc;
        int      method;

        sceIoLseek(fd, pos, SCE_SEEK_SET);
        if (sceIoRead(fd, hdr, 46) != 46 || U32(hdr) != 0x02014b50) {
            Fail("extract", 0, "The downloaded package is damaged (zip directory entry %u).", (unsigned)e);
        }
        method     = U16(hdr + 10);
        crc        = U32(hdr + 16);
        csize      = U32(hdr + 20);
        usize      = U32(hdr + 24);
        nameLen    = U16(hdr + 28);
        extraLen   = U16(hdr + 30);
        commentLen = U16(hdr + 32);
        localOff   = U32(hdr + 42);
        if (nameLen >= sizeof(name)) {
            Fail("extract", 0, "The downloaded package has a file name that is too long.");
        }
        sceIoRead(fd, name, nameLen);
        name[nameLen] = 0;
        pos += 46 + nameLen + extraLen + commentLen;

        if (strstr(name, "..") || name[0] == '/') {
            Fail("extract", 0, "The downloaded package has an unsafe file name: %s", name);
        }
        snprintf(path, sizeof(path), "%s/%s", dir, name);
        if (nameLen && name[nameLen - 1] == '/') {
            MkdirAll(path);
            continue;
        }
        {
            char *slash = strrchr(path, '/');
            *slash      = 0;
            MkdirAll(path);
            *slash = '/';
        }
        if (method != 0 && method != 8) {
            Fail("extract", method, "The downloaded package uses an unknown compression (%s).", name);
        }
        sceIoLseek(fd, localOff, SCE_SEEK_SET);
        if (sceIoRead(fd, local, 30) != 30 || U32(local) != 0x04034b50) {
            Fail("extract", 0, "The downloaded package is damaged (%s).", name);
        }
        ExtractEntry(fd, localOff + 30 + U16(local + 26) + U16(local + 28), method, csize, usize, crc, path);
        Say("  %s\n", name);
    }
    sceIoClose(fd);
    Log("unpacked %u files", (unsigned)cdCount);
}

/* ---------- package header (sce_sys/package/head.bin) ---------- */

/* The installer checks these digests of the header (same scheme as VitaShell). */
static void PkgHmac(const unsigned char *data, unsigned len, unsigned char hmac[16])
{
    unsigned char sha[20], buf[64];

    SHA1(data, len, sha);
    memset(buf, 0, sizeof(buf));
    memcpy(&buf[0], &sha[4], 8);
    memcpy(&buf[8], &sha[4], 8);
    memcpy(&buf[16], &sha[12], 4);
    buf[20] = sha[16];
    buf[21] = sha[1];
    buf[22] = sha[2];
    buf[23] = sha[3];
    memcpy(&buf[24], &buf[16], 8);
    SHA1(buf, sizeof(buf), sha);
    memcpy(hmac, sha, 16);
}

static uint32_t BE32(const unsigned char *p) { return ((uint32_t)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }

/* A string of param.sfo, "" when missing. */
static void SfoString(const unsigned char *sfo, int size, const char *key, char *out, int outSize)
{
    uint32_t keyTable, dataTable, count;

    out[0] = 0;
    if (size < 20 || memcmp(sfo, "\0PSF", 4)) {
        return;
    }
    keyTable  = U32(sfo + 8);
    dataTable = U32(sfo + 12);
    count     = U32(sfo + 16);
    for (uint32_t i = 0; i < count && 20 + i * 16 + 16 <= (uint32_t)size; i++) {
        const unsigned char *e = sfo + 20 + i * 16;
        const char          *k = (const char *)sfo + keyTable + U16(e);
        if (!strcmp(k, key)) {
            uint32_t len = U32(e + 4), off = dataTable + U32(e + 12);
            if (len >= (uint32_t)outSize) {
                len = outSize - 1;
            }
            if (off + len <= (uint32_t)size) {
                memcpy(out, sfo + off, len);
                out[len] = 0;
            }
            return;
        }
    }
}

static void MakeHeadBin(const char *dir, const char *expectedTitle)
{
    unsigned char *sfo = NULL, head[sizeof(g_headBinTemplate)], hmac[16];
    int            sfoSize = 0, res;
    char           titleId[16], contentId[48], id[48];
    uint32_t       len, off, out;
    SceUID         fd;

    {
        char sfoPath[300];
        snprintf(sfoPath, sizeof(sfoPath), "%s/sce_sys/param.sfo", dir);
        res = ReadWhole(sfoPath, &sfo, &sfoSize);
    }
    if (res < 0) {
        Fail("header", res, "%s has no sce_sys/param.sfo.", dir);
    }
    SfoString(sfo, sfoSize, "TITLE_ID", titleId, sizeof(titleId));
    SfoString(sfo, sfoSize, "CONTENT_ID", contentId, sizeof(contentId));
    free(sfo);
    if (strcmp(titleId, expectedTitle)) {
        Fail("header", 0, "%s is for another app (title id '%s', expected %s).", dir, titleId, expectedTitle);
    }

    memcpy(head, g_headBinTemplate, sizeof(head));
    snprintf(id, sizeof(id), "EP9000-%s_00-0000000000000000", titleId);
    strncpy((char *)&head[0x30], contentId[0] ? contentId : id, 48);

    len = BE32(&head[0xD0]);
    PkgHmac(head, len, hmac);
    memcpy(&head[len], hmac, 16);
    off = BE32(&head[0x8]);
    len = BE32(&head[0x10]);
    out = BE32(&head[0xD4]);
    PkgHmac(&head[off], len - 64, hmac);
    memcpy(&head[out], hmac, 16);
    len = BE32(&head[0xE8]);
    PkgHmac(head, len, hmac);
    memcpy(&head[len], hmac, 16);

    {
        char headPath[300];
        snprintf(headPath, sizeof(headPath), "%s/sce_sys/package", dir);
        MkdirAll(headPath);
        snprintf(headPath, sizeof(headPath), "%s/sce_sys/package/head.bin", dir);
        fd = sceIoOpen(headPath, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    }
    if (fd < 0 || sceIoWrite(fd, head, sizeof(head)) != (int)sizeof(head)) {
        Fail("header", fd, "Could not write the package header (memory card full?).");
    }
    sceIoClose(fd);
}

/* ---------- install ---------- */

static int LoadPaf(void)
{
    static uint32_t argp[] = {0x180000, -1, -1, 1, -1, -1};
    int             result = -1;
    uint32_t        buf[4];

    buf[0] = sizeof(buf);
    buf[1] = (uint32_t)&result;
    buf[2] = -1;
    buf[3] = -1;
    return sceSysmoduleLoadModuleInternalWithArg(SCE_SYSMODULE_INTERNAL_PAF, sizeof(argp), argp, buf);
}

static void InstallerUp(void)
{
    int res = LoadPaf();
    if (res < 0) {
        Fail("install", res, "The Vita refused to load its installer (ScePaf). Enable Unsafe Homebrew in "
                             "Settings > HENkaku Settings and try again.");
    }
    res = sceSysmoduleLoadModuleInternal(SCE_SYSMODULE_INTERNAL_PROMOTER_UTIL);
    if (res < 0) {
        Fail("install", res, "The Vita refused to load its installer (PromoterUtil). Enable Unsafe Homebrew "
                             "in Settings > HENkaku Settings and try again.");
    }
    res = scePromoterUtilityInit();
    if (res < 0) {
        Fail("install", res, "The Vita's installer did not start.");
    }
}

static void InstallerDown(void)
{
    scePromoterUtilityExit();
    sceSysmoduleUnloadModuleInternal(SCE_SYSMODULE_INTERNAL_PROMOTER_UTIL);
}

/* Installs the package unpacked in dir (the installer must be up). */
static void Promote(const char *dir, const char *what)
{
    int res, state = 0, result = 0;

    res = scePromoterUtilityPromotePkgWithRif(dir, 1);
    if (res >= 0) {
        /* sync=1 returns when done; the result code tells whether it worked */
        scePromoterUtilityGetState(&state);
        scePromoterUtilityGetResult(&result);
        Log("promote %s: ret=0x%08X state=%d result=0x%08X", what, (unsigned)res, state, (unsigned)result);
        if (result < 0) {
            res = result;
        }
    }
    if (res < 0) {
        Fail("install", res, "The Vita's installer rejected %s. If the memory card is almost full, free some "
                             "space; otherwise report this code.", what);
    }
}

/* Copies a directory tree (the helper title shipped in app0:updater/helper). */
static void CopyTree(const char *from, const char *to)
{
    SceUID      dir = sceIoDopen(from);
    SceIoDirent ent;
    char        src[512], dst[512];
    static unsigned char buf[64 * 1024];

    if (dir < 0) {
        Fail("helper", dir, "This version has no %s (reinstall the game's .vpk).", from);
    }
    MkdirAll(to);
    memset(&ent, 0, sizeof(ent));
    while (sceIoDread(dir, &ent) > 0) {
        snprintf(src, sizeof(src), "%s/%s", from, ent.d_name);
        snprintf(dst, sizeof(dst), "%s/%s", to, ent.d_name);
        if (SCE_S_ISDIR(ent.d_stat.st_mode)) {
            CopyTree(src, dst);
        } else {
            SceUID in = sceIoOpen(src, SCE_O_RDONLY, 0), out;
            int    n;
            if (in < 0) Fail("helper", in, "Could not read %s.", src);
            out = sceIoOpen(dst, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
            if (out < 0) Fail("helper", out, "Could not write %s (memory card full?).", dst);
            while ((n = sceIoRead(in, buf, sizeof(buf))) > 0) {
                if (sceIoWrite(out, buf, n) != n) Fail("helper", 0, "Writing %s failed (memory card full?).", dst);
            }
            sceIoClose(in);
            sceIoClose(out);
        }
        memset(&ent, 0, sizeof(ent));
    }
    sceIoDclose(dir);
}

/* Opens another title and ends this process (as VitaShell launches an app). */
static void LaunchAndExit(const char *titleId) __attribute__((noreturn));
static void LaunchAndExit(const char *titleId)
{
    char uri[64];
    snprintf(uri, sizeof(uri), "psgm:play?titleid=%s", titleId);
    Log("launching %s", titleId);
    sceAppMgrLaunchAppByUri(0xFFFFF, uri);
    sceKernelDelayThread(10000);
    sceAppMgrLaunchAppByUri(0xFFFFF, uri);
    sceKernelExitProcess(0);
    for (;;) {
    }
}

/* Replaces an installed language pack (the game had it open, so it could not do it). */
static void MoveLanguagePack(const char *src, const char *dst)
{
    int res;

    sceIoRemove(dst);
    res = sceIoRename(src, dst);
    if (res < 0) {
        Fail("language", res, "The game was updated, but its language pack could not be replaced: copy %s to %s "
                              "yourself.", src, dst);
    }
    Log("language pack: %s -> %s", src, dst);
}

/* The game's next start removes the helper title (Cleanup) until it is gone. */
static void MarkHelperForRemoval(void)
{
    SceUID fd = sceIoOpen(CLEANUP_FLAG, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    if (fd >= 0) sceIoClose(fd);
    sceIoRemove(CLEANED_FLAG); /* a stale one would skip that start */
}

static int Exists(const char *path)
{
    SceIoStat st;
    return sceIoGetstat(path, &st) >= 0;
}

static void ReadPlan(char *vpk, int vpkSize, char *langSrc, char *langDst, int langSize)
{
    unsigned char *plan = NULL;
    int            planSize = 0;

    if (ReadWhole(PLAN_FILE, &plan, &planSize) < 0) {
        Fail("plan", 0, "There is no update to install (" PLAN_FILE " is missing). Start the game and check "
                        "for updates again.");
    }
    for (char *line = strtok((char *)plan, "\r\n"); line; line = strtok(NULL, "\r\n")) {
        if (!strncmp(line, "version=", 8)) {
            snprintf(g_version, sizeof(g_version), "%s", line + 8);
        } else if (!strncmp(line, "vpk=", 4)) {
            snprintf(vpk, vpkSize, "%s", line + 4);
        } else if (!strncmp(line, "lang=", 5)) {
            char *bar = strchr(line + 5, '|');
            if (bar) {
                *bar = 0;
                snprintf(langSrc, langSize, "%s", line + 5);
                snprintf(langDst, langSize, "%s", bar + 1);
            }
        }
    }
    free(plan);
}

/* OMHA00001, from the game: unpack the new version, install the helper title, open it. */
static void Stage(void)
{
    char vpk[256] = "", langSrc[256] = "", langDst[256] = "";

    ReadPlan(vpk, sizeof(vpk), langSrc, langDst, sizeof(langSrc));
    Log("staging v%s from %s", g_version, vpk);

    Say("Unpacking version %s...\n", g_version);
    ExtractVpk(vpk, PKG_DIR);
    MakeHeadBin(PKG_DIR, GAME_TITLE);

    Say("\nPreparing the installer...\n");
    RemoveTree(HELPER_DIR);
    CopyTree("app0:updater/helper", HELPER_DIR);
    MakeHeadBin(HELPER_DIR, HELPER_TITLE);

    InstallerUp();
    scePromoterUtilityDeletePkg(HELPER_TITLE); /* one left by an earlier update, if any */
    Promote(HELPER_DIR, "the installer");
    MarkHelperForRemoval(); /* from here on the game removes it, even if the update fails */
    InstallerDown();
    RemoveTree(HELPER_DIR);

    Say("Starting the installer...\n");
    LaunchAndExit(HELPER_TITLE);
}

/* OMHA00002: with the game closed, install it, then open it again. */
static void Helper(void)
{
    char vpk[256] = "", langSrc[256] = "", langDst[256] = "";

    MarkHelperForRemoval(); /* the game removes this title at its next start, whatever happens here */

    ReadPlan(vpk, sizeof(vpk), langSrc, langDst, sizeof(langSrc));
    Log("installing v%s", g_version);

    Say("Installing version %s. Do not turn off the Vita...\n", g_version);
    sceKernelPowerLock(0);
    InstallerUp();
    Promote(PKG_DIR, "the new version");
    InstallerDown();
    sceKernelPowerUnlock(0);

    if (langSrc[0]) {
        Say("Updating the language pack...\n");
        MoveLanguagePack(langSrc, langDst);
    }

    sceIoRemove(vpk);
    sceIoRemove(PLAN_FILE);
    RemoveTree(PKG_DIR);
    WriteResult("ok", "done", 0, "");
    Log("installed v%s", g_version);

    Say("\n\e[32;1mOpenMoHAA %s is installed.\e[0m Your settings and saves are kept.\n", g_version);
    Say("Starting the game...\n");
    sceKernelDelayThread(1500 * 1000);
    LaunchAndExit(GAME_TITLE);
}

/* The game's boot picture on screen (as sys_vita.c shows it), so the cleanup between two
 * starts of the game is not seen. */
static void ShowBootPicture(void)
{
    png_image image;
    void     *base = NULL;
    SceUID    block = sceKernelAllocMemBlock("boot_picture", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, 0x200000, NULL);

    if (block < 0 || sceKernelGetMemBlockBase(block, &base) < 0) return;
    memset(base, 0, 0x200000);
    memset(&image, 0, sizeof(image));
    image.version = PNG_IMAGE_VERSION;
    if (png_image_begin_read_from_file(&image, "app0:sce_sys/pic0.png")) {
        if (image.width == 960 && image.height == 544) {
            image.format = PNG_FORMAT_RGBA;
            png_image_finish_read(&image, NULL, base, 960 * 4, NULL);
        }
        png_image_free(&image);
    }
    {
        SceDisplayFrameBuf fb;
        memset(&fb, 0, sizeof(fb));
        fb.size        = sizeof(fb);
        fb.base        = base;
        fb.pitch       = 960;
        fb.pixelformat = SCE_DISPLAY_PIXELFORMAT_A8B8G8R8;
        fb.width       = 960;
        fb.height      = 544;
        sceDisplaySetFrameBuf(&fb, SCE_DISPLAY_SETBUF_NEXTFRAME);
    }
}

/* OMHA00001, from the game's start: remove the helper title, then back to the game. The
 * flag stays until the helper is gone, so every start of the game tries again; the game
 * skips the start that comes back from here (CLEANED_FLAG), so this never loops. */
static void Cleanup(void)
{
    int    res = -1;
    SceUID fd;

    ShowBootPicture();
    /* launching the game left the helper suspended in the background, not closed, and a
     * title that is open cannot be removed (0x80103A07): close it first */
    Log("cleanup: closing the other app = 0x%08X", (unsigned)sceAppMgrDestroyOtherApp());
    sceKernelDelayThread(500 * 1000);
    if (LoadPaf() >= 0 && sceSysmoduleLoadModuleInternal(SCE_SYSMODULE_INTERNAL_PROMOTER_UTIL) >= 0
        && scePromoterUtilityInit() >= 0) {
        /* the helper may still be closing: try for a few seconds */
        for (int i = 0; i < 10; i++) {
            res = scePromoterUtilityDeletePkg(HELPER_TITLE);
            if (res >= 0) break;
            sceKernelDelayThread(500 * 1000);
        }
        InstallerDown();
    }
    Log("cleanup: removing %s = 0x%08X", HELPER_TITLE, (unsigned)res);
    if (res >= 0 || !Exists("ux0:app/" HELPER_TITLE)) {
        sceIoRemove(CLEANUP_FLAG);
    }
    fd = sceIoOpen(CLEANED_FLAG, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    if (fd >= 0) sceIoClose(fd);
    sceAppMgrLoadExec("app0:eboot.bin", NULL, NULL);
    sceKernelExitProcess(0);
}

int main(void)
{
    char self[16] = "";

    /* keep the Vita awake while installing */
    scePowerSetArmClockFrequency(444);
    sceAppMgrAppParamGetString(0, 12, self, sizeof(self)); /* 12: TITLE_ID */
    Log("updater.bin running as %s", self);

    if (strcmp(self, HELPER_TITLE) && !Exists(PLAN_FILE) && Exists(CLEANUP_FLAG)) {
        Cleanup(); /* silent: the boot picture stays on screen */
    }
    psvDebugScreenInit();
    Say("\e[33;1mOpenMoHAA updater\e[0m\n\n");
    if (!strcmp(self, HELPER_TITLE)) {
        Helper();
    } else {
        Stage();
    }
    return 0;
}
