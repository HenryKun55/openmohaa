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
  STEP 3:  per-vertex model lighting (RB_Light_Real in the vertex shader),
           linear farplane fog, alpha test, GL_State/GL_Bind parity, and
           copy-less GPU-mapped attribute/index buffers (vglAlloc +
           vgl*PointerMapped) so a draw no longer memcpys the mesh.

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
extern void vglVertexAttribPointerMapped( unsigned int index, const void *pointer );
extern void vglIndexPointerMapped( const void *pointer );
extern void *vglAlloc( unsigned int size, int type );
extern void vglFree( void *addr );
#define VGL_MEM_RAM_TYPE 1   /* vglMemType VGL_MEM_RAM: USER_RW RAM, GPU-mapped */

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
 * Vertex shader: 4-weight matrix-palette skinning + RB_Light_Real lighting.
 *
 * Each skelWeight has its OWN local-space offset (the vertex position in
 * that bone's frame), so we feed FOUR offset+weight vec4s, not a single
 * a_position. a_idx holds the 4 bone-slot indices into the uniform palette.
 *
 * The bone matrix is uploaded TRANSPOSED and pre-scaled (see DrawSurf): CPU
 * skin math is out[c] = dot(column_c, offset) + off[c]; uploading the columns
 * as the uniform's rows lets the shader use plain dot(row, p).
 *
 * Lighting mirrors RB_Light_Real (tr_sphere_shade.cpp) in model space: ambient
 * + up to VITA_SKIN_MAX_LIGHTS directional/spot/spot-fast/point lights, against
 * the normal rotated by the FIRST weight's bone like SkelVertGetNormal.
 * Colours stay in the CPU's 0..255 scale until the final clamp.
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
    "attribute vec3 a_normal;\n"    /* bind-pose normal (bone-0 frame) */
    "uniform   mat4 u_mvp;\n"
    "uniform   vec4 u_boneMat[300];\n"   /* 100 slots * 3 rows */
    "uniform   vec4 u_boneOff[100];\n"
    "uniform   vec4 u_mvZ;\n"            /* modelview row producing eye-space z */
    "uniform   vec4 u_fog;\n"            /* x = start, y = end, z = enabled */
    "uniform   vec4 u_ambient;\n"        /* rgb 0..255, a = alpha 0..1 */
    "uniform   vec4 u_lightInfo;\n"      /* x = numLights, y = fullbright */
    "uniform   vec4 u_lDir[8];\n"        /* xyz = direction, w = lighttype_t */
    "uniform   vec4 u_lOrg[8];\n"        /* xyz = origin, w = fSpotConst */
    "uniform   vec4 u_lCol[8];\n"        /* xyz = colour, w = fSpotScale */
    /* Two plain vec4 varyings (uv+fog, lit colour): a lone float / "colour" varying got a
     * different type across the vertex->fragment boundary after gxp translation, which
     * MoltenVK rejects at pipeline creation (Vita3K abort). */
    "varying   vec4 v_tc;\n"             /* xy = uv, z = fog factor */
    "varying   vec4 v_lit;\n"
    "vec3 rotOne(int s, vec3 p) {\n"
    "    int o = s * 3;\n"
    "    return vec3(dot(u_boneMat[o].xyz, p), dot(u_boneMat[o + 1].xyz, p), dot(u_boneMat[o + 2].xyz, p));\n"
    "}\n"
    "void main(void) {\n"
    "    int s0 = int(a_idx.x); int s1 = int(a_idx.y); int s2 = int(a_idx.z); int s3 = int(a_idx.w);\n"
    "    vec3 sk = a_w0.w * (rotOne(s0, a_w0.xyz) + u_boneOff[s0].xyz)\n"
    "            + a_w1.w * (rotOne(s1, a_w1.xyz) + u_boneOff[s1].xyz)\n"
    "            + a_w2.w * (rotOne(s2, a_w2.xyz) + u_boneOff[s2].xyz)\n"
    "            + a_w3.w * (rotOne(s3, a_w3.xyz) + u_boneOff[s3].xyz);\n"
    "    gl_Position = u_mvp * vec4(sk, 1.0);\n"
    "    v_tc.xy = a_texcoord;\n"
    "    vec3 n = normalize(rotOne(s0, a_normal));\n"
    "    vec3 c = vec3(0.0);\n"
    "    float a = 1.0;\n"
    "    if (u_lightInfo.y > 0.5) {\n"
    "        c = vec3(255.0);\n"
    "    } else if (u_lightInfo.x < 0.5) {\n"
    "        c = u_ambient.rgb; a = u_ambient.a;\n"
    "    } else {\n"
    "        for (int i = 0; i < 8; i++) {\n"
    "            if (float(i) >= u_lightInfo.x) break;\n"
    "            float t = u_lDir[i].w;\n"
    "            vec3  L = u_lDir[i].xyz;\n"
    "            if (t > 0.5 && t < 1.5) {\n"                /* LIGHT_DIRECTIONAL */
    "                float d = dot(L, n);\n"
    "                if (d > 0.0) c += d * u_lCol[i].xyz;\n"
    "            } else if (t > 1.5 && t < 2.5) {\n"         /* LIGHT_SPOT */
    "                float d = dot(L, n);\n"
    "                if (d > 0.0) {\n"
    "                    vec3 v = u_lOrg[i].xyz - sk;\n"
    "                    float pr = dot(v, L); pr *= pr;\n"
    "                    float d2 = dot(v, v);\n"
    "                    float mi = (u_lOrg[i].w - d2 / pr) * u_lCol[i].w;\n"
    "                    if (mi > 0.0) c += (d / d2) * min(mi, 1.0) * u_lCol[i].xyz;\n"
    "                }\n"
    "            } else if (t > 2.5) {\n"                    /* LIGHT_SPOT_FAST */
    "                float d = dot(L, n);\n"
    "                if (d > 0.0) { vec3 v = u_lOrg[i].xyz - sk; c += (d / dot(v, v)) * u_lCol[i].xyz; }\n"
    "            } else {\n"                                  /* LIGHT_POINT */
    "                vec3 v = u_lOrg[i].xyz - sk;\n"
    "                float d = dot(v, n);\n"
    "                if (d > 0.0) c += (d / dot(v, v)) * u_lCol[i].xyz;\n"
    "            }\n"
    "        }\n"
    "        c += u_ambient.rgb;\n"
    "    }\n"
    "    v_lit = vec4(clamp(c, 0.0, 255.0) * (1.0 / 255.0), a);\n"
    "    float ez = -dot(u_mvZ, vec4(sk, 1.0));\n"
    "    v_tc.z = u_fog.z > 0.5 ? clamp((u_fog.y - ez) / (u_fog.y - u_fog.x), 0.0, 1.0) : 1.0;\n"
    "    v_tc.w = 1.0;\n"
    "}\n";

static const char *s_skin_frag_src =
    "#version 100\n"
    "precision highp float;\n"
    "uniform sampler2D u_diffuse;\n"
    "uniform vec4     u_fogColor;\n"
    "uniform vec4     u_alphaTest;\n"    /* x: 0 off, 1 GT_0, 2 LT_80, 3 GE_80; y = entity alpha */
    "varying vec4     v_tc;\n"
    "varying vec4     v_lit;\n"
    "void main(void) {\n"
    "    vec4 c = texture2D(u_diffuse, v_tc.xy) * v_lit;\n"
    "    c.a *= u_alphaTest.y;\n"
    "    if (u_alphaTest.x > 0.5) {\n"
    "        if (u_alphaTest.x < 1.5) { if (c.a <= 0.0) discard; }\n"
    "        else if (u_alphaTest.x < 2.5) { if (c.a >= 0.5) discard; }\n"
    "        else { if (c.a < 0.5) discard; }\n"
    "    }\n"
    "    c.rgb = mix(u_fogColor.rgb, c.rgb, v_tc.z);\n"
    "    gl_FragColor = c;\n"
    "}\n";

/* vgl* attribute indices — must match the vglBindAttribLocation calls. */
#define ATTR_W0       0
#define ATTR_W1       1
#define ATTR_W2       2
#define ATTR_W3       3
#define ATTR_IDX      4
#define ATTR_TEXCOORD 5
#define ATTR_NORMAL   6
#define ATTR_COUNT    7

#define VITA_SKIN_MAX_WEIGHTS      4
#define VITA_SKIN_MAX_BONESLOTS    100
#define VITA_SKIN_MAX_LIGHTS       8

#define VITA_SKIN_CACHE_CAP   1024
#define VITA_SKIN_HASH_SIZE   2048   /* power of two */

/* Per-attribute component counts (all GL_FLOAT), in ATTR_* order. The mapped
 * (copy-less) vgl* path takes one tightly packed array per attribute. */
static const int s_attr_size[ATTR_COUNT] = { 4, 4, 4, 4, 4, 2, 3 };

typedef struct {
    skelSurfaceGame_t *sf;          /* owner surface (validates stale idx)   */
    float             *attr[ATTR_COUNT]; /* GPU-mapped (vglAlloc) per-attribute arrays */
    unsigned short    *ibuf;        /* GPU-mapped u16 index list             */
    int                numVerts;
    int                numIndexes;
    int                numBoneSlots;
    int                boneChannel[VITA_SKIN_MAX_BONESLOTS];
} vitaSkinCacheEntry_t;

static vitaSkinCacheEntry_t s_skin_cache[VITA_SKIN_CACHE_CAP];
/* Slot 0 is reserved: VitaSkin_HashFind returns 0 for "not seen yet". */
static int                  s_skin_cache_count = 1;

/* sf-pointer → cache slot, open-addressing hash. Avoids touching the
 * shared TIKI struct (ABI) and survives whatever the loader leaves in the
 * surface; rebuilt fresh each level via R_VitaGpuSkin_LevelReset. */
static skelSurfaceGame_t *s_hash_key[VITA_SKIN_HASH_SIZE];
static int                s_hash_slot[VITA_SKIN_HASH_SIZE];

static unsigned int        s_skin_program = 0;
static qboolean            s_skin_ready   = qfalse;
static int                 s_loc_mvp = -1, s_loc_boneMatrix = -1, s_loc_boneOffset = -1, s_loc_diffuse = -1;
static int                 s_loc_mvZ = -1, s_loc_fog = -1, s_loc_fogColor = -1, s_loc_alphaTest = -1;
static int                 s_loc_ambient = -1, s_loc_lightInfo = -1, s_loc_lDir = -1, s_loc_lOrg = -1, s_loc_lCol = -1;

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

    /* Always compile (cheap, once): the cvar only gates drawing, so the perf menu
     * can flip GPU skinning on/off live for A/B comparisons. */
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
    vglBindAttribLocation(prog, ATTR_NORMAL,   "a_normal",   3, GL_FLOAT);

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
    s_loc_mvZ        = glGetUniformLocation(prog, "u_mvZ");
    s_loc_fog        = glGetUniformLocation(prog, "u_fog");
    s_loc_fogColor   = glGetUniformLocation(prog, "u_fogColor");
    s_loc_alphaTest  = glGetUniformLocation(prog, "u_alphaTest");
    s_loc_ambient    = glGetUniformLocation(prog, "u_ambient");
    s_loc_lightInfo  = glGetUniformLocation(prog, "u_lightInfo");
    s_loc_lDir       = glGetUniformLocation(prog, "u_lDir");
    s_loc_lOrg       = glGetUniformLocation(prog, "u_lOrg");
    s_loc_lCol       = glGetUniformLocation(prog, "u_lCol");
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

static void VitaSkin_FreeEntry(vitaSkinCacheEntry_t *e)
{
    int a;
    for (a = 0; a < ATTR_COUNT; a++) {
        if (e->attr[a]) vglFree(e->attr[a]);
        e->attr[a] = NULL;
    }
    if (e->ibuf) vglFree(e->ibuf);
    e->ibuf = NULL;
}

/* Free all per-surface caches + clear the registry. Called at level load
 * (the TIKI surfaces are reloaded, so all cached buffers are stale). */
void R_VitaGpuSkin_LevelReset(void)
{
    int i;
    for (i = 1; i < s_skin_cache_count; i++) {
        VitaSkin_FreeEntry(&s_skin_cache[i]);
    }
    Com_Memset(s_skin_cache, 0, sizeof(s_skin_cache));
    s_skin_cache_count = 1;
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
    if (sf->numVerts > 65535) return -8;          /* u16 indices */
    slot  = s_skin_cache_count++;
    entry = &s_skin_cache[slot];
    Com_Memset(entry, 0, sizeof(*entry));
    entry->sf = sf;

    /* One tightly packed GPU-mapped array per attribute + a u16 index list, built
     * once per surface. vglVertexAttribPointerMapped / vglIndexPointerMapped then
     * draw straight from them with no per-draw copy (the old path re-copied an
     * interleaved, de-indexed buffer on every draw). */
    {
    int a, numIdx = sf->numTriangles * 3;
    for (a = 0; a < ATTR_COUNT; a++) {
        entry->attr[a] = (float *)vglAlloc(sizeof(float) * s_attr_size[a] * sf->numVerts, VGL_MEM_RAM_TYPE);
    }
    entry->ibuf = (unsigned short *)vglAlloc(sizeof(unsigned short) * numIdx, VGL_MEM_RAM_TYPE);
    for (a = 0; a < ATTR_COUNT; a++) {
        if (!entry->attr[a]) break;
    }
    if (a < ATTR_COUNT || !entry->ibuf) {
        VitaSkin_FreeEntry(entry);
        Com_Memset(entry, 0, sizeof(*entry));
        s_skin_cache_count--;
        return -9;                                /* out of GPU-mapped RAM → CPU */
    }

    v = sf->pVerts;
    for (i = 0; i < sf->numVerts; i++) {
        skelWeight_t *w = (skelWeight_t *)((byte *)v + sizeof(skeletorVertex_t));

        for (j = 0; j < VITA_SKIN_MAX_WEIGHTS; j++) {
            float *wo = entry->attr[ATTR_W0 + j] + i * 4;
            if (j < v->numWeights) {
                int channel = skelmodel->pBones[w[j].boneIndex].channel;
                int slotIdx = VitaSkin_LookupOrAddBoneSlot(entry, channel);
                if (slotIdx < 0) {
                    VitaSkin_FreeEntry(entry);
                    Com_Memset(entry, 0, sizeof(*entry));
                    s_skin_cache_count--;
                    return -7;
                }
                wo[0] = w[j].offset[0];
                wo[1] = w[j].offset[1];
                wo[2] = w[j].offset[2];
                wo[3] = w[j].boneWeight;
                entry->attr[ATTR_IDX][i * 4 + j] = (float)slotIdx;
            } else {
                wo[0] = wo[1] = wo[2] = wo[3] = 0.0f;
                entry->attr[ATTR_IDX][i * 4 + j] = 0.0f;
            }
        }
        entry->attr[ATTR_TEXCOORD][i * 2 + 0] = v->texCoords[0];
        entry->attr[ATTR_TEXCOORD][i * 2 + 1] = v->texCoords[1];
        entry->attr[ATTR_NORMAL][i * 3 + 0]   = v->normal[0];
        entry->attr[ATTR_NORMAL][i * 3 + 1]   = v->normal[1];
        entry->attr[ATTR_NORMAL][i * 3 + 2]   = v->normal[2];

        v = (skeletorVertex_t *)((byte *)v + sizeof(skeletorVertex_t)
                                  + sizeof(skelWeight_t) * v->numWeights);
    }

    for (i = 0; i < numIdx; i++) {
        entry->ibuf[i] = (unsigned short)sf->pTriangles[i];
    }
    entry->numVerts   = sf->numVerts;
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
 * Can this draw use the GPU path? Mirrors what the CPU path's stage iterator
 * would do for the current shader + lighting, and rejects anything the skin
 * shader doesn't implement (those surfaces fall back to RB_SkelMesh's CPU loop).
 */
static shaderStage_t *VitaSkin_EligibleStage(int *alphaTestMode, float *entityAlpha)
{
    shaderStage_t *st;
    unsigned int   atest;

    if (!tess.shader || !tess.xstages || tess.shader->numUnfoggedPasses != 1) return NULL;
    st = tess.xstages[0];
    if (!st || !st->active) return NULL;
    if (tess.xstages[1] && tess.xstages[1]->active) return NULL;   /* multi-stage */

    if (st->bundle[0].numImageAnimations > 1 || !st->bundle[0].image[0]) return NULL;
    if (st->bundle[0].tcGen != TCGEN_TEXTURE || st->bundle[0].numTexMods) return NULL;
    if (st->bundle[1].image[0]) return NULL;                       /* multitexture */

    /* rgbGen: only the spherical-lighting path that RB_Light_Real / fullbright feed. */
    if (st->rgbGen != CGEN_LIGHTING_SPHERICAL) return NULL;
    if (!r_drawspherelights->integer || !backEnd.currentSphere) return NULL;
    if (backEnd.currentSphere->TessFunction == RB_Light_Real) {
        if (backEnd.currentSphere->bUsesCubeMap) return NULL;
        if (backEnd.currentSphere->numRealLights > VITA_SKIN_MAX_LIGHTS) return NULL;
    } else if (backEnd.currentSphere->TessFunction != RB_Light_Fullbright) {
        return NULL;                                               /* grid / none → CPU */
    }

    switch (st->alphaGen) {
    case AGEN_IDENTITY:
    case AGEN_SKIP:
        *entityAlpha = 1.0f;
        break;
    case AGEN_ENTITY:
        *entityAlpha = backEnd.currentEntity->e.shaderRGBA[3] * (1.0f / 255.0f);
        break;
    default:
        return NULL;
    }

    atest = st->stateBits & GLS_ATEST_BITS;
    switch (atest) {
    case 0:               *alphaTestMode = 0; break;
    case GLS_ATEST_GT_0:  *alphaTestMode = 1; break;
    case GLS_ATEST_LT_80: *alphaTestMode = 2; break;
    case GLS_ATEST_GE_80: *alphaTestMode = 3; break;
    default:              return NULL;                             /* foliage tests → CPU */
    }
    return st;
}

/*
 * Try to draw a TIKI skeletal surface on the GPU. Returns qtrue if it
 * handled the draw (caller must then skip the CPU path), qfalse to fall
 * back to CPU (ineligible surface/shader, missing bone channel, or not ready).
 */
qboolean R_VitaGpuSkin_DrawSurf(void *sfV, void *tikiV, void *skelmodelV, void *bonesV, float scale)
{
    skelSurfaceGame_t *sf        = (skelSurfaceGame_t *)sfV;
    dtiki_t           *tiki      = (dtiki_t *)tikiV;
    skelHeaderGame_t  *skelmodel = (skelHeaderGame_t *)skelmodelV;
    skelBoneCache_t   *bones     = (skelBoneCache_t *)bonesV;
    int slot, i, a;
    vitaSkinCacheEntry_t *e;
    shaderStage_t *stage;
    int   alphaTestMode = 0;
    float entityAlpha   = 1.0f;
    float mvp[16], mvZ[4], fog[4], fogColor[4], alphaTest[4], ambient[4], lightInfo[4];
    static float boneMatrixData[VITA_SKIN_MAX_BONESLOTS * 3 * 4];
    static float boneOffsetData[VITA_SKIN_MAX_BONESLOTS * 4];
    static float lDir[VITA_SKIN_MAX_LIGHTS * 4], lOrg[VITA_SKIN_MAX_LIGHTS * 4], lCol[VITA_SKIN_MAX_LIGHTS * 4];

    if (!s_skin_ready || !r_vita_gpu_skinning || !r_vita_gpu_skinning->integer) return qfalse;

    stage = VitaSkin_EligibleStage(&alphaTestMode, &entityAlpha);
    if (!stage) return qfalse;

    slot = VitaSkin_HashFind(sf);
    if (slot == 0) {
        /* First time seen — build (or mark ineligible) and register. */
        slot = VitaSkin_BuildSurf(sf, skelmodel);
        VitaSkin_HashInsert(sf, slot > 0 ? slot : -1);
    }
    if (slot <= 0) return qfalse;                     /* ineligible → CPU */
    if (slot >= s_skin_cache_count) return qfalse;    /* stale guard */
    e = &s_skin_cache[slot];
    if (e->sf != sf || !e->ibuf) return qfalse;       /* stale guard */

    /* Pack the bone matrix palette for THIS entity (transposed, see header). */
    for (i = 0; i < e->numBoneSlots; i++) {
        int localChn = ri.TIKI_GetLocalChannel(tiki, e->boneChannel[i]);
        if (localChn < 0) return qfalse;              /* channel absent → CPU */
        skelBoneCache_t *b = &bones[localChn];
        int base = i * 12;
        /* Pre-multiply by the model scale (tiki->load_scale * entity->scale).
         * The CPU path does VectorScale(out, scale, outXyz) on the final
         * skinned position; scaling each bone's rotation rows + translation
         * by `scale` yields skinned*scale identically (the shader normalizes
         * the rotated normal, so the scale doesn't leak into lighting). */
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
    /* Eye-space z row of the (column-major) modelview, for linear fog. */
    mvZ[0] = backEnd.ori.modelMatrix[2];
    mvZ[1] = backEnd.ori.modelMatrix[6];
    mvZ[2] = backEnd.ori.modelMatrix[10];
    mvZ[3] = backEnd.ori.modelMatrix[14];

    /* Farplane fog, as GL_State + RB_SetupFog set it for fixed-function draws. */
    fog[0] = backEnd.viewParms.farplane_bias;
    fog[1] = backEnd.viewParms.farplane_distance;
    fog[2] = ((glState.externalSetState & GLS_FOG) && fog[1] > fog[0]) ? 1.0f : 0.0f;
    fog[3] = 0.0f;
    if (stage->stateBits & GLS_FOG_BLACK) {
        fogColor[0] = fogColor[1] = fogColor[2] = 0.0f;
    } else if (stage->stateBits & GLS_FOG_WHITE) {
        fogColor[0] = fogColor[1] = fogColor[2] = 1.0f;
    } else {
        fogColor[0] = glState.fFogColor[0];
        fogColor[1] = glState.fFogColor[1];
        fogColor[2] = glState.fFogColor[2];
    }
    fogColor[3] = 1.0f;

    alphaTest[0] = (float)alphaTestMode;
    alphaTest[1] = entityAlpha;
    alphaTest[2] = alphaTest[3] = 0.0f;

    /* Lighting, straight from the sphere RB_Light_Real would have used. */
    {
        const int fullbright = backEnd.currentSphere->TessFunction == RB_Light_Fullbright;
        const int numLights  = fullbright ? 0 : backEnd.currentSphere->numRealLights;
        ambient[0] = backEnd.currentSphere->ambient.level[0];
        ambient[1] = backEnd.currentSphere->ambient.level[1];
        ambient[2] = backEnd.currentSphere->ambient.level[2];
        ambient[3] = backEnd.currentSphere->ambient.level[3] * (1.0f / 255.0f);
        lightInfo[0] = (float)numLights;
        lightInfo[1] = fullbright ? 1.0f : 0.0f;
        lightInfo[2] = lightInfo[3] = 0.0f;
        for (i = 0; i < numLights; i++) {
            const reallightinfo_t *l = &backEnd.currentSphere->light[i];
            lDir[i * 4 + 0] = l->vDirection[0];
            lDir[i * 4 + 1] = l->vDirection[1];
            lDir[i * 4 + 2] = l->vDirection[2];
            lDir[i * 4 + 3] = (float)l->eType;
            lOrg[i * 4 + 0] = l->vOrigin[0];
            lOrg[i * 4 + 1] = l->vOrigin[1];
            lOrg[i * 4 + 2] = l->vOrigin[2];
            lOrg[i * 4 + 3] = l->fSpotConst;
            lCol[i * 4 + 0] = l->color[0];
            lCol[i * 4 + 1] = l->color[1];
            lCol[i * 4 + 2] = l->color[2];
            lCol[i * 4 + 3] = l->fSpotScale;
        }
    }

    if (!s_skin_draw_logged) {
        s_skin_draw_logged = 1;
        ri.Printf(PRINT_ALL,
            "[VITA-SKIN] draw '%s': verts=%d idx=%d bones=%d scale=%f lights=%d atest=%d fog=%d\n",
            sf->name[0] ? sf->name : "?", e->numVerts, e->numIndexes, e->numBoneSlots, scale,
            (int)lightInfo[0], alphaTestMode, (int)fog[2]);
    }

    /* Same GL state + texture binding the stage iterator would set, through the
     * engine's trackers (a raw glBindTexture desyncs GL_Bind's cache). */
    GL_State(stage->stateBits);
    GL_SelectTexture(0);
    GL_Bind(stage->bundle[0].image[0]);
    /* Two-sided: our GPU hook bypasses the engine's GL_Cull(shader->cullType),
     * and the leftover cull state could discard the whole model. */
    GL_Cull(CT_TWO_SIDED);

    if (!s_skin_err_traced) glGetError(); /* clear any pre-existing error so checks below are ours */

    glUseProgram(s_skin_program);                                    SKIN_GLCHK("glUseProgram");
    glUniformMatrix4fv(s_loc_mvp, 1, 0, mvp);                        SKIN_GLCHK("uniform mvp");
    glUniform4fv(s_loc_boneMatrix, e->numBoneSlots * 3, boneMatrixData); SKIN_GLCHK("uniform boneMat");
    glUniform4fv(s_loc_boneOffset, e->numBoneSlots,     boneOffsetData); SKIN_GLCHK("uniform boneOff");
    glUniform4fv(s_loc_mvZ, 1, mvZ);
    glUniform4fv(s_loc_fog, 1, fog);
    glUniform4fv(s_loc_fogColor, 1, fogColor);
    glUniform4fv(s_loc_alphaTest, 1, alphaTest);
    glUniform4fv(s_loc_ambient, 1, ambient);
    glUniform4fv(s_loc_lightInfo, 1, lightInfo);
    if (lightInfo[0] > 0.0f) {
        const int numLights = (int)lightInfo[0];
        glUniform4fv(s_loc_lDir, numLights, lDir);
        glUniform4fv(s_loc_lOrg, numLights, lOrg);
        glUniform4fv(s_loc_lCol, numLights, lCol);
    }                                                                SKIN_GLCHK("uniform lighting/fog");
    glUniform1i(s_loc_diffuse, 0);                                   SKIN_GLCHK("uniform diffuse");

    /* vgl* pipeline, copy-less: attributes and indices come straight from the
     * GPU-mapped arrays built once in VitaSkin_BuildSurf. It does NOT touch
     * fixed-function client state, so the next CPU surface stays intact. */
    for (a = 0; a < ATTR_COUNT; a++) {
        vglVertexAttribPointerMapped(a, e->attr[a]);
    }                                                                SKIN_GLCHK("vglVertexAttribPointerMapped");
    vglIndexPointerMapped(e->ibuf);                                  SKIN_GLCHK("vglIndexPointerMapped");
    vglDrawObjects(GL_TRIANGLES, e->numIndexes, 0 /* shader does mvp */); SKIN_GLCHK("vglDrawObjects");

    glUseProgram(0);                                                 SKIN_GLCHK("glUseProgram(0)");
    /* One-shot trace done. From here NO glGetError runs in the skin path —
     * on vitaGL glGetError forces a full GPU sync (glFinish), and one per
     * surface tanked the frame to ~1 FPS. The path must therefore be 100%
     * GL-error-clean (the one-shot checks above verify that on surface #1).
     */
    s_skin_err_traced = 1;
    backEnd.pc.c_drawElems++;
    return qtrue;
}

#endif /* __vita__ */
