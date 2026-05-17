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

#ifndef GL_ARRAY_BUFFER
#define GL_ARRAY_BUFFER          0x8892
#endif
#ifndef GL_ELEMENT_ARRAY_BUFFER
#define GL_ELEMENT_ARRAY_BUFFER  0x8893
#endif
#ifndef GL_STATIC_DRAW
#define GL_STATIC_DRAW           0x88E4
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
typedef struct {
    int vertOffset;     /* index of first drawVert_t in the VBO, or -1 */
    int numVerts;
    int indexOffset;    /* index of first uint32 in the IBO */
    int numIndexes;
} vitaWorldVboSurf_t;

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
    int              *indexBuf     = NULL;

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

    /* Pass 1: tally + record offsets. */
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
        if (tri->numVerts <= 0 || tri->numIndexes <= 0) {
            continue;
        }

        worldVboSurf[i].vertOffset  = accumVerts;
        worldVboSurf[i].numVerts    = tri->numVerts;
        worldVboSurf[i].indexOffset = accumIndexes;
        worldVboSurf[i].numIndexes  = tri->numIndexes;

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

    /* Pass 2: pack into flat buffers, rebase indices onto the
     * concatenated vertex stream. */
    vertBuf  = (drawVert_t *)ri.Hunk_AllocateTempMemory(sizeof(drawVert_t) * accumVerts);
    indexBuf = (int *)       ri.Hunk_AllocateTempMemory(sizeof(int)        * accumIndexes);

    for (i = 0; i < worldVboSurfCount; i++) {
        if (worldVboSurf[i].vertOffset < 0) continue;

        srfTriangles_t *tri = (srfTriangles_t *)tr.world->surfaces[i].data;
        int             vo  = worldVboSurf[i].vertOffset;
        int             io  = worldVboSurf[i].indexOffset;
        int             j;

        Com_Memcpy(vertBuf + vo, tri->verts, sizeof(drawVert_t) * tri->numVerts);
        for (j = 0; j < tri->numIndexes; j++) {
            indexBuf[io + j] = vo + tri->indexes[j];
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
                  (long)(sizeof(int) * accumIndexes),
                  indexBuf,
                  GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    worldVboTotalVerts   = accumVerts;
    worldVboTotalIndexes = accumIndexes;
    worldVboBuilt        = qtrue;

    ri.Printf(PRINT_ALL,
        "[VITA-VBO] built world VBO: %d surfaces, %d verts (%lu KB), %d indexes (%lu KB)\n",
        eligibleSurfaces,
        accumVerts,   (unsigned long)(sizeof(drawVert_t) * accumVerts) / 1024,
        accumIndexes, (unsigned long)(sizeof(int)        * accumIndexes) / 1024);

    ri.Hunk_FreeTempMemory(indexBuf);
    ri.Hunk_FreeTempMemory(vertBuf);
    /* worldVboSurf stays around — needed at draw time later. */
}

#endif /* __vita__ */
