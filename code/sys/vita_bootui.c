/*
===========================================================================
Copyright (C) 2026 the OpenMoHAA team

This file is part of OpenMoHAA source code.

OpenMoHAA source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

OpenMoHAA source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with OpenMoHAA source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/

// vita_bootui.c -- the update check at start-up, before the renderer exists.
//
// Drawn straight into the boot picture's display buffer (sys_vita.c): a dark band over
// the bottom of the picture with centred lines of text (app0:main/fonts/vita-boot.font,
// made by misc/vita/make_boot_font.py), a progress bar and a button hint. The texts come
// from the Text Language files (app0:main/vita/lang/<code>.txt, read directly: the game's
// localization is not up yet), the choices from the saved config.

#include "vita_update.h"
#include "../qcommon/localization.h"

#include <psp2/appmgr.h>
#include <psp2/ctrl.h>
#include <psp2/display.h>
#include <psp2/kernel/threadmgr.h>

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FB_W     960
#define FB_H     544
#define BAND_Y   280 /* the band grows up to here from the bottom of the picture... */
#define BAND_MIN 372 /* ...and covers at least this much */
#define BAND_H   (FB_H - BAND_Y)

typedef struct __attribute__((packed)) { /* 14 bytes, as in the file */
    uint16_t w, h;
    int16_t  x, y;
    uint16_t advance;
    uint32_t offset;
} glyph_t;

static uint32_t      *s_fb;      /* where the band is put together, off screen */
static uint32_t      *s_screen;  /* the picture on display */
static uint32_t      *s_band;   /* the picture under the band, to redraw it */
static glyph_t        s_glyph[224];
static unsigned char *s_bits;
static int            s_lineH, s_ascent;
static char          *s_lang;   /* the Text Language file (Latin-1), NULL = English */

// ---------- font ----------

static int BootUI_LoadFont(void)
{
    FILE         *f = fopen("app0:main/fonts/vita-boot.font", "rb");
    unsigned char hdr[8];
    uint32_t      size;

    if (!f) return 0;
    if (fread(hdr, 1, 8, f) != 8 || memcmp(hdr, "VBF1", 4) || fread(s_glyph, sizeof(glyph_t), 224, f) != 224
        || fread(&size, 4, 1, f) != 1 || !(s_bits = malloc(size)) || fread(s_bits, 1, size, f) != size) {
        fclose(f);
        free(s_bits);
        s_bits = NULL;
        return 0;
    }
    fclose(f);
    s_lineH  = hdr[4] | (hdr[5] << 8);
    s_ascent = hdr[6] | (hdr[7] << 8);
    return 1;
}

static int BootUI_TextWidth(const char *s)
{
    int w = 0;
    for (; *s; s++) {
        const unsigned char c = (unsigned char)*s;
        if (c >= 32) w += s_glyph[c - 32].advance;
    }
    return w;
}

/* Latin-1 text, alpha-blended in colour (0xBBGGRR) at x, y (top of the line). */
static void BootUI_Text(int x, int y, const char *s, uint32_t colour)
{
    const int r = colour & 0xFF, g = (colour >> 8) & 0xFF, b = (colour >> 16) & 0xFF;

    for (; *s; s++) {
        const unsigned char c = (unsigned char)*s;
        const glyph_t      *gl;
        if (c < 32) continue;
        gl = &s_glyph[c - 32];
        for (int gy = 0; gy < gl->h; gy++) {
            const int py = y + gl->y + gy;
            if (py < 0 || py >= FB_H) continue;
            for (int gx = 0; gx < gl->w; gx++) {
                const int px = x + gl->x + gx;
                const int a  = s_bits[gl->offset + gy * gl->w + gx];
                uint32_t *d;
                if (!a || px < 0 || px >= FB_W) continue;
                d = &s_fb[py * FB_W + px];
                {
                    const int dr = *d & 0xFF, dg = (*d >> 8) & 0xFF, db = (*d >> 16) & 0xFF;
                    *d = 0xFF000000u | ((db + (b - db) * a / 255) << 16) | ((dg + (g - dg) * a / 255) << 8)
                       | (dr + (r - dr) * a / 255);
                }
            }
        }
        x += gl->advance;
    }
}

