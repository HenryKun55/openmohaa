/*
===========================================================================
OpenMoHAA — PS Vita renderer Phase 2: GPU skeletal skinning.

Built ONLY on Vita (#ifdef __vita__).

Goal: move TIKI mesh skinning from CPU (RB_SkelMesh, ~23% of an m1l1
combat frame) to a vertex shader. CPU side per NPC surface drops to
"upload bone matrices + one indexed draw".

WHY THE PRIOR (2026-05-17) ATTEMPT FAILED, AND HOW THIS ONE DIFFERS:
The old code drew with the STANDARD GLES2 path — glUseProgram +
glEnableVertexAttribArray + glVertexAttribPointer. On vitaGL the generic
attribute slots ALIAS the fixed-function client arrays (slot 0 ==
GL_VERTEX_ARRAY, which the engine enables once at init and assumes stays
on). Toggling those slots around our draw corrupted the next fixed-
function surface → shattered heads / unlit walls. There was no slot
enable/disable combo that reconciled both pipelines.

THIS version uses vitaGL's OWN "legacy vgl* draw pipeline"
(VGL_EXT_gpu_objects_array + VGL_EXT_gxp_shaders):
  vglBindAttribLocation (once, at link) + vglVertexAttribPointer +
  vglIndexPointer + vglDrawObjects.
That pipeline is SEPARATE from both fixed-function and standard GLES2
client state — it never touches glEnableClientState/glVertexPointer, so
it cannot corrupt the engine's fixed-function arrays. No slot juggling.

Incremental bring-up (each step tested on device):
  STEP 1/2 (this file): skinned geometry + diffuse texture. NO lighting
           yet, so models look flat-lit but PROVE the math + the vgl*
           state isolation (neighbours must stay correct).
  STEP 3:  per-vertex model lighting so they're not flat.

Cvar r_vita_gpu_skinning gates everything (default 0).
===========================================================================
*/

#ifdef __vita__

#include "tr_local.h"
#include "../tiki/tiki_shared.h"

/* ---- GL entry points (vitaGL is statically linked; resolve directly) ---- */
extern unsigned int glCreateShader( unsigned int type );
extern void         glShaderSource( unsigned int shader, int count, const char * const *string, const int *length );
extern void         glCompileShader( unsigned int shader );
extern void         glGetShaderiv( unsigned int shader, unsigned int pname, int *params );
extern void         glGetShaderInfoLog( unsigned int shader, int bufSize, int *length, char *infoLog );
extern void         glDeleteShader( unsigned int shader );
extern unsigned int glCreateProgram( void );
extern void         glAttachShader( unsigned int program, unsigned int shader );
extern void         glLinkProgram( unsigned int program );
extern void         glGetProgramiv( unsigned int program, unsigned int pname, int *params );
extern void         glGetProgramInfoLog( unsigned int program, int bufSize, int *length, char *infoLog );
extern void         glDeleteProgram( unsigned int program );
extern void         glUseProgram( unsigned int program );
extern int          glGetUniformLocation( unsigned int program, const char *name );
extern void         glUniformMatrix4fv( int location, int count, unsigned char transpose, const float *value );
extern void         glUniform4fv( int location, int count, const float *value );
extern void         glUniform1i( int location, int v );
extern void         glGetFloatv( unsigned int pname, float *params );
extern void         glActiveTexture( unsigned int texture );
extern void         glBindTexture( unsigned int target, unsigned int texture );
extern unsigned int glGetError( void );
extern void         GL_Cull( int cullType );   /* engine-tracked face culling */

/* Diagnostic: log + CLEAR the GL error after a step the first time it
 * fires (so we pinpoint the failing call AND the engine's fatal
 * RE_BeginFrame glGetError check doesn't fire). One line per unique step. */
static int s_skin_err_traced = 0;
static int s_skin_draw_logged = 0;
/* glGetError is a GPU sync point on vitaGL (calling it per draw tanks FPS),
 * so only probe on the very FIRST surface — one-shot diagnosis, zero cost
 * afterwards. */
#define SKIN_GLCHK(step) do { \
        if (!s_skin_err_traced) { \
            unsigned int _e = glGetError(); \
            if (_e) ri.Printf(PRINT_ALL, "[VITA-SKIN] GLERR 0x%x after %s\n", _e, step); \
        } \
    } while (0)

