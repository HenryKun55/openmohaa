/*
===========================================================================
Copyright (C) 2024 the OpenMoHAA team

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

// tr_font.cpp -- font rendering

#include "tr_local.h"

#define MAX_LOADED_FONTS 255

static fontheader_sgl_t s_loadedFonts_sgl[MAX_LOADED_FONTS];
static int s_numLoadedFonts_sgl = 0;
static fontheader_t s_loadedFonts[MAX_LOADED_FONTS];
static int s_numLoadedFonts = 0;
static float s_fontHeightScale = 1.0;
static float s_fontGeneralScale = 1.0;
static float s_fontZ = 0.0;

void R_ShutdownFont() {
    int i;
    fontheader_t *header;
    fontheader_sgl_t *header_sgl;

    for (i = 0; i < s_numLoadedFonts; i++)
    {
        header = &s_loadedFonts[i];
        if (header->charTable) {
            ri.Free(header->charTable);
            header->charTable = NULL;
        }

        memset(header, 0, sizeof(*header));
    }

    for (i = 0; i < s_numLoadedFonts_sgl; i++)
    {
        header_sgl = &s_loadedFonts_sgl[i];
        memset(header_sgl, 0, sizeof(*header_sgl));
    }
}

void R_SetFontHeightScale(float scale)
{
    s_fontHeightScale = scale;
}

void R_SetFontScale(float scale)
{
    s_fontGeneralScale = scale;
}

void R_SetFontZ(float zed)
{
    s_fontZ = zed;
}

static int CodeSearch(const fontheader_t* font, unsigned short uch) {
    int mid;
    int l, r;

    r = font->charTableLength;
    l = 0;
    while (l < r) {
        mid = (l + r) / 2;
        
        if (font->charTable[mid].cp > uch) {
            r = (l + r) / 2;
            continue;
        }

        if (uch == font->charTable[mid].cp) {
            return (l + r) / 2;
        }

        l = mid + 1;
    }

    if (uch != font->charTable[l].cp) {
        return -1;
    }

    return l;
}

static qboolean DBCSIsLeadByte(const fontheader_t* font, unsigned short uch) {
    // Byte ranges found in Wikipedia articles with relevant search strings in each case
    switch (font->codePage) {
    case 932:
        // Shift_jis
        return ((uch >= 0x81) && (uch <= 0x9F)) ||
            ((uch >= 0xE0) && (uch <= 0xFC));
        // Lead bytes F0 to FC may be a Microsoft addition.
    case 936:
        // GBK
        return (uch >= 0x81) && (uch <= 0xFE);
    case 949:
        // Korean Wansung KS C-5601-1987
        return (uch >= 0x81) && (uch <= 0xFE);
    case 950:
        // Big5
        return (uch >= 0x81) && (uch <= 0xFE);
    case 1361:
        // Korean Johab KS C-5601-1992
        return
            ((uch >= 0x84) && (uch <= 0xD3)) ||
            ((uch >= 0xD8) && (uch <= 0xDE)) ||
            ((uch >= 0xE0) && (uch <= 0xF9));
    }
    return false;
}

#if defined(__vita__) || defined(__SWITCH__)
/*
=================
R_FontAddAccents

The English game fonts only have ASCII glyphs, so translated text drew '?' for every
accented letter. Build the Latin-1 accented letters from the font's own glyphs (base
letter plus an accent made from ` ^ ~ . ,) into extra rows of the font page, at load
time from the player's own game data, so they keep the game's look.
=================
*/
enum { ACC_GRAVE, ACC_ACUTE, ACC_CIRC, ACC_TILDE, ACC_DIAER, ACC_CEDIL };

static const struct {
    unsigned char c, base, accent;
} s_composedChars[] = {
    { 0xC0, 'A', ACC_GRAVE }, { 0xC1, 'A', ACC_ACUTE }, { 0xC2, 'A', ACC_CIRC }, { 0xC3, 'A', ACC_TILDE },
    { 0xC4, 'A', ACC_DIAER }, { 0xC7, 'C', ACC_CEDIL }, { 0xC8, 'E', ACC_GRAVE }, { 0xC9, 'E', ACC_ACUTE },
    { 0xCA, 'E', ACC_CIRC },  { 0xCB, 'E', ACC_DIAER }, { 0xCC, 'I', ACC_GRAVE }, { 0xCD, 'I', ACC_ACUTE },
    { 0xCE, 'I', ACC_CIRC },  { 0xCF, 'I', ACC_DIAER }, { 0xD1, 'N', ACC_TILDE }, { 0xD2, 'O', ACC_GRAVE },
    { 0xD3, 'O', ACC_ACUTE }, { 0xD4, 'O', ACC_CIRC },  { 0xD5, 'O', ACC_TILDE }, { 0xD6, 'O', ACC_DIAER },
    { 0xD9, 'U', ACC_GRAVE }, { 0xDA, 'U', ACC_ACUTE }, { 0xDB, 'U', ACC_CIRC },  { 0xDC, 'U', ACC_DIAER },
    { 0xDD, 'Y', ACC_ACUTE }, { 0xE0, 'a', ACC_GRAVE }, { 0xE1, 'a', ACC_ACUTE }, { 0xE2, 'a', ACC_CIRC },
    { 0xE3, 'a', ACC_TILDE }, { 0xE4, 'a', ACC_DIAER }, { 0xE7, 'c', ACC_CEDIL }, { 0xE8, 'e', ACC_GRAVE },
    { 0xE9, 'e', ACC_ACUTE }, { 0xEA, 'e', ACC_CIRC },  { 0xEB, 'e', ACC_DIAER }, { 0xEC, 'i', ACC_GRAVE },
    { 0xED, 'i', ACC_ACUTE }, { 0xEE, 'i', ACC_CIRC },  { 0xEF, 'i', ACC_DIAER }, { 0xF1, 'n', ACC_TILDE },
    { 0xF2, 'o', ACC_GRAVE }, { 0xF3, 'o', ACC_ACUTE }, { 0xF4, 'o', ACC_CIRC },  { 0xF5, 'o', ACC_TILDE },
    { 0xF6, 'o', ACC_DIAER }, { 0xF9, 'u', ACC_GRAVE }, { 0xFA, 'u', ACC_ACUTE }, { 0xFB, 'u', ACC_CIRC },
    { 0xFC, 'u', ACC_DIAER }, { 0xFD, 'y', ACC_ACUTE }, { 0xFF, 'y', ACC_DIAER },
};