/* Lines no wider than the screen: cut at spaces. Returns the next y; draw = 0 only measures. */
static int BootUI_Wrapped(int y, const char *text, uint32_t colour, int draw)
{
    char line[256];

    while (*text) {
        int n = 0, cut = 0;
        while (text[n] && n < (int)sizeof(line) - 1) {
            line[n] = text[n];
            line[n + 1] = 0;
            if (BootUI_TextWidth(line) > FB_W - 80) break;
            if (text[n] == ' ') cut = n;
            n++;
        }
        if (text[n] && cut) n = cut;
        memcpy(line, text, n);
        line[n] = 0;
        if (draw) BootUI_Text((FB_W - BootUI_TextWidth(line)) / 2, y, line, colour);
        y += s_lineH;
        text += n;
        while (*text == ' ') text++;
    }
    return y;
}

// ---------- texts ----------

/* The translation of an English text, from the Text Language file; the text itself when
 * there is none. */
static const char *BootUI_Tr(const char *en)
{
    static char out[4][512];
    static int  which;
    char        key[300];
    const char *p, *q;
    char       *o;

    if (!s_lang) return en;
    snprintf(key, sizeof(key), "{ \"%s\" \"", en);
    p = strstr(s_lang, key);
    if (!p) return en;
    p += strlen(key);
    q = strchr(p, '"');
    if (!q || q - p >= (int)sizeof(out[0])) return en;
    o = out[which++ & 3];
    memcpy(o, p, q - p);
    o[q - p] = 0;
    return o;
}

/* A setting from the saved config ("seta name \"value\""), or def. */
static void BootUI_ConfigValue(const char *cfg, const char *name, char *out, int outSize, const char *def)
{
    char        key[64];
    const char *p;

    snprintf(out, outSize, "%s", def);
    if (!cfg) return;
    snprintf(key, sizeof(key), "seta %s \"", name);
    p = strstr(cfg, key);
    if (!p) {
        snprintf(key, sizeof(key), "seta %s ", name);
        p = strstr(cfg, key);
        if (!p) return;
    }
    p += strlen(key);
    {
        int n = 0;
        while (p[n] && p[n] != '"' && p[n] != '\n' && p[n] != '\r' && n < outSize - 1) n++;
        memcpy(out, p, n);
        out[n] = 0;
    }
}

static char *BootUI_ReadFile(const char *path)
{
    FILE *f = fopen(path, "rb");
    long  n;
    char *data;

    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    data = malloc(n + 1);
    if (data && fread(data, 1, n, f) == (size_t)n) {
        data[n] = 0;
    } else {
        free(data);
        data = NULL;
    }
    fclose(f);
    return data;
}

// ---------- screen ----------

/* The band with up to two messages, a progress bar (progress < 0: none) and a hint. */
static char s_lastKey[1024]; /* what is on screen (BootUI_Show, BootUI_ShowList) */

static void BootUI_Show(const char *title, const char *detail, float progress, const char *hint)
{
    char *const last = s_lastKey;
    char        key[1024];
    int         h = 0, y;

    /* drawn again only when something changes (the bar: when it grows by a pixel) */
    snprintf(key, sizeof(key), "%s|%s|%d|%s", title ? title : "", detail ? detail : "",
             progress < 0.0f ? -1 : (int)((FB_W - 240) * (progress > 1.0f ? 1.0f : progress)), hint ? hint : "");
    if (!strcmp(key, last)) return;
    snprintf(last, sizeof(s_lastKey), "%s", key);

    /* the band's height: the texts, the bar and the hint, under each other */
    if (title) h = BootUI_Wrapped(h, title, 0, 0);
    if (detail && detail[0]) h = BootUI_Wrapped(h + 4, detail, 0, 0);
    if (progress >= 0.0f) h += 24;
    if (hint && hint[0]) h += 8 + s_lineH;
    y = FB_H - 18 - h;
    if (y - 18 > BAND_MIN) y = BAND_MIN + 18;
    if (y - 18 < BAND_Y) y = BAND_Y + 18;

    memcpy(s_fb + BAND_Y * FB_W, s_band, BAND_H * FB_W * 4);
    for (int i = (y - 18) * FB_W; i < FB_H * FB_W; i++) {
        const uint32_t p = s_fb[i];
        s_fb[i] = 0xFF000000u | ((((p >> 16) & 0xFF) * 3 / 10) << 16) | ((((p >> 8) & 0xFF) * 3 / 10) << 8)
                | ((p & 0xFF) * 3 / 10);
    }
    if (title) y = BootUI_Wrapped(y, title, 0xFFFFFF, 1);
    if (detail && detail[0]) y = BootUI_Wrapped(y + 4, detail, 0xB0B0B0, 1);
    if (progress >= 0.0f) {
        const int x0 = 120, w = FB_W - 240, by = y + 10, done = (int)(w * (progress > 1.0f ? 1.0f : progress));
        for (int py = by; py < by + 14 && py < FB_H; py++) {
            for (int px = x0; px < x0 + w; px++) {
                s_fb[py * FB_W + px] = px < x0 + done ? 0xFF40A070u : 0xFF303030u;
            }
        }
        y += 24;
    }
    if (hint && hint[0]) {
        BootUI_Text((FB_W - BootUI_TextWidth(hint)) / 2, y + 8, hint, 0x40C8F0);
    }
    /* whole, right after the vertical blank: the display reaches the band's rows only
     * after the copy is done, so it never shows a half drawn band */
    sceDisplayWaitVblankStart();
    memcpy(s_screen + BAND_Y * FB_W, s_fb + BAND_Y * FB_W, BAND_H * FB_W * 4);
}