/* vitaGL legacy vgl* shader-draw pipeline (separate from fixed-function). */
extern void vglBindAttribLocation( unsigned int prog, unsigned int index, const char *name, unsigned int num, unsigned int type );
extern void vglVertexAttribPointer( unsigned int index, int size, unsigned int type, unsigned char normalized, int stride, unsigned int count, const void *pointer );
extern void vglIndexPointer( unsigned int type, int stride, unsigned int count, const void *pointer );
extern void vglIndexPointerDefault( void );
extern void vglDrawObjects( unsigned int mode, int count, unsigned char implicit_wvp );

#ifndef GL_VERTEX_SHADER
#define GL_VERTEX_SHADER          0x8B31
#endif
#ifndef GL_FRAGMENT_SHADER
#define GL_FRAGMENT_SHADER        0x8B30
#endif
#ifndef GL_COMPILE_STATUS
#define GL_COMPILE_STATUS         0x8B81
#endif
#ifndef GL_LINK_STATUS
#define GL_LINK_STATUS            0x8B82
#endif
#ifndef GL_FLOAT
#define GL_FLOAT                  0x1406
#endif
#ifndef GL_UNSIGNED_SHORT
#define GL_UNSIGNED_SHORT         0x1403
#endif
#ifndef GL_UNSIGNED_INT
#define GL_UNSIGNED_INT           0x1405
#endif
#ifndef GL_TRIANGLES
#define GL_TRIANGLES              0x0004
#endif
#ifndef GL_TEXTURE0
#define GL_TEXTURE0               0x84C0
#endif
#ifndef GL_TEXTURE_2D
#define GL_TEXTURE_2D             0x0DE1
#endif
#ifndef GL_MODELVIEW_MATRIX
#define GL_MODELVIEW_MATRIX       0x0BA6
#endif
#ifndef GL_PROJECTION_MATRIX
#define GL_PROJECTION_MATRIX      0x0BA7
#endif

/*
 * Vertex shader: 4-weight matrix-palette skinning.
 *
 * Each skelWeight has its OWN local-space offset (the vertex position in
 * that bone's frame), so we feed FOUR offset+weight vec4s, not a single
 * a_position. a_idx holds the 4 bone-slot indices into the uniform palette.
 *
 * The bone matrix is uploaded TRANSPOSED (see DrawSurf): CPU skin math is
 * out[c] = dot(column_c, offset) + off[c]; uploading the columns as the
 * uniform's rows lets the shader use plain dot(row, p).
 */
static const char *s_skin_vert_src =
    "#version 100\n"
    "precision highp float;\n"
    "attribute vec4 a_w0;\n"        /* xyz = offset0, w = weight0 */
    "attribute vec4 a_w1;\n"
    "attribute vec4 a_w2;\n"
    "attribute vec4 a_w3;\n"
    "attribute vec4 a_idx;\n"       /* 4 bone-slot indices (as floats) */
    "attribute vec2 a_texcoord;\n"
    "uniform   mat4 u_mvp;\n"
    "uniform   vec4 u_boneMat[300];\n"   /* 100 slots * 3 rows */
    "uniform   vec4 u_boneOff[100];\n"
    "varying   vec2 v_texcoord;\n"
    "vec3 skinOne(int s, vec3 p) {\n"
    "    int o = s * 3;\n"
    "    return vec3(\n"
    "        dot(u_boneMat[o    ].xyz, p),\n"
    "        dot(u_boneMat[o + 1].xyz, p),\n"
    "        dot(u_boneMat[o + 2].xyz, p)) + u_boneOff[s].xyz;\n"
    "}\n"
    "void main(void) {\n"
    "    vec3 sk = a_w0.w * skinOne(int(a_idx.x), a_w0.xyz)\n"
    "            + a_w1.w * skinOne(int(a_idx.y), a_w1.xyz)\n"
    "            + a_w2.w * skinOne(int(a_idx.z), a_w2.xyz)\n"
    "            + a_w3.w * skinOne(int(a_idx.w), a_w3.xyz);\n"
    "    gl_Position = u_mvp * vec4(sk, 1.0);\n"
    "    v_texcoord  = a_texcoord;\n"
    "}\n";

static const char *s_skin_frag_src =
    "#version 100\n"
    "precision mediump float;\n"
    "uniform sampler2D u_diffuse;\n"
    "varying vec2     v_texcoord;\n"
    "void main(void) {\n"
    "    gl_FragColor = texture2D(u_diffuse, v_texcoord);\n"
    "}\n";