#define ACC_INK 40 // alpha above which a pixel counts as part of a glyph

typedef struct {
    int x, y, w, h; // cell in page pixels
} fontCell_t;

static qboolean FontCell(const fontheader_sgl_t *font, int c, int pageW, int pageH, fontCell_t *cell)
{
    int idx = font->indirection[c];

    if (idx < 0) {
        return qfalse;
    }
    // locations are fractions of the page
    cell->x = (int)(font->locations[idx].pos[0] * pageW + 0.5f);
    cell->y = (int)(font->locations[idx].pos[1] * pageH + 0.5f);
    cell->w = (int)(font->locations[idx].size[0] * pageW + 0.5f);
    cell->h = (int)(font->locations[idx].size[1] * pageH + 0.5f);
    return cell->w > 0 && cell->h > 0 && cell->x + cell->w <= pageW && cell->y + cell->h <= pageH;
}

// Ink bounding box of a cell (inclusive); false if the cell is empty.
static qboolean InkBox(const byte *pic, int pageW, const fontCell_t *cell, int *x0, int *y0, int *x1, int *y1)
{
    *x0 = cell->w;
    *y0 = cell->h;
    *x1 = -1;
    *y1 = -1;
    for (int y = 0; y < cell->h; y++) {
        for (int x = 0; x < cell->w; x++) {
            if (pic[((cell->y + y) * pageW + cell->x + x) * 4 + 3] > ACC_INK) {
                if (x < *x0) *x0 = x;
                if (x > *x1) *x1 = x;
                if (y < *y0) *y0 = y;
                if (y > *y1) *y1 = y;
            }
        }
    }
    return *x1 >= 0;
}

