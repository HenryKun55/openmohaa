/*
===========================================================================
OpenMoHAA — PS Vita renderer Phase 1: World BSP VBO.

This file is built ONLY on Vita (#ifdef __vita__). On every other
platform the symbols below are absent and nothing changes.

The goal is to upload the static world BSP triangle soup to a vitaGL
vertex buffer once at level load, so the per-frame copy from
client-side tess buffers to vitaGL's legacy_pool can be skipped at
draw time. The Mac sample profile showed that copy dominating
set2d (~50ms of a ~80ms frame). VBOs replace the per-frame copy with
a one-time upload at level load.

Phase 1a (this file, today): allocate + upload the VBO. Print
diagnostic. DO NOT touch the existing draw path yet — so the cvar
gate `r_vita_vbo_world` doesn't actually change behaviour until
Phase 1b wires the bind into the draw path.

This split keeps the risk surface small: if the upload is broken,
the worst case is a wasted N MB of VRAM. The draw path is still the
same proven code.
===========================================================================
*/

#ifdef __vita__

#include "tr_local.h"

/* vitaGL is statically linked on Vita — gl* symbols resolve directly.
 * Declare the VBO entry points we need; vitaGL implements all of them.
 * Engine's qgl* indirection is only for SDL_GL_GetProcAddress paths,
 * which don't apply here.  Use plain GLuint/GLenum/etc. via GL types. */
extern void glGenBuffers( int n, unsigned int *buffers );
extern void glBindBuffer( unsigned int target, unsigned int buffer );
extern void glBufferData( unsigned int target, long size, const void *data, unsigned int usage );
extern void glDeleteBuffers( int n, const unsigned int *buffers );

/* For Phase 1b draw: re-issue the vertex/normal/texcoord/color
 * pointers as VBO offsets when the world VBO is bound. */
extern void glVertexPointer        ( int size, unsigned int type, int stride, const void *ptr );
extern void glNormalPointer        ( unsigned int type, int stride, const void *ptr );
extern void glTexCoordPointer      ( int size, unsigned int type, int stride, const void *ptr );
extern void glColorPointer         ( int size, unsigned int type, int stride, const void *ptr );
extern void glClientActiveTexture  ( unsigned int texture );
extern void glDrawElements         ( unsigned int mode, int count, unsigned int type, const void *indices );

#ifndef GL_TEXTURE0
#define GL_TEXTURE0              0x84C0
#endif
#ifndef GL_TEXTURE1
#define GL_TEXTURE1              0x84C1
#endif

#ifndef GL_ARRAY_BUFFER
#define GL_ARRAY_BUFFER          0x8892
#endif
#ifndef GL_ELEMENT_ARRAY_BUFFER
#define GL_ELEMENT_ARRAY_BUFFER  0x8893
#endif
#ifndef GL_STATIC_DRAW
#define GL_STATIC_DRAW           0x88E4
#endif
#ifndef GL_FLOAT
#define GL_FLOAT                 0x1406
#endif
#ifndef GL_UNSIGNED_BYTE
#define GL_UNSIGNED_BYTE         0x1401
#endif
#ifndef GL_UNSIGNED_SHORT
#define GL_UNSIGNED_SHORT        0x1403
#endif
#ifndef GL_UNSIGNED_INT
#define GL_UNSIGNED_INT          0x1405
#endif
#ifndef GL_TRIANGLES
#define GL_TRIANGLES             0x0004
#endif

/* ---- Module state ----
 *
 * One vertex buffer holding every SF_TRIANGLES surface's drawVert_t
 * array, concatenated. One index buffer with the per-surface index
 * lists rebased onto the concatenated vertex stream.
 *
 * Per-surface offsets live in worldVboSurf[] indexed by msurface_t's
 * index in tr.world->surfaces[]. Surfaces that are not eligible
 * (sky, billboard, terrain, grid, etc.) get vertOffset == -1.
 */
/* vitaWorldVboSurf_t is declared in tr_local.h so the draw path can
 * see it. Per-surface offsets: vertOffset = first drawVert_t in VBO
 * (-1 if not eligible), indexOffset = first uint32 in IBO. */