/* vgl* attribute indices — must match the vglBindAttribLocation calls. */
#define ATTR_W0       0
#define ATTR_W1       1
#define ATTR_W2       2
#define ATTR_W3       3
#define ATTR_IDX      4
#define ATTR_TEXCOORD 5

#define VITA_SKIN_MAX_WEIGHTS      4
#define VITA_SKIN_FLOATS_PER_VERT  22   /* 4*vec4 + vec4(idx) + vec2(uv) */
#define VITA_SKIN_BYTES_PER_VERT   (VITA_SKIN_FLOATS_PER_VERT * 4)
#define VITA_SKIN_MAX_BONESLOTS    100

#define VITA_SKIN_CACHE_CAP   1024
#define VITA_SKIN_HASH_SIZE   2048   /* power of two */

typedef struct {
    skelSurfaceGame_t *sf;          /* owner surface (validates stale idx)   */
    float             *vbuf;        /* persistent interleaved attribs (ri.Malloc) */
    unsigned int      *ibuf;        /* persistent index list (u32: vgl* pipeline) */
    int                numVerts;
    int                numIndexes;
    int                numBoneSlots;
    int                boneChannel[VITA_SKIN_MAX_BONESLOTS];
    qboolean           ineligible;  /* morphs / >4 weights → always CPU       */
} vitaSkinCacheEntry_t;

static vitaSkinCacheEntry_t s_skin_cache[VITA_SKIN_CACHE_CAP];
static int                  s_skin_cache_count = 0;

/* sf-pointer → cache slot, open-addressing hash. Avoids touching the
 * shared TIKI struct (ABI) and survives whatever the loader leaves in the
 * surface; rebuilt fresh each level via R_VitaGpuSkin_LevelReset. */
static skelSurfaceGame_t *s_hash_key[VITA_SKIN_HASH_SIZE];
static int                s_hash_slot[VITA_SKIN_HASH_SIZE];

static unsigned int        s_skin_program = 0;
static qboolean            s_skin_ready   = qfalse;
static int                 s_loc_mvp = -1, s_loc_boneMatrix = -1, s_loc_boneOffset = -1, s_loc_diffuse = -1;

cvar_t *r_vita_gpu_skinning = NULL;

/* TIKI bone cache + channel lookup (from RB_SkelMesh's path). */
extern skelBoneCache_t TIKI_Skel_Bones[];

/* ---------------------------------------------------------------- */
static unsigned int VitaSkin_CompileStage(unsigned int type, const char *src, const char *label)
{
    unsigned int sh = glCreateShader(type);
    int          ok = 0;
    glShaderSource(sh, 1, &src, NULL);
    glCompileShader(sh);
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        int  len = 0;
        glGetShaderInfoLog(sh, sizeof(log), &len, log);
        log[sizeof(log) - 1] = 0;
        ri.Printf(PRINT_WARNING, "[VITA-SKIN] %s compile FAILED:\n%s\n", label, log);
        glDeleteShader(sh);
        return 0;
    }
    ri.Printf(PRINT_ALL, "[VITA-SKIN] %s compile OK\n", label);
    return sh;
}