static void R_FontAddAccents(fontheader_sgl_t *font)
{
    shader_t      *shader = (shader_t *)font->shader;
    image_t       *img;
    byte          *pic = NULL, *out;
    int            pageW, pageH, newH, used = 0, i, n;
    fontCell_t     base, acc, dot;
    int            rowX, rowY, cellH;
    char           name[MAX_QPATH];
    int            origUsed;

    if (font->indirection[0xE9] != -1 || !shader || !shader->unfoggedStages[0]) {
        return; // the font already has accented letters
    }
    img = shader->unfoggedStages[0]->bundle[0].image[0];
    if (!img || !R_LoadRawImage(img->imgName, &pic, &pageW, &pageH)) {
        return;
    }
    if (!FontCell(font, 'e', pageW, pageH, &base) || !FontCell(font, '`', pageW, pageH, &acc)) {
        R_FreeRawImage(pic);
        return;
    }
    cellH = base.h;
    for (i = 0; i < 256; i++) {
        if (font->indirection[i] >= used) {
            used = font->indirection[i] + 1;
        }
    }
    origUsed = used;

    // Room for the new cells in extra rows below the page (power of two height).
    {
        int rows = 1, x = 0;
        for (i = 0; i < (int)ARRAY_LEN(s_composedChars); i++) {
            if (FontCell(font, s_composedChars[i].base, pageW, pageH, &base)) {
                if (x + base.w + 1 > pageW) {
                    rows++;
                    x = 0;
                }
                x += base.w + 1;
            }
        }
        for (newH = pageH; newH < pageH + rows * (cellH + 1); newH *= 2) {}
    }
    out = (byte *)ri.Malloc(pageW * newH * 4);
    memset(out, 0, pageW * newH * 4);
    memcpy(out, pic, pageW * pageH * 4);

    rowX = 0;
    rowY = pageH;
    for (n = 0; n < (int)ARRAY_LEN(s_composedChars) && used < 256; n++) {
        const int c = s_composedChars[n].c, accent = s_composedChars[n].accent;
        int       bx0, by0, bx1, by1, ax0, ay0, ax1, ay1;
        int       aw, ah, top, left, gap, x, y;
        byte     *cellPix;
        int       ok;

        if (font->indirection[c] != -1 || !FontCell(font, s_composedChars[n].base, pageW, pageH, &base)
            || !InkBox(pic, pageW, &base, &bx0, &by0, &bx1, &by1)) {
            continue;
        }
        ok = FontCell(font, accent == ACC_CIRC ? '^' : accent == ACC_TILDE ? '~' : accent == ACC_DIAER ? '.'
                            : accent == ACC_CEDIL ? ',' : '`', pageW, pageH, &acc)
          && InkBox(pic, pageW, &acc, &ax0, &ay0, &ax1, &ay1);
        if (!ok) {
            continue;
        }
        if (rowX + base.w + 1 > pageW) {
            rowX = 0;
            rowY += cellH + 1;
        }
        if (rowY + cellH > newH) {
            break;
        }

        // Base letter (a dotless i under an accent).
        cellPix = (byte *)ri.Malloc(base.w * cellH * 4);
        memset(cellPix, 0, base.w * cellH * 4);
        for (y = 0; y < base.h && y < cellH; y++) {
            memcpy(cellPix + y * base.w * 4, pic + ((base.y + y) * pageW + base.x) * 4, base.w * 4);
        }
        if (s_composedChars[n].base == 'i' && accent != ACC_CEDIL) {
            int gapRow = -1;
            for (y = by0; y <= by1 && gapRow < 0; y++) {
                qboolean empty = qtrue;
                for (x = 0; x < base.w; x++) {
                    if (cellPix[(y * base.w + x) * 4 + 3] > ACC_INK) empty = qfalse;
                }
                if (empty) gapRow = y;
            }
            if (gapRow > 0) {
                memset(cellPix, 0, gapRow * base.w * 4);
                for (by0 = gapRow; by0 < by1; by0++) {
                    qboolean ink = qfalse;
                    for (x = 0; x < base.w; x++) {
                        if (cellPix[(by0 * base.w + x) * 4 + 3] > ACC_INK) ink = qtrue;
                    }
                    if (ink) break;
                }
            }
        }

        aw  = ax1 - ax0 + 1;
        ah  = ay1 - ay0 + 1;
        gap = cellH >= 16 ? 2 : 1;
        if (accent == ACC_DIAER) {
            aw = aw * 2 + gap + 1;
        }
        left = (bx0 + bx1 + 1) / 2 - aw / 2;
        if (left < 0) left = 0;
        if (left + aw > base.w) left = base.w - aw > 0 ? base.w - aw : 0;

        if (accent == ACC_CEDIL) {
            top = by1 + 1 - ah / 3;
        } else {
            top = by0 - gap - ah;
            if (top < 0) {
                // A capital fills the cell: squeeze its ink down to make room.
                const int newTop = ah + gap, oldH = by1 - by0 + 1, newHgt = by1 - newTop + 1;
                byte     *tmp    = (byte *)ri.Malloc(base.w * cellH * 4);
                memcpy(tmp, cellPix, base.w * cellH * 4);
                memset(cellPix + by0 * base.w * 4, 0, (by1 - by0 + 1) * base.w * 4);
                for (y = 0; y < newHgt && newHgt > 0; y++) {
                    const int src = by0 + y * oldH / newHgt;
                    memcpy(cellPix + (newTop + y) * base.w * 4, tmp + src * base.w * 4, base.w * 4);
                }
                ri.Free(tmp);
                top = 0;
            }
        }

        // Accent ink, mirrored for acute, doubled for diaeresis.
        for (y = 0; y < ah; y++) {
            const int dy = top + y;
            if (dy < 0 || dy >= cellH) continue;
            for (x = 0; x < aw; x++) {
                int sx, dx = left + x;
                const byte *s;
                byte       *d;
                if (dx < 0 || dx >= base.w) continue;
                if (accent == ACC_DIAER) {
                    const int dw = ax1 - ax0 + 1;
                    if (x < dw) sx = x;
                    else if (x >= dw + gap + 1) sx = x - dw - gap - 1;
                    else continue;
                } else if (accent == ACC_ACUTE) {
                    sx = aw - 1 - x;
                } else {
                    sx = x;
                }
                s = pic + ((acc.y + ay0 + y) * pageW + acc.x + ax0 + sx) * 4;
                d = cellPix + (dy * base.w + dx) * 4;
                if (s[3] > d[3]) {
                    memcpy(d, s, 4);
                }
            }
        }

        for (y = 0; y < cellH; y++) {
            memcpy(out + ((rowY + y) * pageW + rowX) * 4, cellPix + y * base.w * 4, base.w * 4);
        }
        ri.Free(cellPix);

        font->locations[used].pos[0]  = (float)rowX / pageW;
        font->locations[used].pos[1]  = (float)rowY / newH;
        font->locations[used].size[0] = (float)base.w / pageW;
        font->locations[used].size[1] = (float)cellH / newH;
        font->indirection[c]          = used++;
        rowX += base.w + 1;
    }

    // Existing cells keep their pixels; only their share of the taller page changes.
    for (i = 0; i < origUsed; i++) {
        font->locations[i].pos[1] *= (float)pageH / newH;
        font->locations[i].size[1] *= (float)pageH / newH;
    }

    Com_sprintf(name, sizeof(name), "%s_accents", img->imgName);
    shader->unfoggedStages[0]->bundle[0].image[0] = R_CreateImageOld(
        name, out, pageW, newH, 0, 0, qfalse, qtrue, qtrue, 0, img->wrapClampModeX, img->wrapClampModeY
    );
    ri.Printf(PRINT_DEVELOPER, "Font %s: built accented letters (%d glyphs)\n", font->name, used);
    ri.Free(out);
    R_FreeRawImage(pic);
}
#endif

