/*
 * OpenMoHAA for PS Vita - updater.bin
 *
 * Started by the game (sceAppMgrLoadExec) with a downloaded release ready in
 * ux0:data/openmohaa/update (plan.txt): unpacks the .vpk there, checks every file, then
 * puts the new files in place of the game's own in ux0:app/OMHA00001 (the game is closed
 * while this runs, settings and saves in ux0:data are not touched), puts the new language
 * pack in place, writes result.txt (shown, translated, by the game) and starts the game.
 *
 * No second app is installed: the Vita's installer refuses to install over a running
 * title (0x80101114) and this runs as the game's title, so the files are replaced
 * directly. sce_sys (param.sfo, LiveArea) is left as installed: the system holds it, and
 * a release that changes the bubble is installed by hand.
 *
 * The boot picture stays on screen while it works; every failure is reported on screen,
 * in result.txt and in update.log.
 *
 * This file is part of OpenMoHAA, GPL v2 or later. debugScreen*.c/h are the Vita SDK
 * samples (PSPSDK BSD license).
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

#define DATA_DIR    "ux0:data/openmohaa"
#define UPDATE_DIR  DATA_DIR "/update"
#define PKG_DIR     UPDATE_DIR "/pkg"
#define GAME_TITLE  "OMHA00001"
#define GAME_DIR    "ux0:app/" GAME_TITLE
#define OLD_HELPER  "OMHA00002" /* the helper title of test builds before 0.3 */
#define PLAN_FILE   UPDATE_DIR "/plan.txt"
#define RESULT_FILE UPDATE_DIR "/result.txt"
#define LOG_FILE    UPDATE_DIR "/update.log"

/* Text on screen only when something goes wrong: the boot picture stays up otherwise. */
static int g_screen;
#define Say(...)                                                                                                       \
    do {                                                                                                               \
        if (g_screen) psvDebugScreenPrintf(__VA_ARGS__);                                                               \
    } while (0)

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
    if (!g_screen) {
        psvDebugScreenInit();
        g_screen = 1;
        Say("\e[33;1mOpenMoHAA updater\e[0m\n");
    }
    Say("\n\e[31;1mThe update failed (step: %s, code 0x%08X).\e[0m\n%s\n", step, (unsigned)code, detail);
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

/* Test builds before 0.3 installed a helper title to do the install; remove it if one is
 * still there, so no extra bubble stays on the LiveArea. */
static void RemoveOldHelper(void)
{
    int    res = -1;
    SceUID dir = sceIoDopen("ux0:app/" OLD_HELPER);

    if (dir < 0) {
        return;
    }
    sceIoDclose(dir);
    sceAppMgrDestroyOtherApp(); /* it may be suspended in the background */
    if (LoadPaf() >= 0 && sceSysmoduleLoadModuleInternal(SCE_SYSMODULE_INTERNAL_PROMOTER_UTIL) >= 0
        && scePromoterUtilityInit() >= 0) {
        res = scePromoterUtilityDeletePkg(OLD_HELPER);
        scePromoterUtilityExit();
        sceSysmoduleUnloadModuleInternal(SCE_SYSMODULE_INTERNAL_PROMOTER_UTIL);
    }
    Log("removing the old helper title %s = 0x%08X", OLD_HELPER, (unsigned)res);
}

/* One file of the new version in place of the game's: moved (same memory card, so only the
 * directory entry changes), or copied when the move is refused. */