void R_VitaGpuSkin_Init(void)
{
    r_vita_gpu_skinning = ri.Cvar_Get("r_vita_gpu_skinning", "0", CVAR_ARCHIVE);

    if (!r_vita_gpu_skinning->integer) {
        return; /* off — don't compile */
    }
    if (s_skin_ready) {
        return;
    }

    unsigned int vs = VitaSkin_CompileStage(GL_VERTEX_SHADER,   s_skin_vert_src, "skin.vert");
    if (!vs) return;
    unsigned int fs = VitaSkin_CompileStage(GL_FRAGMENT_SHADER, s_skin_frag_src, "skin.frag");
    if (!fs) { glDeleteShader(vs); return; }

    unsigned int prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);

    /* Bind our attributes into vitaGL's vgl* draw pipeline BEFORE link. */
    vglBindAttribLocation(prog, ATTR_W0,       "a_w0",       4, GL_FLOAT);
    vglBindAttribLocation(prog, ATTR_W1,       "a_w1",       4, GL_FLOAT);
    vglBindAttribLocation(prog, ATTR_W2,       "a_w2",       4, GL_FLOAT);
    vglBindAttribLocation(prog, ATTR_W3,       "a_w3",       4, GL_FLOAT);
    vglBindAttribLocation(prog, ATTR_IDX,      "a_idx",      4, GL_FLOAT);
    vglBindAttribLocation(prog, ATTR_TEXCOORD, "a_texcoord", 2, GL_FLOAT);

    glLinkProgram(prog);

    int linked = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &linked);
    if (!linked) {
        char log[2048];
        int  len = 0;
        glGetProgramInfoLog(prog, sizeof(log), &len, log);
        log[sizeof(log) - 1] = 0;
        ri.Printf(PRINT_WARNING, "[VITA-SKIN] program link FAILED:\n%s\n", log);
        glDeleteProgram(prog);
        glDeleteShader(vs);
        glDeleteShader(fs);
        return;
    }
    glDeleteShader(vs);
    glDeleteShader(fs);

    s_skin_program   = prog;
    s_loc_mvp        = glGetUniformLocation(prog, "u_mvp");
    s_loc_boneMatrix = glGetUniformLocation(prog, "u_boneMat");
    s_loc_boneOffset = glGetUniformLocation(prog, "u_boneOff");
    s_loc_diffuse    = glGetUniformLocation(prog, "u_diffuse");
    s_skin_ready     = qtrue;

    ri.Printf(PRINT_ALL,
        "[VITA-SKIN] program LINK OK, prog=%u (vgl* pipeline) "
        "mvp=%d boneMat=%d boneOff=%d diffuse=%d\n",
        prog, s_loc_mvp, s_loc_boneMatrix, s_loc_boneOffset, s_loc_diffuse);
}

void R_VitaGpuSkin_Shutdown(void)
{
    R_VitaGpuSkin_LevelReset();
    if (s_skin_program) {
        glDeleteProgram(s_skin_program);
        s_skin_program = 0;
    }
    s_skin_ready = qfalse;
}

qboolean R_VitaGpuSkin_IsReady(void)
{
    return s_skin_ready;
}

/* Free all per-surface caches + clear the registry. Called at level load
 * (the TIKI surfaces are reloaded, so all cached buffers are stale). */
void R_VitaGpuSkin_LevelReset(void)
{
    int i;
    for (i = 0; i < s_skin_cache_count; i++) {
        if (s_skin_cache[i].vbuf) ri.Free(s_skin_cache[i].vbuf);
        if (s_skin_cache[i].ibuf) ri.Free(s_skin_cache[i].ibuf);
    }
    Com_Memset(s_skin_cache, 0, sizeof(s_skin_cache));
    s_skin_cache_count = 0;
    for (i = 0; i < VITA_SKIN_HASH_SIZE; i++) {
        s_hash_key[i]  = NULL;
        s_hash_slot[i] = 0;
    }
}

/* ---- sf → slot registry (open addressing) ---- */
static int VitaSkin_HashFind(skelSurfaceGame_t *sf)
{
    unsigned int h = ((unsigned int)(uintptr_t)sf >> 4) & (VITA_SKIN_HASH_SIZE - 1);
    int probes = 0;
    while (s_hash_key[h] != NULL && probes < VITA_SKIN_HASH_SIZE) {
        if (s_hash_key[h] == sf) return s_hash_slot[h]; /* slot, or -1 = ineligible */
        h = (h + 1) & (VITA_SKIN_HASH_SIZE - 1);
        probes++;
    }
    return 0; /* not seen yet */
}

static void VitaSkin_HashInsert(skelSurfaceGame_t *sf, int slot)
{
    unsigned int h = ((unsigned int)(uintptr_t)sf >> 4) & (VITA_SKIN_HASH_SIZE - 1);
    int probes = 0;
    while (s_hash_key[h] != NULL && probes < VITA_SKIN_HASH_SIZE) {
        h = (h + 1) & (VITA_SKIN_HASH_SIZE - 1);
        probes++;
    }
    s_hash_key[h]  = sf;
    s_hash_slot[h] = slot;
}

static int VitaSkin_LookupOrAddBoneSlot(vitaSkinCacheEntry_t *e, int channel)
{
    int i;
    for (i = 0; i < e->numBoneSlots; i++) {
        if (e->boneChannel[i] == channel) return i;
    }
    if (e->numBoneSlots >= VITA_SKIN_MAX_BONESLOTS) return -1;
    e->boneChannel[e->numBoneSlots] = channel;
    return e->numBoneSlots++;
}