fontheader_sgl_t* R_LoadFont_sgl(const char* name)
{
    int i;
    char* theFile;
    fontheader_sgl_t* header;
    char* ref;
    const char* token;
    qboolean error;

    error = qfalse;

    for (i = 0; i < s_numLoadedFonts_sgl; i++)
    {
        header = &s_loadedFonts_sgl[i];
        if (!Q_stricmp(name, header->name)) {
            return header;
        }
    }

    if (s_numLoadedFonts >= MAX_LOADED_FONTS)
    {
        ri.Printf(PRINT_WARNING, "LoadFont: Too many fonts loaded!  Couldn't load %s\n", name);
        return NULL;
    }

    if (ri.FS_ReadFile(va("fonts/%s.RitualFont", name), (void**)&theFile) == -1)
    {
        ri.Printf(PRINT_WARNING, "LoadFont: Couldn't load font %s\n", name);
        return NULL;
    }

    header = &s_loadedFonts_sgl[s_numLoadedFonts_sgl];
    header->height = 0.0;
    header->aspectRatio = 0.0;
    Q_strncpyz(header->name, name, sizeof(header->name));

    ref = theFile;
    while (ref && !error)
    {
        token = COM_Parse(&ref);
        if (!Q_stricmp(token, "RitFont"))
        {
            // ignore this token
            continue;
        }

        if (!Q_stricmp(token, "indirections"))
        {
            token = COM_Parse(&ref);
            if (Q_stricmp(token, "{"))
            {
                error = qtrue;
                break;
            }

            for (i = 0; i < 256; i++)
            {
                token = COM_Parse(&ref);
                if (!token[0]) {
                    error = qtrue;
                    break;
                }

                header->indirection[i] = atoi(token);
            }

            if (error) {
                break;
            }

            token = COM_Parse(&ref);
            if (Q_stricmp(token, "}"))
            {
                error = qtrue;
                break;
            }
        }
        else if (!Q_stricmp(token, "locations"))
        {
            token = COM_Parse(&ref);
            if (Q_stricmp(token, "{"))
            {
                error = qtrue;
                break;
            }

            for (i = 0; i < 256; i++)
            {
                token = COM_Parse(&ref);
                if (Q_stricmp(token, "{")) {
                    error = qtrue;
                    break;
                }

                if (header->aspectRatio == 0.0)
                {
                    ri.Printf(PRINT_WARNING, "WARNING: aspect decl must be before locations in font '%s'", name);
                    break;
                }

                header->locations[i].pos[0] = atof(COM_Parse(&ref)) / 256.0;
                header->locations[i].pos[1] = atof(COM_Parse(&ref)) * header->aspectRatio / 256.0;
                header->locations[i].size[0] = atof(COM_Parse(&ref)) / 256.0;
                header->locations[i].size[1] = atof(COM_Parse(&ref)) * header->aspectRatio / 256.0;
            
                token = COM_Parse(&ref);
                if (Q_stricmp(token, "}"))
                {
                    error = qtrue;
                    break;
                }
            }

            if (error) {
                break;
            }

            token = COM_Parse(&ref);
            if (Q_stricmp(token, "}"))
            {
                error = qtrue;
                break;
            }
        }
        else if (!Q_stricmp(token, "height"))
        {
            // parse the header height
            token = COM_Parse(&ref);
            header->height = atof(token);
        }
        else if (!Q_stricmp(token, "aspect"))
        {
            // parse the aspect ratio
            token = COM_Parse(&ref);
            header->aspectRatio = atof(token);
        }
        else
        {
            // unknown token
            break;
        }
    }

    if (token[0])
    {
        ri.Printf(PRINT_WARNING, "WARNING: Unknown token '%s' parsing font '%s'\n", token, name);
        error = qtrue;
    }

    R_LoadFontShader(header);
    if (!header->height || !header->aspectRatio) {
        // invalid height or aspect ratio
        error = qtrue;
    }
#if defined(__vita__) || defined(__SWITCH__)
    if (!error) {
        R_FontAddAccents(header);
    }
#endif

    ri.FS_FreeFile(theFile);
    if (error)
    {
        ri.Printf(3, "WARNING: Error parsing font %s.\n", name);
        return NULL;
    }
    else
    {
        s_numLoadedFonts_sgl++;
        return header;
    }

    return NULL;
}