static unsigned int        worldVboId      = 0;
static unsigned int        worldIboId      = 0;
static vitaWorldVboSurf_t *worldVboSurf    = NULL;
static int                 worldVboSurfCount = 0;
static int                 worldVboTotalVerts = 0;
static int                 worldVboTotalIndexes = 0;
static qboolean            worldVboBuilt    = qfalse;

/*
====================
R_VitaWorldVBO_Free

Called from RE_LoadWorldMap before a new map's data is built, and from
shutdown. Safe to call when nothing has been built yet.
====================
*/
void R_VitaWorldVBO_Free(void)
{
    if (worldVboId) {
        glDeleteBuffers(1, &worldVboId);
        worldVboId = 0;
    }
    if (worldIboId) {
        glDeleteBuffers(1, &worldIboId);
        worldIboId = 0;
    }
    if (worldVboSurf) {
        ri.Free(worldVboSurf);
        worldVboSurf = NULL;
    }
    worldVboSurfCount   = 0;
    worldVboTotalVerts  = 0;
    worldVboTotalIndexes = 0;
    worldVboBuilt       = qfalse;
}

/*
====================
R_VitaWorldVBO_Build

Walk every world surface, concatenate SF_TRIANGLES vertex + index
data into two flat buffers, upload to vitaGL VBO/IBO. Save the
per-surface offsets so the draw path (Phase 1b, not in this commit
yet) can bind + offset.

SF_TRIANGLES is the only type we handle today. SF_FACE is already
routed to SF_TRIANGLES on Vita via the prior `vita: route BSP brush
faces through SF_TRIANGLES` fix, so that covers the dominant world
geometry. SF_GRID needs a separate path because it owns a
sub-divided patch mesh — Phase 1.5 or later.
====================
*/
void R_VitaWorldVBO_Build(void)
{
    int               i;
    msurface_t       *surf;
    int               accumVerts   = 0;
    int               accumIndexes = 0;
    int               eligibleSurfaces = 0;
    drawVert_t       *vertBuf      = NULL;
    unsigned short   *indexBuf     = NULL;

    R_VitaWorldVBO_Free();

    if (!r_vita_vbo_world || !r_vita_vbo_world->integer) {
        return;
    }
    if (!tr.world || tr.world->numsurfaces <= 0) {
        return;
    }

    worldVboSurfCount = tr.world->numsurfaces;
    worldVboSurf      = (vitaWorldVboSurf_t *)
        ri.Hunk_AllocateTempMemory(sizeof(vitaWorldVboSurf_t) * worldVboSurfCount);

    /* Pass 1: tally + record offsets. Also stamp the parallel
     * vitaVboSurfIdx field inside srfTriangles_t so the draw path
     * (RB_SurfaceTriangles, R_DrawElements) can look up its VBO
     * range in O(1) without searching the world surface list. */
    for (i = 0; i < worldVboSurfCount; i++) {
        surf = &tr.world->surfaces[i];
        worldVboSurf[i].vertOffset  = -1;
        worldVboSurf[i].indexOffset = -1;
        worldVboSurf[i].numVerts    = 0;
        worldVboSurf[i].numIndexes  = 0;

        if (!surf->data || *surf->data != SF_TRIANGLES) {
            continue;
        }
        srfTriangles_t *tri = (srfTriangles_t *)surf->data;
        tri->vitaVboSurfIdx = -1;
        if (tri->numVerts <= 0 || tri->numIndexes <= 0) {
            continue;
        }
        /* Sky / portal-sky surfaces draw through their own path
         * (RB_SurfaceSky, tcGen sky, depth tricks). On Vita SF_FACE is
         * routed to SF_TRIANGLES, so sky brushes land here too — but
         * pushing them through the static world VBO renders garbage
         * (the "céu cagado" on m1l1). Leave them out: vitaVboSurfIdx
         * stays -1 so RB_SurfaceTriangles falls back to the normal
         * draw for them. Walls (the bulk) still go through the VBO. */
        if (surf->shader && (surf->shader->isSky || surf->shader->isPortalSky)) {
            continue;
        }

        worldVboSurf[i].vertOffset  = accumVerts;
        worldVboSurf[i].numVerts    = tri->numVerts;
        worldVboSurf[i].indexOffset = accumIndexes;
        worldVboSurf[i].numIndexes  = tri->numIndexes;
        tri->vitaVboSurfIdx         = i;

        accumVerts   += tri->numVerts;
        accumIndexes += tri->numIndexes;
        eligibleSurfaces++;
    }

    if (eligibleSurfaces == 0 || accumVerts == 0) {
        ri.Printf(PRINT_ALL, "[VITA-VBO] no eligible SF_TRIANGLES world surfaces, skipping\n");
        ri.Hunk_FreeTempMemory(worldVboSurf);
        worldVboSurf      = NULL;
        worldVboSurfCount = 0;
        return;
    }

    /* Vita's vitaGL is most stable with 16-bit indices
     * (SCE_GXM_INDEX_FORMAT_U16). 32-bit was the cause of the
     * "walls broken even with dual-TMU bind" symptom — vitaGL was
     * misinterpreting the index stream. Fall back if the concatenated
     * vertex pool exceeds 65535 entries. */
    if (accumVerts > 65535) {
        ri.Printf(PRINT_WARNING,
            "[VITA-VBO] %d verts exceeds u16 limit (65535) — VBO disabled this level\n",
            accumVerts);
        for (i = 0; i < tr.world->numsurfaces; i++) {
            msurface_t *s = &tr.world->surfaces[i];
            if (s->data && *s->data == SF_TRIANGLES) {
                ((srfTriangles_t *)s->data)->vitaVboSurfIdx = -1;
            }
        }
        ri.Hunk_FreeTempMemory(worldVboSurf);
        worldVboSurf      = NULL;
        worldVboSurfCount = 0;
        return;
    }

    /* Pass 2: pack into flat buffers, rebase indices onto the
     * concatenated vertex stream. Indices stored as u16. */
    vertBuf  = (drawVert_t *)    ri.Hunk_AllocateTempMemory(sizeof(drawVert_t)    * accumVerts);
    indexBuf = (unsigned short *)ri.Hunk_AllocateTempMemory(sizeof(unsigned short) * accumIndexes);

    for (i = 0; i < worldVboSurfCount; i++) {
        if (worldVboSurf[i].vertOffset < 0) continue;

        srfTriangles_t *tri = (srfTriangles_t *)tr.world->surfaces[i].data;
        int             vo  = worldVboSurf[i].vertOffset;
        int             io  = worldVboSurf[i].indexOffset;
        int             j;

        Com_Memcpy(vertBuf + vo, tri->verts, sizeof(drawVert_t) * tri->numVerts);
        for (j = 0; j < tri->numIndexes; j++) {
            indexBuf[io + j] = (unsigned short)(vo + tri->indexes[j]);
        }
    }

    /* Upload to vitaGL. */
    glGenBuffers(1, &worldVboId);
    glBindBuffer(GL_ARRAY_BUFFER, worldVboId);
    glBufferData(GL_ARRAY_BUFFER,
                  (long)(sizeof(drawVert_t) * accumVerts),
                  vertBuf,
                  GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    glGenBuffers(1, &worldIboId);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, worldIboId);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                  (long)(sizeof(unsigned short) * accumIndexes),
                  indexBuf,
                  GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    worldVboTotalVerts   = accumVerts;
    worldVboTotalIndexes = accumIndexes;
    worldVboBuilt        = qtrue;

    ri.Printf(PRINT_ALL,
        "[VITA-VBO] built world VBO: %d surfaces, %d verts (%lu KB), %d indexes (%lu KB, u16)\n",
        eligibleSurfaces,
        accumVerts,   (unsigned long)(sizeof(drawVert_t)    * accumVerts)    / 1024,
        accumIndexes, (unsigned long)(sizeof(unsigned short) * accumIndexes) / 1024);

    ri.Hunk_FreeTempMemory(indexBuf);
    ri.Hunk_FreeTempMemory(vertBuf);
    /* worldVboSurf stays around — needed at draw time later. */
}