/* Build the interleaved attribute + index buffers for a surface. Returns
 * the cache slot (>0) on success, or a negative ineligibility code. */
static int VitaSkin_BuildSurf(skelSurfaceGame_t *sf, skelHeaderGame_t *skelmodel)
{
    int i, j, slot;
    skeletorVertex_t *v;
    vitaSkinCacheEntry_t *entry;

    if (sf->numVerts <= 0 || sf->numTriangles <= 0) return -2;

    /* Eligibility: <=4 weights, no morph targets (face anim). */
    v = sf->pVerts;
    for (i = 0; i < sf->numVerts; i++) {
        if (v->numWeights > VITA_SKIN_MAX_WEIGHTS || v->numMorphs > 0) {
            return -4;
        }
        v = (skeletorVertex_t *)((byte *)v + sizeof(skeletorVertex_t)
                                  + sizeof(skelWeight_t) * v->numWeights);
    }

    if (s_skin_cache_count >= VITA_SKIN_CACHE_CAP) return -6;
    slot  = s_skin_cache_count++;
    entry = &s_skin_cache[slot];
    Com_Memset(entry, 0, sizeof(*entry));
    entry->sf = sf;

    /* Build per-vertex interleaved data into a temp, then DE-INDEX into the
     * persistent draw buffer. vitaGL's vglIndexPointer rejects both U16 and
     * U32 (GL_INVALID_ENUM) in this build, so we draw NON-indexed triangle
     * soup via vglIndexPointerDefault. */
    {
    int    numIdx = sf->numTriangles * 3;
    float *tmpv   = (float *)ri.Malloc(sizeof(float) * VITA_SKIN_FLOATS_PER_VERT * sf->numVerts);

    v = sf->pVerts;
    for (i = 0; i < sf->numVerts; i++) {
        skelWeight_t *w = (skelWeight_t *)((byte *)v + sizeof(skeletorVertex_t));
        float *out = tmpv + i * VITA_SKIN_FLOATS_PER_VERT;

        for (j = 0; j < VITA_SKIN_MAX_WEIGHTS; j++) {
            if (j < v->numWeights) {
                int channel = skelmodel->pBones[w[j].boneIndex].channel;
                int slotIdx = VitaSkin_LookupOrAddBoneSlot(entry, channel);
                if (slotIdx < 0) {
                    ri.Free(tmpv);
                    Com_Memset(entry, 0, sizeof(*entry));
                    s_skin_cache_count--;
                    return -7;
                }
                out[j * 4 + 0] = w[j].offset[0];
                out[j * 4 + 1] = w[j].offset[1];
                out[j * 4 + 2] = w[j].offset[2];
                out[j * 4 + 3] = w[j].boneWeight;
                out[16 + j]    = (float)slotIdx;   /* a_idx.x..w (offset 64B / 4) */
            } else {
                out[j * 4 + 0] = out[j * 4 + 1] = out[j * 4 + 2] = out[j * 4 + 3] = 0.0f;
                out[16 + j]    = 0.0f;
            }
        }
        out[20] = v->texCoords[0];   /* a_texcoord (offset 80B / 4) */
        out[21] = v->texCoords[1];

        v = (skeletorVertex_t *)((byte *)v + sizeof(skeletorVertex_t)
                                  + sizeof(skelWeight_t) * v->numWeights);
    }

    entry->vbuf = (float *)ri.Malloc(sizeof(float) * VITA_SKIN_FLOATS_PER_VERT * numIdx);
    for (i = 0; i < numIdx; i++) {
        Com_Memcpy(entry->vbuf + i * VITA_SKIN_FLOATS_PER_VERT,
                   tmpv + sf->pTriangles[i] * VITA_SKIN_FLOATS_PER_VERT,
                   sizeof(float) * VITA_SKIN_FLOATS_PER_VERT);
    }
    ri.Free(tmpv);
    entry->ibuf       = NULL;
    entry->numVerts   = numIdx;   /* triangle-soup draw count (de-indexed) */
    entry->numIndexes = numIdx;
    }

    ri.Printf(PRINT_DEVELOPER,
        "[VITA-SKIN] built slot %d '%s' (%d verts, %d tris, %d bones)\n",
        slot, sf->name[0] ? sf->name : "?", sf->numVerts, sf->numTriangles, entry->numBoneSlots);
    return slot;
}