fontheader_t* R_LoadFont(const char* name) {
    int i;
    char* theFile;
    fontheader_t* header;
    char* ref;
    const char* token;
    qboolean error;
    char* pRitFontNames[32];

    error = qfalse;

    for (i = 0; i < s_numLoadedFonts; i++)
    {
        header = &s_loadedFonts[i];
        if (!Q_stricmp(name, header->name)) {
            return header;
        }
    }

    if (s_numLoadedFonts >= MAX_LOADED_FONTS)
    {
        ri.Printf(PRINT_WARNING, "LoadFont: Too many fonts loaded!  Couldn't load %s\n", name);
        return NULL;
    }

    if (ri.FS_ReadFile(va("fonts/%s.RitualFont", name), (void**)&theFile) == -1)
    {
        ri.Printf(PRINT_WARNING, "LoadFont: Couldn't load font %s\n", name);
        return NULL;
    }

    memset(pRitFontNames, 0, sizeof(pRitFontNames));
    ref = theFile;
    header = &s_loadedFonts[s_numLoadedFonts];

    token = COM_Parse(&ref);
    if (Q_stricmp(token, "RitFontList"))
    {
        if (Q_stricmp(token, "RitFont"))
        {
            ri.Printf(PRINT_WARNING, "LoadFont: Not actual font %s\n", name);
            return NULL;
        }
        header->numPages = 0;
    }
    else
    {
        while (ref && !error)
        {
            token = COM_Parse(&ref);
            if (!Q_stricmp(token, "CodePage"))
            {
                header->codePage = atoi(COM_Parse(&ref));
            }
            else if (!Q_stricmp(token, "Chars"))
            {
                header->charTableLength = atoi(COM_Parse(&ref));
                header->charTable = (fontchartable_t*)ri.Malloc(header->charTableLength * sizeof(fontchartable_t));
                if (!header->charTable)
                {
                    ri.Printf(PRINT_WARNING, "LoadFont: Couldn't alloc mem %s\n", name);
                    error = qtrue;
                    break;
                }
            }
            else if (!Q_stricmp(token, "Pages"))
            {
                header->numPages = atoi(COM_Parse(&ref));
            }
            else if (!Q_stricmp(token, "RitFontName"))
            {
                token = COM_Parse(&ref);
                if (Q_stricmp(token, "{"))
                {
                    ri.Printf(PRINT_WARNING, "LoadFont: Bad Format %s\n", name);
                    error = qtrue;
                    break;
                }

                for (i = 0; i < header->numPages; i++)
                {
                    token = COM_Parse(&ref);
                    if (!token[0])
                    {
                        ri.Printf(PRINT_WARNING, "LoadFont: Bad Token %s\n", name);
                        error = qtrue;
                        break;
                    }

                    pRitFontNames[i] = (char*)ri.Malloc(strlen(token) + 1);
                    if (!pRitFontNames[i])
                    {
                        ri.Printf(PRINT_WARNING, "LoadFont: Couldn't alloc mem %s\n", name);
                        error = qtrue;
                        break;
                    }

                    strcpy(pRitFontNames[i], token);
                }

                if (error) {
                    break;
                }

                token = COM_Parse(&ref);
                if (Q_stricmp(token, "}"))
                {
                    ri.Printf(PRINT_WARNING, "LoadFont: Bad Format %s\n", name);
                    error = qtrue;
                    break;
                }
            }
            else if (!Q_stricmp(token, "CharTable"))
            {
                token = COM_Parse(&ref);
                if (Q_stricmp(token, "{"))
                {
                    ri.Printf(PRINT_WARNING, "LoadFont: Bad Format %s\n", name);
                    error = qtrue;
                    break;
                }

                for (i = 0; i < header->charTableLength; i++) {
                    token = COM_Parse(&ref);
                    if (Q_stricmp(token, "{"))
                    {
                        ri.Printf(PRINT_WARNING, "LoadFont: Bad Token %s\n", name);
                        error = qtrue;
                        break;
                    }

                    header->charTable[i].cp = atoi(COM_Parse(&ref));
                    header->charTable[i].index = atoi(COM_Parse(&ref));
                    header->charTable[i].loc = atoi(COM_Parse(&ref));
                    atoi(COM_Parse(&ref));

                    token = COM_Parse(&ref);
                    if (Q_stricmp(token, "}"))
                    {
                        ri.Printf(PRINT_WARNING, "LoadFont: Bad Format %s\n", name);
                        error = qtrue;
                        break;
                    }
                }

                if (error) {
                    break;
                }

                token = COM_Parse(&ref);
                if (Q_stricmp(token, "}"))
                {
                    ri.Printf(PRINT_WARNING, "LoadFont: Bad Format %s\n", name);
                    error = qtrue;
                    break;
                }
            }
            else
            {
                ri.Printf(PRINT_WARNING, "LoadFont: Bad Token %s\n", name);
                error = qtrue;
                break;
            }
        }
    }

    ri.FS_FreeFile(theFile);

    if (!header->numPages)
    {
        header->charTableLength = 0;
        header->charTable = NULL;
        header->sgl[0] = R_LoadFont_sgl(name);
        if (!header->sgl[0])
        {
            ri.Printf(PRINT_WARNING, "LoadFont: failed %s\n", name);
            return NULL;
        }
    }
    else
    {
        for (i = 0; i < header->numPages; i++)
        {
            header->sgl[i] = R_LoadFont_sgl(pRitFontNames[i]);
            if (!header->sgl[i])
            {
                ri.Printf(PRINT_WARNING, "LoadFont: failed %s(%s)\n", pRitFontNames[i], name);
                error = qtrue;
                break;
            }
        }

        // Free all allocated strings
        for (i = 0; i < header->numPages; i++) {
            ri.Free(pRitFontNames[i]);
        }

        if (error)
        {
            if (header->charTable) {
                ri.Free(header->charTable);
            }

            header->numPages = 0;
            header->charTableLength = 0;
            header->charTable = NULL;
        }
    }

    strcpy(header->name, name);
    s_numLoadedFonts++;
    return header;
}

void R_LoadFontShader(fontheader_sgl_t* font)
{
    int i;
    int save;
    char filename[64];
    shader_t* fontshader;

    save = r_sequencenumber;
    r_sequencenumber = -1;
    Com_sprintf(filename, sizeof(filename), "gfx/fonts/%s", font->name);
    font->shader = R_FindShader(filename, -1, qfalse, qfalse, qfalse, qfalse);
    r_sequencenumber = save;

    if (!font->shader) {
        ri.Error(ERR_DROP, "Could not load font shader for %s\n", filename);
    }

    fontshader = (shader_t*)font->shader;
    if (fontshader->numUnfoggedPasses > 0)
    {
        for (i = 0; i < fontshader->numUnfoggedPasses; i++)
        {
            if (fontshader->unfoggedStages[0] != NULL && fontshader->unfoggedStages[0]->active)
            {
                fontshader->unfoggedStages[0]->rgbGen = CGEN_GLOBAL_COLOR;
                fontshader->unfoggedStages[0]->alphaGen = AGEN_GLOBAL_ALPHA;
            }
        }

        font->trhandle = r_sequencenumber;
    }
    else
    {
        font->trhandle = r_sequencenumber;
    }
}