static unsigned int BootUI_Buttons(void)
{
    SceCtrlData pad;
    memset(&pad, 0, sizeof(pad));
    sceCtrlPeekBufferPositive(0, &pad, 1);
    return pad.buttons;
}

/* Waits for one of the buttons in mask (pressed after the call); returns it, or 0 after
 * timeoutMs (0: no timeout). */
static unsigned int BootUI_WaitButton(unsigned int mask, int timeoutMs)
{
    unsigned int prev = BootUI_Buttons();
    for (int waited = 0; !timeoutMs || waited < timeoutMs; waited += 16) {
        const unsigned int now = BootUI_Buttons(), pressed = now & ~prev & mask;
        if (pressed) return pressed;
        prev = now;
        sceKernelDelayThread(16 * 1000);
    }
    return 0;
}

// ---------- set up ----------

/* Set by Vita settings > System > Change campaign (cl_uiview3d.cpp) before the game starts
 * again: the campaign choice waits for the player, and the update check is skipped. */
#define CHOOSE_FLAG "ux0:data/openmohaa/vita_choose"

static int BootUI_Exists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

/* The saved config's language (the system's when never chosen) and one setting. */
static void BootUI_Settings(const char *name, char *value, int valueSize, const char *def, char *lang, int langSize)
{
    char *cfg = BootUI_ReadFile("ux0:data/openmohaa/main/configs/omconfig.cfg");
    BootUI_ConfigValue(cfg, name, value, valueSize, def);
    BootUI_ConfigValue(cfg, "vita_language", lang, langSize, "");
    free(cfg);
    if (!lang[0]) snprintf(lang, langSize, "%s", Sys_DefaultTextLanguage());
}

/* Font, texts and buffers for drawing on the boot picture; 0: nothing can be shown. */
static int BootUI_Begin(uint32_t *framebuffer, const char *lang)
{
    if (!framebuffer || !BootUI_LoadFont()) return 0;
    s_screen = framebuffer;
    s_fb     = malloc(FB_H * FB_W * 4);
    s_band   = malloc(BAND_H * FB_W * 4);
    if (!s_fb || !s_band) {
        free(s_fb);
        free(s_band);
        s_fb = s_band = NULL;
        return 0;
    }
    memcpy(s_band, s_screen + BAND_Y * FB_W, BAND_H * FB_W * 4);
    if (strcmp(lang, "en")) {
        char path[64];
        snprintf(path, sizeof(path), "app0:main/vita/lang/%s.txt", lang);
        s_lang = BootUI_ReadFile(path);
    }
    s_lastKey[0] = 0;
    return 1;
}

/* The picture as it was, and everything freed. */
static void BootUI_End(void)
{
    sceDisplayWaitVblankStart();
    memcpy(s_screen + BAND_Y * FB_W, s_band, BAND_H * FB_W * 4);
    free(s_band);
    free(s_fb);
    s_band = NULL;
    s_fb   = NULL;
    free(s_lang);
    s_lang = NULL;
    free(s_bits);
    s_bits = NULL;
}

// ---------- the check ----------