static void VitaSkin_MatMul(float *out, const float *a, const float *b)
{
    int c, r, k;
    for (c = 0; c < 4; c++) {
        for (r = 0; r < 4; r++) {
            float s = 0.0f;
            for (k = 0; k < 4; k++) s += a[k * 4 + r] * b[c * 4 + k];
            out[c * 4 + r] = s;
        }
    }
}

/*
 * Try to draw a TIKI skeletal surface on the GPU. Returns qtrue if it
 * handled the draw (caller must then skip the CPU path), qfalse to fall
 * back to CPU (ineligible surface, missing bone channel, or not ready).
 */
qboolean R_VitaGpuSkin_DrawSurf(void *sfV, void *tikiV, void *skelmodelV, void *bonesV, float scale)
{
    skelSurfaceGame_t *sf        = (skelSurfaceGame_t *)sfV;
    dtiki_t           *tiki      = (dtiki_t *)tikiV;
    skelHeaderGame_t  *skelmodel = (skelHeaderGame_t *)skelmodelV;
    skelBoneCache_t   *bones     = (skelBoneCache_t *)bonesV;
    int slot, i;
    vitaSkinCacheEntry_t *e;
    float mvp[16];
    static float boneMatrixData[VITA_SKIN_MAX_BONESLOTS * 3 * 4];
    static float boneOffsetData[VITA_SKIN_MAX_BONESLOTS * 4];

    if (!s_skin_ready || !r_vita_gpu_skinning || !r_vita_gpu_skinning->integer) return qfalse;

    slot = VitaSkin_HashFind(sf);
    if (slot == 0) {
        /* First time seen — build (or mark ineligible) and register. */
        slot = VitaSkin_BuildSurf(sf, skelmodel);
        VitaSkin_HashInsert(sf, slot > 0 ? slot : -1);
    }
    if (slot <= 0) return qfalse;                     /* ineligible → CPU */
    if (slot >= s_skin_cache_count) return qfalse;    /* stale guard */
    e = &s_skin_cache[slot];
    if (e->sf != sf || !e->vbuf) return qfalse;       /* stale guard */

    /* Pack the bone matrix palette for THIS entity (transposed, see header). */
    for (i = 0; i < e->numBoneSlots; i++) {
        int localChn = ri.TIKI_GetLocalChannel(tiki, e->boneChannel[i]);
        if (localChn < 0) return qfalse;              /* channel absent → CPU */
        skelBoneCache_t *b = &bones[localChn];
        int base = i * 12;
        /* Pre-multiply by the model scale (tiki->load_scale * entity->scale).
         * The CPU path does VectorScale(out, scale, outXyz) on the final
         * skinned position; scaling each bone's rotation rows + translation
         * by `scale` yields skinned*scale identically. Without this the GPU
         * bodies are wrong-sized/displaced → CPU heads float off them. */
        boneMatrixData[base + 0] = b->matrix[0][0] * scale;
        boneMatrixData[base + 1] = b->matrix[1][0] * scale;
        boneMatrixData[base + 2] = b->matrix[2][0] * scale;
        boneMatrixData[base + 3] = 0.0f;
        boneMatrixData[base + 4] = b->matrix[0][1] * scale;
        boneMatrixData[base + 5] = b->matrix[1][1] * scale;
        boneMatrixData[base + 6] = b->matrix[2][1] * scale;
        boneMatrixData[base + 7] = 0.0f;
        boneMatrixData[base + 8] = b->matrix[0][2] * scale;
        boneMatrixData[base + 9] = b->matrix[1][2] * scale;
        boneMatrixData[base +10] = b->matrix[2][2] * scale;
        boneMatrixData[base +11] = 0.0f;
        boneOffsetData[i * 4 + 0] = b->offset[0] * scale;
        boneOffsetData[i * 4 + 1] = b->offset[1] * scale;
        boneOffsetData[i * 4 + 2] = b->offset[2] * scale;
        boneOffsetData[i * 4 + 3] = b->offset[3];
    }

    /* MVP from the engine's own matrices. (Do NOT use glGetFloatv with
     * GL_MODELVIEW_MATRIX / GL_PROJECTION_MATRIX — vitaGL/GLES2 rejects those
     * legacy queries with GL_INVALID_ENUM, which RE_BeginFrame treats as a
     * fatal error → app close. backEnd.ori.modelMatrix is the current
     * entity's modelview, loaded via qglLoadMatrixf before this draw.) */
    VitaSkin_MatMul(mvp, backEnd.viewParms.projectionMatrix, backEnd.ori.modelMatrix);

    if (!s_skin_draw_logged) {
        s_skin_draw_logged = 1;
        ri.Printf(PRINT_ALL,
            "[VITA-SKIN] draw '%s': drawCount=%d bones=%d scale=%f "
            "modelM[0]=%f modelM[12,13,14]=%f,%f,%f\n",
            sf->name[0] ? sf->name : "?", e->numVerts, e->numBoneSlots, scale,
            backEnd.ori.modelMatrix[0], backEnd.ori.modelMatrix[12],
            backEnd.ori.modelMatrix[13], backEnd.ori.modelMatrix[14]);
    }

    glGetError(); /* clear any pre-existing error so checks below are ours */

    glUseProgram(s_skin_program);                                    SKIN_GLCHK("glUseProgram");
    glUniformMatrix4fv(s_loc_mvp, 1, 0, mvp);                        SKIN_GLCHK("uniform mvp");
    glUniform4fv(s_loc_boneMatrix, e->numBoneSlots * 3, boneMatrixData); SKIN_GLCHK("uniform boneMat");
    glUniform4fv(s_loc_boneOffset, e->numBoneSlots,     boneOffsetData); SKIN_GLCHK("uniform boneOff");

    /* Diffuse texture from the current shader's stage 0. */
    glActiveTexture(GL_TEXTURE0);                                    SKIN_GLCHK("glActiveTexture");
    if (tess.shader && tess.xstages && tess.xstages[0]
        && tess.xstages[0]->bundle[0].image[0]) {
        glBindTexture(GL_TEXTURE_2D, tess.xstages[0]->bundle[0].image[0]->texnum);
    }                                                                SKIN_GLCHK("glBindTexture");
    glUniform1i(s_loc_diffuse, 0);                                   SKIN_GLCHK("uniform diffuse");

    /* vgl* pipeline: copy-in attributes + index list, then draw. This is
     * vitaGL's own batched shader path — it does NOT touch fixed-function
     * client state, so the next CPU surface (head/morph) stays intact.
     * STRIDE in floats for the byte stride. */
    /* Render two-sided for now: our GPU hook bypasses the engine's
     * GL_Cull(shader->cullType), so the leftover cull state could discard the
     * whole model. Two-sided removes culling as a cause of "invisible bodies".
     * (Engine re-sets cull for the next surface, so this doesn't leak.) */
    GL_Cull(CT_TWO_SIDED);

    {
        const int STRIDE = VITA_SKIN_BYTES_PER_VERT;
        const float *base = e->vbuf;
        vglVertexAttribPointer(ATTR_W0,       4, GL_FLOAT, 0, STRIDE, e->numVerts, base + 0);
        vglVertexAttribPointer(ATTR_W1,       4, GL_FLOAT, 0, STRIDE, e->numVerts, base + 4);
        vglVertexAttribPointer(ATTR_W2,       4, GL_FLOAT, 0, STRIDE, e->numVerts, base + 8);
        vglVertexAttribPointer(ATTR_W3,       4, GL_FLOAT, 0, STRIDE, e->numVerts, base + 12);
        vglVertexAttribPointer(ATTR_IDX,      4, GL_FLOAT, 0, STRIDE, e->numVerts, base + 16);
        vglVertexAttribPointer(ATTR_TEXCOORD, 2, GL_FLOAT, 0, STRIDE, e->numVerts, base + 20);
        SKIN_GLCHK("vglVertexAttribPointer x6");
        /* Non-indexed: progressive index list (0,1,2,...) over the de-indexed
         * triangle-soup buffer. Avoids vglIndexPointer (rejects U16/U32 here). */
        vglIndexPointerDefault();                                       SKIN_GLCHK("vglIndexPointerDefault");
        vglDrawObjects(GL_TRIANGLES, e->numVerts, 0 /* shader does mvp */); SKIN_GLCHK("vglDrawObjects");
    }

    glUseProgram(0);                                                 SKIN_GLCHK("glUseProgram(0)");
    /* One-shot trace done. From here NO glGetError runs in the skin path —
     * on vitaGL glGetError forces a full GPU sync (glFinish), and one per
     * surface tanked the frame to ~1 FPS. The path must therefore be 100%
     * GL-error-clean (the one-shot checks above verify that on surface #1).
     */
    s_skin_err_traced = 1;
    return qtrue;
}

#endif /* __vita__ */