/*
====================
R_VitaWorldVBO_IsReady

True only if the cvar is set AND a VBO has been built for the current
level. Cheap check used in the draw hot path.
====================
*/
qboolean R_VitaWorldVBO_IsReady(void)
{
    if (!worldVboBuilt) return qfalse;
    if (!r_vita_vbo_world || !r_vita_vbo_world->integer) return qfalse;
    return qtrue;
}

/*
====================
R_VitaWorldVBO_LookupSurf

Given a world-resident srfTriangles_t's vitaVboSurfIdx, return its
per-surface VBO entry. Returns NULL if idx is out of range.
====================
*/
const vitaWorldVboSurf_t *R_VitaWorldVBO_LookupSurf(int idx)
{
    if (idx < 0 || idx >= worldVboSurfCount) return NULL;
    if (worldVboSurf[idx].vertOffset < 0)    return NULL;
    return &worldVboSurf[idx];
}

/*
====================
R_VitaWorldVBO_BindAndDraw

Bind the world VBO + IBO, set the interleaved attribute pointers to
the drawVert_t layout, fire a single qglDrawElements for the requested
index range, then unbind so the next non-VBO draw goes back through
client arrays.

The pointer offsets correspond to drawVert_t fields:
  xyz       offset 0   (vec3, 12 B)
  st        offset 12  (vec2, 8 B)        <- stage 0 base UVs
  lightmap  offset 20  (vec2, 8 B)        <- stage 1 lightmap UVs
                                             (engine sets stage's
                                             TexCoordPointer per pass)
  normal    offset 28  (vec3, 12 B)
  color     offset 40  (vec4 byte, 4 B)
  stride    44

For Phase 1b we only override xyz/normal/color/the BASE st here. The
shader stage iterator will re-call qglTexCoordPointer for st/lightmap
between stages — those calls also resolve against the bound VBO
because we leave it bound throughout.
====================
*/
void R_VitaWorldVBO_BindAndDraw(int firstIndex, int numIndexes)
{
    if (!worldVboBuilt) return;

    glBindBuffer(GL_ARRAY_BUFFER,         worldVboId);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, worldIboId);

    /* Pointers relative to bound VBO (offsets into drawVert_t).
     *
     * Q3's DrawMultitextured sets texcoord pointers PER TMU:
     *   TMU 0 (base)     → svars.texcoords[0]  (mirrors drawVert.st)
     *   TMU 1 (lightmap) → svars.texcoords[1]  (mirrors drawVert.lightmap)
     * Those CPU addresses become bogus VBO offsets when our VBO is
     * bound, which was the first-cut bug (walls glitched). We
     * override BOTH TMUs here with the correct drawVert_t offsets so
     * either single-pass multitexture (TMU0+TMU1 active together)
     * or multi-pass shaders (one TMU at a time) render correctly.
     *
     * Restoring TMU 0 active at the end matches DrawMultitextured's
     * convention so the engine's glState.currenttmu stays in sync. */
    glVertexPointer  (3, GL_FLOAT,         44, (const void *)(uintptr_t)0);
    glNormalPointer  (   GL_FLOAT,         44, (const void *)(uintptr_t)28);
    glColorPointer   (4, GL_UNSIGNED_BYTE, 44, (const void *)(uintptr_t)40);

    glClientActiveTexture(GL_TEXTURE1);
    glTexCoordPointer(2, GL_FLOAT,         44, (const void *)(uintptr_t)20); /* lightmap */
    glClientActiveTexture(GL_TEXTURE0);
    glTexCoordPointer(2, GL_FLOAT,         44, (const void *)(uintptr_t)12); /* base */

    glDrawElements(GL_TRIANGLES, numIndexes, GL_UNSIGNED_SHORT,
                   (const void *)(uintptr_t)(firstIndex * sizeof(unsigned short)));

    /* Unbind so other draws fall through to client arrays. */
    glBindBuffer(GL_ARRAY_BUFFER,         0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

#endif /* __vita__ */