// heightScale/generalScale/fontZ are passed in: the render thread draws queued strings
// with the values captured at queue time and must not touch the front end's globals.
static void R_DrawString_sgl_Impl(fontheader_sgl_t* font, const char* text, float x, float y, int maxlen, const float *pvVirtualScreen,
                                  float heightScale, float generalScale, float fontZ) {
    float charHeight;
    float startx, starty;
    int i;
    float fWidthScale, fHeightScale;

    i = 0;
    startx = x;
    starty = y;
    if (pvVirtualScreen) {
        if (pvVirtualScreen[0]) {
            fWidthScale = pvVirtualScreen[0];
        } else {
            fWidthScale = (double)glConfig.vidWidth / 640.0;
        }

        if (pvVirtualScreen[1]) {
            fHeightScale = pvVirtualScreen[1];
        } else {
            fHeightScale = (double)glConfig.vidHeight / 480.0;
        }
    }

    if (!font || !font->shader) {
        return;
    }

    charHeight = heightScale * font->height * generalScale;
    RB_BeginSurface((shader_t*)font->shader);

    for (i = 0; text[i]; i++) {
        unsigned char c;
        int indirected;
        letterloc_t* loc;

        c = text[i];

        if (maxlen != -1 && i >= maxlen) {
            break;
        }

        switch (c)
        {
        case '\t':
            indirected = font->indirection[32];
            if (indirected == -1) {
                ri.Printf(PRINT_DEVELOPER, "R_DrawString: no space-character in font!\n");
            } else {
                x = generalScale * font->locations[indirected].size[0] * 256.0 * 3.0 + x;
            }
            break;

        case '\n':
            starty = charHeight + starty;
            x = startx;
            y = starty;
            break;

        case '\r':
            x = startx;
            break;

        default:
            indirected = font->indirection[c];
            if (indirected == -1)
            {
                ri.Printf(PRINT_DEVELOPER, "R_DrawString: no 0x%02x-character in font!\n", c);
                indirected = font->indirection['?'];
                if (indirected == -1) {
                    ri.Printf(PRINT_DEVELOPER, "R_DrawString: no '?' character in font!\n");
                    break;
				}
                // set the indirection for the next time
				font->indirection[c] = indirected;
            }

            RB_CHECKOVERFLOW(4, 6);

            loc = &font->locations[indirected];

            // texture coordinates
            tess.texCoords[tess.numVertexes][0][0] = loc->pos[0];
            tess.texCoords[tess.numVertexes][0][1] = loc->pos[1];
            tess.texCoords[tess.numVertexes + 1][0][0] = loc->size[0] + loc->pos[0];
            tess.texCoords[tess.numVertexes + 1][0][1] = loc->pos[1];
            tess.texCoords[tess.numVertexes + 2][0][0] = loc->pos[0];
            tess.texCoords[tess.numVertexes + 2][0][1] = loc->size[1] + loc->pos[1];
            tess.texCoords[tess.numVertexes + 3][0][0] = loc->size[0] + loc->pos[0];
            tess.texCoords[tess.numVertexes + 3][0][1] = loc->size[1] + loc->pos[1];

            // vertices position
            tess.xyz[tess.numVertexes][0] = x;
            tess.xyz[tess.numVertexes][1] = y;
            tess.xyz[tess.numVertexes][2] = fontZ;
            tess.xyz[tess.numVertexes + 1][0] = x + generalScale * loc->size[0] * 256.0;
            tess.xyz[tess.numVertexes + 1][1] = y;
            tess.xyz[tess.numVertexes + 1][2] = fontZ;
            tess.xyz[tess.numVertexes + 2][0] = x;
            tess.xyz[tess.numVertexes + 2][1] = y + charHeight;
            tess.xyz[tess.numVertexes + 2][2] = fontZ;
            tess.xyz[tess.numVertexes + 3][0] = x + generalScale * loc->size[0] * 256.0;
            tess.xyz[tess.numVertexes + 3][1] = y + charHeight;
            tess.xyz[tess.numVertexes + 3][2] = fontZ;

            // indices
            tess.indexes[tess.numIndexes] = tess.numVertexes;
            tess.indexes[tess.numIndexes + 1] = tess.numVertexes + 1;
            tess.indexes[tess.numIndexes + 2] = tess.numVertexes + 2;
            tess.indexes[tess.numIndexes + 3] = tess.numVertexes + 1;
            tess.indexes[tess.numIndexes + 4] = tess.numVertexes + 3;
            tess.indexes[tess.numIndexes + 5] = tess.numVertexes + 2;

            if (pvVirtualScreen)
            {
                // scale the string properly if virtual screen
                tess.xyz[tess.numVertexes][0] *= fWidthScale;
                tess.xyz[tess.numVertexes][1] *= fHeightScale;
                tess.xyz[tess.numVertexes + 1][0] *= fWidthScale;
                tess.xyz[tess.numVertexes + 1][1] *= fHeightScale;
                tess.xyz[tess.numVertexes + 2][0] *= fWidthScale;
                tess.xyz[tess.numVertexes + 2][1] *= fHeightScale;
                tess.xyz[tess.numVertexes + 3][0] *= fWidthScale;
                tess.xyz[tess.numVertexes + 3][1] *= fHeightScale;
            }

            x += generalScale * loc->size[0] * 256.0;
            tess.numVertexes += 4;
            tess.numIndexes += 6;
            break;
        }
    }

    RB_EndSurface();
}

