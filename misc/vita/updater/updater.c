/*
 * OpenMoHAA for PS Vita - updater.bin
 *
 * The game downloads a new release (checking its SHA-256) and then replaces itself with
 * this small program (sceAppMgrLoadExec), because an app cannot install over itself while
 * it runs. This unpacks the downloaded .vpk, writes the package header the Vita's
 * installer needs, installs it over the app (settings and saves in ux0:data are kept),
 * puts the new language pack in place and writes what happened to result.txt, which the
 * game shows, translated, the next time it starts. Every step reports the exact error.
 *
 * This file is part of OpenMoHAA, GPL v2 or later. head_bin_template.h comes from
 * VitaShell (GPLv3); debugScreen*.c/h are the Vita SDK samples (PSPSDK BSD license).
 */
#include <psp2/appmgr.h>
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

static void ExtractVpk(const char *vpk)
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

    RemoveTree(PKG_DIR);
    if (MkdirAll(PKG_DIR) < 0) {
        Fail("extract", 0, "Could not create " PKG_DIR ".");
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
        snprintf(path, sizeof(path), PKG_DIR "/%s", name);
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

static void MakeHeadBin(void)
{
    unsigned char *sfo = NULL, head[sizeof(g_headBinTemplate)], hmac[16];
    int            sfoSize = 0, res;
    char           titleId[16], contentId[48], id[48];
    uint32_t       len, off, out;
    SceUID         fd;

    res = ReadWhole(PKG_DIR "/sce_sys/param.sfo", &sfo, &sfoSize);
    if (res < 0) {
        Fail("header", res, "The new version has no sce_sys/param.sfo.");
    }
    SfoString(sfo, sfoSize, "TITLE_ID", titleId, sizeof(titleId));
    SfoString(sfo, sfoSize, "CONTENT_ID", contentId, sizeof(contentId));
    free(sfo);
    if (strcmp(titleId, "OMHA00001")) {
        Fail("header", 0, "The downloaded package is for another app (title id '%s').", titleId);
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

    MkdirAll(PKG_DIR "/sce_sys/package");
    fd = sceIoOpen(PKG_DIR "/sce_sys/package/head.bin", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
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

static void Promote(void)
{
    int res, state = 0, result = 0;

    res = LoadPaf();
    if (res < 0) {
        Fail("install", res, "The Vita refused to load its installer (ScePaf). Enable \"unsafe homebrew\" in "
                             "Settings > HENkaku Settings and try again.");
    }
    res = sceSysmoduleLoadModuleInternal(SCE_SYSMODULE_INTERNAL_PROMOTER_UTIL);
    if (res < 0) {
        Fail("install", res, "The Vita refused to load its installer (PromoterUtil). Enable \"unsafe homebrew\" "
                             "in Settings > HENkaku Settings and try again.");
    }
    res = scePromoterUtilityInit();
    if (res < 0) {
        Fail("install", res, "The Vita's installer did not start.");
    }
    res = scePromoterUtilityPromotePkgWithRif(PKG_DIR, 1);
    if (res >= 0) {
        /* sync=1 returns when done; the result code tells whether it worked */
        scePromoterUtilityGetState(&state);
        scePromoterUtilityGetResult(&result);
        Log("promote: ret=0x%08X state=%d result=0x%08X", (unsigned)res, state, (unsigned)result);
        if (result < 0) {
            res = result;
        }
    }
    scePromoterUtilityExit();
    sceSysmoduleUnloadModuleInternal(SCE_SYSMODULE_INTERNAL_PROMOTER_UTIL);
    if (res < 0) {
        Fail("install", res, "The Vita's installer rejected the new version. If the memory card is almost full, "
                             "free some space; otherwise report this code.");
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

int main(void)
{
    unsigned char *plan = NULL;
    int            planSize = 0;
    char           vpk[256] = "", langSrc[256] = "", langDst[256] = "";

    psvDebugScreenInit();
    /* keep the Vita awake while installing */
    scePowerSetArmClockFrequency(444);

    Say("\e[33;1mOpenMoHAA updater\e[0m\n\n");

    if (ReadWhole(PLAN_FILE, &plan, &planSize) < 0) {
        Fail("plan", 0, "There is no update to install (" PLAN_FILE " is missing). Start the game and check "
                        "for updates again.");
    }
    for (char *line = strtok((char *)plan, "\r\n"); line; line = strtok(NULL, "\r\n")) {
        if (!strncmp(line, "version=", 8)) {
            snprintf(g_version, sizeof(g_version), "%s", line + 8);
        } else if (!strncmp(line, "vpk=", 4)) {
            snprintf(vpk, sizeof(vpk), "%s", line + 4);
        } else if (!strncmp(line, "lang=", 5)) {
            char *bar = strchr(line + 5, '|');
            if (bar) {
                *bar = 0;
                snprintf(langSrc, sizeof(langSrc), "%s", line + 5);
                snprintf(langDst, sizeof(langDst), "%s", bar + 1);
            }
        }
    }
    free(plan);
    Log("installing v%s from %s", g_version, vpk);

    Say("Unpacking version %s...\n", g_version);
    ExtractVpk(vpk);

    Say("\nPreparing the package...\n");
    MakeHeadBin();

    Say("Installing. Do not turn off the Vita...\n");
    sceKernelPowerLock(0);
    Promote();
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
    WaitCrossAndExit();
    return 0;
}