void Sys_VitaBootUpdateCheck(uint32_t *framebuffer)
{
    char              enabled[8], lang[16];
    vitaUpdateState_t st;

    BootUI_Settings("vita_update_check", enabled, sizeof(enabled), "1", lang, sizeof(lang));
    if (atoi(enabled) == 0 || BootUI_Exists(CHOOSE_FLAG) /* only changing campaign */
        || !BootUI_Begin(framebuffer, lang)) {
        return;
    }

    BootUI_Show(BootUI_Tr("Looking for a new version..."), NULL, -1.0f, NULL);
    VitaUpdate_Check(lang);
    for (int t = 0; (st = VitaUpdate_State()) == VU_CHECKING && t < 30 * 20; t++) {
        sceKernelDelayThread(50 * 1000);
    }

    if (st == VU_AVAILABLE) {
        char text[512];
        snprintf(text, sizeof(text), BootUI_Tr("Version %s is available (you have %s)."), VitaUpdate_NewVersion(),
                 VitaUpdate_CurrentVersion());
        BootUI_Show(text, BootUI_Tr("Settings and saves are kept."), -1.0f, BootUI_Tr("X: update now   O: later"));
        if (BootUI_WaitButton(SCE_CTRL_CROSS | SCE_CTRL_CIRCLE, 0) == SCE_CTRL_CROSS) {
            VitaUpdate_Download();
            while ((st = VitaUpdate_State()) == VU_DOWNLOADING) {
                long long done, total;
                VitaUpdate_Progress(&done, &total);
                snprintf(text, sizeof(text), BootUI_Tr("Downloading version %s..."), VitaUpdate_NewVersion());
                BootUI_Show(text, NULL, total > 0 ? (float)done / (float)total : 0.0f, BootUI_Tr("O: cancel"));
                if (BootUI_Buttons() & SCE_CTRL_CIRCLE) VitaUpdate_Cancel();
                sceKernelDelayThread(100 * 1000);
            }
            if (st == VU_READY) {
                BootUI_Show(BootUI_Tr("The game will close, install the new version and start again."), NULL, -1.0f,
                            NULL);
                sceKernelDelayThread(1200 * 1000);
                VitaUpdate_Launch(); /* returns only when the Vita refused */
                st = VitaUpdate_State();
            }
        } else {
            st = VU_IDLE;
        }
    }
    /* every failure is told, but for the Wi-Fi being off (nothing to do about it here) */
    if (st == VU_ERROR && strcmp(VitaUpdate_Error(), "The Vita is not connected to Wi-Fi.")) {
        BootUI_Show(BootUI_Tr(VitaUpdate_Error()), VitaUpdate_ErrorDetail(), -1.0f,
                    BootUI_Tr("Press any button to continue."));
        BootUI_WaitButton(~0u, 15000); /* nobody looking: the game goes on by itself */
    }
    BootUI_End();
}

// ---------- the campaign ----------
/*
Allied Assault, Spearhead or Breakthrough: the expansions are their own games for the
engine (com_target_game, which must be set before Com_Init: sys_main.c adds it to the
command line). Offered only when an expansion is on the memory card, next to main:
ux0:data/openmohaa/mainta (Spearhead) and maintt (Breakthrough). The last choice is
remembered and taken by itself when no button is pressed for a while.
*/
#define GAME_CHOICE_FILE "ux0:data/openmohaa/vita_game.txt"
#define GAME_CHOICE_WAIT_MS 8000

static int s_targetGame;

int Sys_VitaTargetGame(void)
{
    return s_targetGame;
}

