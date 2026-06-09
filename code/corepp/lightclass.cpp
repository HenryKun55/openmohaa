/*
===========================================================================
Copyright (C) 2023 the OpenMoHAA team

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

#include "lightclass.h"

#if defined(GAME_DLL)

#    include "../fgame/g_local.h"

#    define LIGHTCLASS_Printf  gi.Printf
#    define LIGHTCLASS_DPrintf gi.DPrintf
#    define LIGHTCLASS_Error   gi.Error

#elif defined(CGAME_DLL)

#    include "../cgame/cg_local.h"

#    define LIGHTCLASS_Printf  cgi.Printf
#    define LIGHTCLASS_DPrintf cgi.DPrintf
#    define LIGHTCLASS_Error   cgi.Error

#else

#    include "../qcommon/qcommon.h"

#    define LIGHTCLASS_Printf  Com_Printf
#    define LIGHTCLASS_DPrintf Com_DPrintf
#    define LIGHTCLASS_Error   Com_Error

#endif

size_t       totalmemallocated   = 0;
unsigned int numclassesallocated = 0;

void DisplayMemoryUsage()
{
    LIGHTCLASS_Printf("Classes %-5d Class memory used: %zu\n", numclassesallocated, totalmemallocated);
}

// On Switch the whole engine + game + cgame live in ONE statically-linked
// binary with symbol isolation. The size-prefix scheme below (operator new
// returns ptr+1, operator delete does ptr-1) is fragile there: if any delete
// resolves to the GLOBAL operator delete instead of this one, it frees ptr
// (not ptr-1) and corrupts the heap — which surfaced as non-deterministic
// crashes/hangs during entity spawn. Use the plain global new/delete so every
// allocation/free path is consistent.
#if !defined(_DEBUG_MEM) && !defined(__SWITCH__)
void *LightClass::operator new(size_t s)
{
    size_t *p;

    if (s == 0) {
        static void *empty_memory = nullptr;
        return &empty_memory;
    }

    s += sizeof(size_t);

#    ifdef GAME_DLL
    p = (size_t *)gi.Malloc(s);
#    elif defined(CGAME_DLL)
    p = (size_t *)cgi.Malloc(s);
#    else
    p = (size_t *)Z_Malloc(s);
#    endif

    totalmemallocated += s;
    numclassesallocated++;

    p++;

    return p;
}

void LightClass::operator delete(void *ptr)
{
    size_t *p = ((size_t *)ptr) - 1;

    totalmemallocated -= *p;
    numclassesallocated--;

#    ifdef GAME_DLL
    gi.Free(p);
#    elif defined(CGAME_DLL)
    cgi.Free(p);
#    else
    Z_Free(p);
#    endif
}
#else

void *LightClass::operator new(size_t s)
{
#ifdef __SWITCH__
    /* Heap-corruption hunt: 32-byte header (rounded size + front canary) and an
     * 8-byte trailer canary around every LightClass allocation. If the trailer
     * is clobbered when the object is freed, it overflowed its buffer — log the
     * size to pinpoint the corruptor on the libc heap. */
    size_t         sr   = (s + 7) & ~(size_t)7;
    unsigned char *base = (unsigned char *)::operator new(32 + sr + 8);
    *(size_t *)(base)                       = sr;
    *(unsigned long long *)(base + 8)        = 0xC0FFEE11C0FFEE11ULL;
    *(unsigned long long *)(base + 32 + sr)  = 0xDEADBEEFDEADBEEFULL;
    return base + 32;
#else
    return ::operator new(s);
#endif
}

void LightClass::operator delete(void *ptr)
{
#ifdef __SWITCH__
    unsigned char *base = (unsigned char *)ptr - 32;
    size_t         sr   = *(size_t *)(base);
    int front_ok = (*(unsigned long long *)(base + 8)       == 0xC0FFEE11C0FFEE11ULL);
    int back_ok  = (*(unsigned long long *)(base + 32 + sr) == 0xDEADBEEFDEADBEEFULL);
    if (!front_ok || !back_ok) {
#    if defined(GAME_DLL)
        gi.Printf("[CANARY] LightClass clobbered: size=%u front=%d back=%d\n",
                  (unsigned)sr, front_ok, back_ok);
#    elif defined(CGAME_DLL)
        cgi.Printf("[CANARY] LightClass clobbered: size=%u front=%d back=%d\n",
                   (unsigned)sr, front_ok, back_ok);
#    endif
    }
    ::operator delete(base);
#else
    ::operator delete(ptr);
#endif
}

#endif

void *LightClass::operator new(size_t size, void *placement)
{
    return placement;
}

void LightClass::operator delete(void *ptr, void *placement) {}

#ifdef __SWITCH__
#    include <stdlib.h>

// === Heap-overflow hunt (Switch) =========================================
// No other global operator new[]/delete[] override exists (win_bounds.cpp is
// compiled with DISABLE_BOUNDS), so wrap every ARRAY allocation — crucially the
// `new char[]` backing every str — with a front marker and a trailing canary.
// If a buffer's BACK canary is clobbered when it is freed, THAT buffer overran
// its bounds: it is the corruptor stomping the heap that makes the InitGame
// map-transition crash in _free_r / strdata::DelRef. Log its size + leading
// bytes so the real overflow can be identified and fixed.
#    define HEAPCAN_FRONT 0xA5C0FFEEA5C0FFEEULL
#    define HEAPCAN_BACK  0xBEEFCA11BEEFCA11ULL

void *operator new[](size_t s)
{
    size_t         sr   = (s + 7) & ~(size_t)7;
    unsigned char *base = (unsigned char *)malloc(32 + sr + 8);
    if (!base) {
        return (void *)0;
    }
    *(unsigned long long *)(base + 0)      = HEAPCAN_FRONT;
    *(size_t *)(base + 8)                  = s;
    *(size_t *)(base + 16)                 = sr;
    *(unsigned long long *)(base + 32 + sr) = HEAPCAN_BACK;
    return base + 32;
}

static void heapcan_free(void *ptr)
{
    if (!ptr) {
        return;
    }
    unsigned char *base = (unsigned char *)ptr - 32;
    if (*(unsigned long long *)(base + 0) != HEAPCAN_FRONT) {
        free(ptr); // not one of our canaried allocations — free as-is
        return;
    }
    size_t s  = *(size_t *)(base + 8);
    size_t sr = *(size_t *)(base + 16);
    if (*(unsigned long long *)(base + 32 + sr) != HEAPCAN_BACK) {
        const unsigned char *d = (const unsigned char *)ptr;
        LIGHTCLASS_Printf(
            "[HEAPCAN] OVERFLOW size=%u head=%02x%02x%02x%02x%02x%02x%02x%02x\n",
            (unsigned)s, d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7]);
    }
    free(base);
}

void operator delete[](void *ptr) noexcept
{
    heapcan_free(ptr);
}

void operator delete[](void *ptr, size_t) noexcept
{
    heapcan_free(ptr);
}
#endif