// Front end: make sure the font's shader is registered (registration stays out of the
// backend), then draw -- queued with R_QUEUE_2D, immediately otherwise.
static void R_DrawString_sgl(fontheader_sgl_t* font, const char* text, float x, float y, int maxlen, const float *pvVirtualScreen) {
    if (!font) {
        return;
    }

#ifndef R_QUEUE_2D
    R_IssuePendingRenderCommands();
#endif

    if (font->trhandle != r_sequencenumber) {
        font->shader = NULL;
    }

    if (!font->shader) {
        R_LoadFontShader(font);
    }

#ifdef R_QUEUE_2D
    {
        draw2DCommand_t *cmd;
        int              len = (int)strlen(text);

        if (maxlen >= 0 && maxlen < len) {
            len = maxlen;
        }
        cmd = R_Queue2DCommand(D2_STRING, len + 1);
        if (!cmd) {
            return;
        }
        Com_Memcpy(cmd + 1, text, len);
        ((char *)(cmd + 1))[len] = 0;
        cmd->ptr  = font;
        cmd->f[0] = x;
        cmd->f[1] = y;
        // The font scale/depth globals as they are now, not when the backend runs.
        cmd->f[2] = s_fontHeightScale;
        cmd->f[3] = s_fontGeneralScale;
        cmd->f[4] = s_fontZ;
        cmd->i[0] = pvVirtualScreen ? 1 : 0;
        if (pvVirtualScreen) {
            cmd->f[5] = pvVirtualScreen[0];
            cmd->f[6] = pvVirtualScreen[1];
        }
    }
#else
    R_DrawString_sgl_Impl(font, text, x, y, maxlen, pvVirtualScreen, s_fontHeightScale, s_fontGeneralScale, s_fontZ);
#endif
}

#ifdef R_QUEUE_2D
void R_DrawString_sgl_Exec(const draw2DCommand_t *cmd) {
    float vs[2];

    vs[0] = cmd->f[5];
    vs[1] = cmd->f[6];

    // The text was already cut to maxlen when queued.
    R_DrawString_sgl_Impl((fontheader_sgl_t *)cmd->ptr, (const char *)(cmd + 1), cmd->f[0], cmd->f[1], -1,
                          cmd->i[0] ? vs : NULL, cmd->f[2], cmd->f[3], cmd->f[4]);
}
#endif

void R_DrawString(fontheader_t* font, const char* text, float x, float y, int maxlen, const float *pvVirtualScreen) {
    int i;
    int code;
    unsigned short uch;
    char buffer[512];
    size_t buflen;
    int cursgl;
    float curX, curY;
    float curHeight;
    
    if (!font->numPages) {
        if (font->sgl[0]) {
            R_DrawString_sgl(font->sgl[0], text, x, y, maxlen, pvVirtualScreen);
        }
        return;
    }

    if (maxlen < 0) {
        maxlen = strlen(text);
    }
    
    curX = x;
    curY = y;
    curHeight = 0.f;
    cursgl = -1;
    buflen = 0;

    i = 0;
    while(i < maxlen) {
        fontchartable_t* ct;

        uch = text[i];
        i++;

        if (DBCSIsLeadByte(font, uch)) {
            uch = (uch << 8) | text[i];
            i++;
        }

        if (!uch) {
            break;
        }

        if (uch == '\n' || uch == '\r') {
            buffer[buflen] = 0;
            R_DrawString_sgl(font->sgl[cursgl], buffer, curX, curY, maxlen, pvVirtualScreen);
            
            curX = x;
            curHeight = 0.f;
            buflen = 0;
            if (uch == '\n') {
                curY += font->sgl[0]->height * s_fontGeneralScale * s_fontHeightScale;
            }

            continue;
        }

        code = CodeSearch(font, uch);
        if (code < 0) {
            continue;
        }

        if (cursgl == -1) {
            cursgl = font->charTable[code].index;
        }

        ct = &font->charTable[code];
        if (cursgl != ct->index || buflen >= ARRAY_LEN(buffer) - 2) {
            buffer[buflen] = 0;
            R_DrawString_sgl(font->sgl[cursgl], buffer, curX, curY, maxlen, pvVirtualScreen);

            curX += curHeight;
            curHeight = 0.f;
            buflen = 0;
            ct = &font->charTable[code];
            cursgl = ct->index;
        }

        buffer[buflen++] = ct->loc;
        curHeight += font->sgl[cursgl]->locations[ct->loc].size[0] * 256.0f;
    }

    if (buflen)
    {
        buffer[buflen] = 0;
        R_DrawString_sgl(font->sgl[cursgl], buffer, curX, y, buflen, pvVirtualScreen);
    }
}