static int BootUI_HasGame(int game)
{
    static const char *const pak[3] = {"", "ux0:data/openmohaa/mainta/pak1.pk3", "ux0:data/openmohaa/maintt/pak1.pk3"};
    FILE                     *f;

    if (game == 0) return 1;
    f = fopen(pak[game], "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

/* A title, a list with one line chosen, and a hint, in the band. */
static void BootUI_ShowList(const char *title, const char *const *items, const int *shown, int n, int sel,
                            const char *hint)
{
    char key[1024];
    int  h, y, k, count = 0;

    snprintf(key, sizeof(key), "list|%s|%d|%s", title, sel, hint);
    if (!strcmp(key, s_lastKey)) return;
    snprintf(s_lastKey, sizeof(s_lastKey), "%s", key);

    for (k = 0; k < n; k++) count += shown[k];
    h = s_lineH + 10 + count * s_lineH + 10 + s_lineH;
    y = FB_H - 18 - h;
    if (y - 18 < BAND_Y) y = BAND_Y + 18;

    memcpy(s_fb + BAND_Y * FB_W, s_band, BAND_H * FB_W * 4);
    for (int i = (y - 18) * FB_W; i < FB_H * FB_W; i++) {
        const uint32_t p = s_fb[i];
        s_fb[i] = 0xFF000000u | ((((p >> 16) & 0xFF) * 3 / 10) << 16) | ((((p >> 8) & 0xFF) * 3 / 10) << 8)
                | ((p & 0xFF) * 3 / 10);
    }
    BootUI_Text((FB_W - BootUI_TextWidth(title)) / 2, y, title, 0xFFFFFF);
    y += s_lineH + 10;
    for (k = 0; k < n; k++) {
        char line[128];
        if (!shown[k]) continue;
        snprintf(line, sizeof(line), k == sel ? "> %s <" : "%s", items[k]);
        BootUI_Text((FB_W - BootUI_TextWidth(line)) / 2, y, line, k == sel ? 0x40C8F0 : 0xA0A0A0);
        y += s_lineH;
    }
    BootUI_Text((FB_W - BootUI_TextWidth(hint)) / 2, y + 10, hint, 0xB0B0B0);
    sceDisplayWaitVblankStart();
    memcpy(s_screen + BAND_Y * FB_W, s_fb + BAND_Y * FB_W, BAND_H * FB_W * 4);
}

void Sys_VitaBootChooseGame(uint32_t *framebuffer)
{
    static const char *const names[3] = {"Allied Assault", "Spearhead", "Breakthrough"};
    int                      shown[3], sel = 0, idle = 0, k, wait = 1;
    char                     lang[16], unused[8];
    unsigned int             prev;
    FILE                    *f;

    if (BootUI_Exists(CHOOSE_FLAG)) {
        remove(CHOOSE_FLAG);
        wait = 0; /* asked for from the game: no choice taken by itself */
    }
    for (k = 0; k < 3; k++) shown[k] = BootUI_HasGame(k);
    if (!shown[1] && !shown[2]) return; /* the base game only: nothing to choose */

    f = fopen(GAME_CHOICE_FILE, "r");
    if (f) {
        if (fscanf(f, "%d", &sel) != 1 || sel < 0 || sel > 2 || !shown[sel]) sel = 0;
        fclose(f);
    }
    s_targetGame = sel;

    BootUI_Settings("vita_language", unused, sizeof(unused), "", lang, sizeof(lang));
    if (!BootUI_Begin(framebuffer, lang)) return;

    prev = BootUI_Buttons();
    k    = sel;
    for (;;) {
        unsigned int now, pressed;

        BootUI_ShowList(BootUI_Tr("Choose the campaign"), names, shown, 3, sel,
                        BootUI_Tr("Up/Down: choose   X: start"));
        now     = BootUI_Buttons();
        pressed = now & ~prev;
        prev    = now;
        if (pressed & SCE_CTRL_CROSS) break;
        if (pressed & (SCE_CTRL_UP | SCE_CTRL_DOWN)) {
            const int step = (pressed & SCE_CTRL_UP) ? 2 : 1; /* +2 = -1 modulo 3 */
            do {
                sel = (sel + step) % 3;
            } while (!shown[sel]);
            idle = 0;
        }
        if (now) idle = 0;
        if (wait && (idle += 16) >= GAME_CHOICE_WAIT_MS) break; /* nobody looking: the last choice */
        sceKernelDelayThread(16 * 1000);
    }

    {
        /* for the log (boot.log is not open yet): ux0:data/openmohaa/vita_game_log.txt */
        FILE *log = fopen("ux0:data/openmohaa/vita_game_log.txt", "w");
        if (log) {
            fprintf(log, "installed %d%d%d, remembered %d, chosen %d by %s%s\n", shown[0], shown[1], shown[2], k, sel,
                    idle >= GAME_CHOICE_WAIT_MS ? "time" : "button", wait ? "" : " (asked from the game)");
            fclose(log);
        }
    }
    s_targetGame = sel;
    f = fopen(GAME_CHOICE_FILE, "w");
    if (f) {
        fprintf(f, "%d\n", sel);
        fclose(f);
    }
    BootUI_End();
}