static int PutFile(const char *src, const char *dst)
{
    static unsigned char buf[64 * 1024];
    SceUID               in, out;
    int                  n, res;

    sceIoRemove(dst);
    if (sceIoRename(src, dst) >= 0) {
        return 0;
    }
    in = sceIoOpen(src, SCE_O_RDONLY, 0);
    if (in < 0) {
        return in;
    }
    out = sceIoOpen(dst, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    if (out < 0) {
        sceIoClose(in);
        return out;
    }
    res = 0;
    while ((n = sceIoRead(in, buf, sizeof(buf))) > 0) {
        if (sceIoWrite(out, buf, n) != n) {
            res = -1;
            break;
        }
    }
    sceIoClose(in);
    sceIoClose(out);
    return res;
}

/* Every file under from/ in place of the one under to/. At the top level, sce_sys is left
 * out (the system holds the running title's param.sfo, 0x8001000D, and the LiveArea is not
 * refreshed from it anyway) and eboot.bin is left for last, so a game that has it is
 * complete. updater.bin is in use (this program): when it cannot be replaced, the old one
 * stays, which is harmless.
 * check = 1 changes nothing: it only opens each file that will be replaced for writing,
 * so a file the Vita will not let go of stops the update before the game is touched. */
static void PutTree(const char *from, const char *to, int top, int check)
{
    SceUID      dir = sceIoDopen(from);
    SceIoDirent ent;
    char        src[512], dst[512];

    if (dir < 0) {
        Fail("install", dir, "Could not read %s.", from);
    }
    if (!check) MkdirAll(to);
    memset(&ent, 0, sizeof(ent));
    while (sceIoDread(dir, &ent) > 0) {
        const int isUpdater = top && !strcmp(ent.d_name, "updater.bin");

        snprintf(src, sizeof(src), "%s/%s", from, ent.d_name);
        snprintf(dst, sizeof(dst), "%s/%s", to, ent.d_name);
        if (top && !strcmp(ent.d_name, "sce_sys")) {
            /* left as installed */
        } else if (SCE_S_ISDIR(ent.d_stat.st_mode)) {
            PutTree(src, dst, 0, check);
        } else if (check) {
            SceIoStat st;
            if (!isUpdater && sceIoGetstat(dst, &st) >= 0) {
                SceUID fd = sceIoOpen(dst, SCE_O_WRONLY, 0777); /* no truncate: nothing changes */
                if (fd < 0) {
                    Fail("install", fd, "The Vita does not let %s be replaced. Nothing was changed: the installed "
                                        "game still works.", dst);
                }
                sceIoClose(fd);
            }
        } else if (!(top && !strcmp(ent.d_name, "eboot.bin"))) {
            int res = PutFile(src, dst);
            if (res < 0 && isUpdater) {
                Log("updater.bin is in use, the old one stays (0x%08X)", (unsigned)res);
            } else if (res < 0) {
                Fail("install", res, "Could not replace %s. The game may be incomplete now: install the "
                                     "OpenMoHAA .vpk again with VitaShell (settings and saves are kept).", dst);
            }
        }
        memset(&ent, 0, sizeof(ent));
    }
    sceIoDclose(dir);
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

/* Checks that the unpacked package is this game: its param.sfo names our title. */
static void CheckPackage(const char *dir)
{
    unsigned char *sfo = NULL;
    int            size = 0, res;
    char           path[300];

    snprintf(path, sizeof(path), "%s/sce_sys/param.sfo", dir);
    res = ReadWhole(path, &sfo, &size);
    if (res < 0) {
        Fail("extract", res, "The downloaded package has no sce_sys/param.sfo.");
    }
    sfo[size] = 0;
    /* the title id is stored as plain text in the sfo's data table */
    for (int i = 0; i + 9 <= size; i++) {
        if (!memcmp(sfo + i, GAME_TITLE, 9)) {
            free(sfo);
            return;
        }
    }
    free(sfo);
    Fail("extract", 0, "The downloaded package is not OpenMoHAA (%s).", GAME_TITLE);
}

static void Install(void)
{
    char vpk[256] = "", langSrc[256] = "", langDst[256] = "", src[300], dst[300];
    int  res;

    ReadPlan(vpk, sizeof(vpk), langSrc, langDst, sizeof(langSrc));
    Log("installing v%s from %s", g_version, vpk);
    RemoveOldHelper();

    /* everything unpacked and checked (zip CRCs) before the first file of the game changes */
    ExtractVpk(vpk, PKG_DIR);
    CheckPackage(PKG_DIR);

    /* While the title's folder is mounted as app0: the Vita refuses writes to it
     * (0x8001000D), even with the game closed and only this program running: unmount it,
     * as VitaShell does to update itself. Nothing is read from app0: after this. */
    res = sceAppMgrUmount("app0:");
    Log("unmounting app0: = 0x%08X", (unsigned)res);
    PutTree(PKG_DIR, GAME_DIR, 1, 1);
    Log("every file can be replaced");
    sceKernelPowerLock(0);
    PutTree(PKG_DIR, GAME_DIR, 1, 0);
    snprintf(src, sizeof(src), "%s/eboot.bin", PKG_DIR);
    snprintf(dst, sizeof(dst), "%s/eboot.bin", GAME_DIR);
    res = PutFile(src, dst);
    if (res < 0) {
        Fail("install", res, "Could not replace %s. Install the OpenMoHAA .vpk again with VitaShell (settings "
                             "and saves are kept).", dst);
    }
    RemoveTree(GAME_DIR "/updater/helper"); /* shipped by test builds before 0.3 */
    sceIoRmdir(GAME_DIR "/updater");
    sceKernelPowerUnlock(0);

    if (langSrc[0]) {
        MoveLanguagePack(langSrc, langDst);
    }
    sceIoRemove(vpk);
    sceIoRemove(PLAN_FILE);
    RemoveTree(PKG_DIR);
    WriteResult("ok", "done", 0, "");
    Log("installed v%s", g_version);

    /* app0: still names the title's folder for the next program (only ux0:app paths are
     * refused, 0x8080201E), and that folder now holds the new version */
    res = sceAppMgrLoadExec("app0:eboot.bin", NULL, NULL);
    Log("starting app0:eboot.bin = 0x%08X", (unsigned)res);
    Fail("start", 0, "The new version is installed, but could not be started: start OpenMoHAA again from the "
                     "LiveArea.");
}

int main(void)
{
    /* faster unpacking */
    scePowerSetArmClockFrequency(444);
    ShowBootPicture();
    Log("updater.bin started");
    Install();
    return 0;
}
