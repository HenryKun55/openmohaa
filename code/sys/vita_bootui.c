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
static void BootUI_Show(const char *title, const char *detail, float progress, const char *hint)
{
    static char last[1024];
    char        key[1024];
    int         h = 0, y;

    /* drawn again only when something changes (the bar: when it grows by a pixel) */
    snprintf(key, sizeof(key), "%s|%s|%d|%s", title ? title : "", detail ? detail : "",
             progress < 0.0f ? -1 : (int)((FB_W - 240) * (progress > 1.0f ? 1.0f : progress)), hint ? hint : "");
    if (!strcmp(key, last)) return;
    snprintf(last, sizeof(last), "%s", key);

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

// ---------- the check ----------

void Sys_VitaBootUpdateCheck(uint32_t *framebuffer)
{
    char             *cfg;
    char              enabled[8], lang[16];
    vitaUpdateState_t st;

    if (!framebuffer) return;
    cfg = BootUI_ReadFile("ux0:data/openmohaa/main/configs/omconfig.cfg");
    BootUI_ConfigValue(cfg, "vita_update_check", enabled, sizeof(enabled), "1");
    BootUI_ConfigValue(cfg, "vita_language", lang, sizeof(lang), "");
    free(cfg);
    if (atoi(enabled) == 0) return;
    if (!lang[0]) snprintf(lang, sizeof(lang), "%s", Sys_DefaultTextLanguage());
    if (!BootUI_LoadFont()) return;

    s_screen = framebuffer;
    s_fb     = malloc(FB_H * FB_W * 4);
    s_band   = malloc(BAND_H * FB_W * 4);
    if (!s_fb || !s_band) {
        free(s_fb);
        free(s_band);
        s_fb = s_band = NULL;
        return;
    }
    memcpy(s_band, s_screen + BAND_Y * FB_W, BAND_H * FB_W * 4);
    if (strcmp(lang, "en")) {
        char path[64];
        snprintf(path, sizeof(path), "app0:main/vita/lang/%s.txt", lang);
        s_lang = BootUI_ReadFile(path);
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

    sceDisplayWaitVblankStart();
    memcpy(s_screen + BAND_Y * FB_W, s_band, BAND_H * FB_W * 4); /* the picture as it was */
    free(s_band);
    free(s_fb);
    s_band = NULL;
    s_fb   = NULL;
    free(s_lang);
    s_lang = NULL;
    free(s_bits);
    s_bits = NULL;
}