void R_DrawFloatingString_sgl(fontheader_sgl_t* font, const char* text, const vec3_t org, const vec4_t color, float scale, int maxlen) {
    shader_t* fontshader;
    qhandle_t fsh;
    float charWidth, charHeight;
    int i;
    vec3_t pos;
    polyVert_t verts[4];

    if (!font) {
        return;
    }

    R_IssuePendingRenderCommands();
    if (font->trhandle != r_sequencenumber) {
        font->shader = NULL;
    }

    if (!font->shader) {
        R_LoadFontShader(font);
    }

    i = 0;
    fontshader = (shader_t*)font->shader;
    fsh = 0;

    for (i = 0; i < tr.numShaders; i++)
    {
        if (fontshader == tr.shaders[i])
        {
            fsh = i;
            break;
        }
    }

    i = 0;
    charHeight = font->height * s_fontHeightScale * s_fontGeneralScale * scale;
    VectorCopy(org, pos);

    for (i = 0; text[i]; i++) {
        unsigned char c;
        int indirected;
        letterloc_t* loc;
        
        c = text[i];
        indirected = font->indirection[c];
        if (indirected == -1)
        {
            ri.Printf(PRINT_ALL, "R_DrawFloatingString: no 0x%02x-character in font!\n", c);
            continue;
        }

        loc = &font->locations[indirected];

        // vertices color
        verts[0].modulate[0] = (int)(color[0] * 255.0);
        verts[0].modulate[1] = (int)(color[1] * 255.0);
        verts[0].modulate[2] = (int)(color[2] * 255.0);
        verts[0].modulate[3] = (int)(color[3] * 255.0);
        verts[1].modulate[0] = verts[0].modulate[0];
        verts[1].modulate[1] = verts[0].modulate[1];
        verts[1].modulate[2] = verts[0].modulate[2];
        verts[1].modulate[3] = verts[0].modulate[3];
        verts[2].modulate[0] = verts[0].modulate[0];
        verts[2].modulate[1] = verts[0].modulate[1];
        verts[2].modulate[2] = verts[0].modulate[2];
        verts[2].modulate[3] = verts[0].modulate[3];
        verts[3].modulate[0] = verts[0].modulate[0];
        verts[3].modulate[1] = verts[0].modulate[1];
        verts[3].modulate[2] = verts[0].modulate[2];
        verts[3].modulate[3] = verts[0].modulate[3];

        // texture coordinates
        verts[0].st[0] = loc->pos[0];
        verts[0].st[1] = loc->pos[1];
        verts[1].st[0] = loc->pos[0] + font->locations[indirected].size[0];
        verts[1].st[1] = loc->pos[1];
        verts[2].st[0] = verts[1].st[0];
        verts[2].st[1] = loc->pos[1] + loc->size[1];
        verts[3].st[0] = loc->pos[0];
        verts[3].st[1] = verts[2].st[1];
        VectorCopy(pos, verts[3].xyz);

        charWidth = font->locations[indirected].size[0] * 256.0 * s_fontGeneralScale * scale;
        verts[2].xyz[0] = pos[0] + tr.refdef.viewaxis[1][0] * -charWidth;
        verts[2].xyz[1] = pos[1] + tr.refdef.viewaxis[1][1] * -charWidth;
        verts[2].xyz[2] = pos[2] + tr.refdef.viewaxis[1][2] * -charWidth;
        verts[1].xyz[0] = verts[2].xyz[0] + charHeight * tr.refdef.viewaxis[2][0];
        verts[1].xyz[1] = verts[2].xyz[1] + charHeight * tr.refdef.viewaxis[2][1];
        verts[1].xyz[2] = verts[2].xyz[2] + charHeight * tr.refdef.viewaxis[2][2];
        verts[0].xyz[1] = verts[1].xyz[1] + tr.refdef.viewaxis[1][1] * charWidth;
        verts[0].xyz[2] = verts[1].xyz[2] + tr.refdef.viewaxis[1][2] * charWidth;
        verts[0].xyz[0] = verts[1].xyz[0] + tr.refdef.viewaxis[1][0] * charWidth;
    
        if (RE_AddPolyToScene(fsh, 4, verts, 0)) {
            ++tr.refdef.numPolys;
        }

        pos[0] = verts[2].xyz[0];
        pos[1] = verts[2].xyz[1];
        pos[2] = verts[2].xyz[2];
    }
}

void R_DrawFloatingString(fontheader_t* font, const char* text, const vec3_t org, const vec4_t color, float scale, int maxlen) {
    return R_DrawFloatingString_sgl(font->sgl[0], text, org, color, scale, maxlen);
}

float R_GetFontHeight(const fontheader_t* font)
{
    if (!font || !font->sgl[0]) {
        return 0.0;
    }

    return font->sgl[0]->height * s_fontGeneralScale * s_fontHeightScale;
}

float R_GetFontStringWidth_sgl(const fontheader_sgl_t* font, const char* s)
{
    float widths;
    int i;

    widths = 0.0;

    if (!font) {
        return 0.0;
    }

    for (i = 0; s[i]; i++)
    {
        int indirected;
        char c = *s;

        if (c == '\t')
        {
            indirected = font->indirection[32];
            if (indirected != -1) {
                widths += font->locations[indirected].size[0] * 3.0;
            } else {
                ri.Printf(PRINT_ALL, "R_GetFontStringWidth: no space-character in font!\n");
            }
        }
        else
        {
            indirected = font->indirection[c];
            if (indirected != -1) {
                widths += font->locations[indirected].size[0];
            } else {
                ri.Printf(PRINT_ALL, "R_GetFontStringWidth: no 0x%02x-character in font!\n", c);
            }
        }
    }

    return widths * s_fontGeneralScale * 256.0;
}

float R_GetFontStringWidth(const fontheader_t* font, const char* s)
{
    int i;
    int code;
    fontchartable_t* ct;
    float width = 0.f;

    if (!font->numPages) {
        return R_GetFontStringWidth_sgl(font->sgl[0], s);
    }

    i = 0;
    while(s[i]) {
        unsigned char uch = s[i++];

        if (DBCSIsLeadByte(font, uch)) {
            uch = (uch << 8) | s[i++];
            if (!uch) {
                break;
            }
        }

        if (uch == '\t') {
            code = CodeSearch(font, ' ');
            if (code >= 0) {
                ct = &font->charTable[code];
                width += font->sgl[ct->index]->locations[ct->loc].size[0] * 3.f;
            }
        }
        else {
            code = CodeSearch(font, uch);
            if (code >= 0) {
                ct = &font->charTable[code];
                width += font->sgl[ct->index]->locations[ct->loc].size[0];
            }
        }
    }

    return width;
}
