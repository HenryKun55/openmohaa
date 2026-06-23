/*
===========================================================================
Copyright (C) 2025 the OpenMoHAA team

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

#ifdef USE_INTERNAL_SDL_HEADERS
#    include "SDL.h"
#else
#    include <SDL.h>
#endif

#include "../qcommon/qcommon.h"

static SDL_Cursor *cursor = NULL;
static SDL_Surface *cursor_surface = NULL;
static byte *cursor_image_data = NULL;
static pCursorFree cursor_free = NULL;

void IN_GetMousePosition(int *x, int *y) {
    SDL_GetMouseState(x, y);
}

qboolean IN_SetCursorFromImage(const byte *pic, int width, int height, pCursorFree cursorFreeFn) {
    IN_FreeCursor();

#if defined(__vita__) || defined(__SWITCH__)
    /* Vita/Switch have NO OS hardware cursor -- the engine draws the UI cursor
     * itself (see IN_IsCursorActive below). Building an SDL color cursor here is
     * not just useless, it CRASHES: SDL_CreateColorCursor blits the source
     * surface, and on a level transition the UI re-pushes its menus (which calls
     * refreshCursor) while the renderer's raw-image system is being torn down and
     * rebuilt -- so the cursor pixels are freed/unmapped and SDL's blit walks off
     * into bad memory, data-aborting inside Blit_RGB565_ABGR8888. (Root-caused
     * from the Vita coredump on the m1l2a->m1l2b transition with vita-parse-core.)
     * Skip the SDL cursor entirely; just release the caller's pixels so the raw
     * image doesn't leak. */
    if (pic && cursorFreeFn) {
        cursorFreeFn((byte *)pic);
    }
    return qtrue;
#else
    cursor_surface = SDL_CreateRGBSurfaceWithFormatFrom(pic, width, height, 32, 4 * width, SDL_PIXELFORMAT_ABGR8888);
    if (!cursor_surface) {
        return qfalse;
    }

    cursor = SDL_CreateColorCursor(cursor_surface, 0, 0);
    SDL_SetCursor(cursor);

    return qtrue;
#endif
}

void IN_FreeCursor() {
    if (cursor) {
        SDL_FreeCursor(cursor);
    }
    if (cursor_surface) {
        SDL_FreeSurface(cursor_surface);
    }
    if (cursor_image_data) {
        cursor_free(cursor_image_data);
        cursor_image_data = NULL;
    }
}

qboolean IN_IsCursorActive()
{
#if defined(__vita__) || defined(__SWITCH__)
    /* Upstream returns true only when relative mouse mode is on, because
     * on desktop that implies the OS cursor is hidden and the engine has
     * to draw its own. We disable relative mode on Vita/Switch so the
     * stick-driven motion delivers absolute coordinates, and neither has an
     * OS cursor — the engine must always draw the UI cursor when active. */
    extern qboolean in_guimouse;
    return in_guimouse;
#else
    return SDL_GetRelativeMouseMode() == SDL_TRUE;
#endif
}
