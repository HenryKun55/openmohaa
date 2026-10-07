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
extern void         glGetIntegerv( unsigned int pname, int *params );
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
extern void vglSetSemanticBindingMode( unsigned int mode );
#define VGL_MODE_SHADER_PAIR_ENUM 0   /* vglSemanticMode: SHADER_PAIR, GLOBAL, POSTPONED */
#define VGL_MODE_POSTPONED_ENUM   2
extern void vglVertexAttribPointerMapped( unsigned int index, const void *pointer );
extern void vglIndexPointerMapped( const void *pointer );
extern void *vglAlloc( unsigned int size, int type );
extern void vglFree( void *addr );
#define VGL_MEM_RAM_TYPE 1   /* vglMemType VGL_MEM_RAM: USER_RW RAM, GPU-mapped */
#define VGL_MEM_VRAM_TYPE 0  /* VGL_MEM_VRAM: CDRAM */
#define VGL_MEM_SLOW_TYPE 2  /* VGL_MEM_SLOW: PHYCONT_USER_RW RAM */

/* Vertex and index data written once and then only read by the GPU: CDRAM first (the
 * GPU's own memory, and where the space is: on the device vitaGL's RAM pool is used up
 * by the time a level is loaded, "vitaGL free: vram 29 MB, ram 0 MB, phycont 26 MB",
 * and every character fell back to CPU skinning), then phycont, then RAM. */
static void *VitaSkin_GpuAlloc(unsigned int size)
{
    void *p = vglAlloc(size, VGL_MEM_VRAM_TYPE);
    if (!p) p = vglAlloc(size, VGL_MEM_SLOW_TYPE);
    if (!p) p = vglAlloc(size, VGL_MEM_RAM_TYPE);
    return p;
}

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
 * as the uniform's rows, with off[c] in .w, lets the shader use dot(row, (p, 1)).
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
    "attribute vec4 a_morph;\n"     /* face animation: xyz = offset, w = the weight it moves (9 = none) */
    /* Must fit the Vita's vertex uniform budget (GL_MAX_VERTEX_UNIFORM_VECTORS, logged
     * at init; the 424-vector version crashed at link on hardware): 4 + 96 + 4 + 12 = 116. */
    /* Every vertex uniform in ONE array, uploaded with one glUniform4fv per draw (each
     * glUniform* call costs a lookup and a type query in vitaGL):
     *   0-3 mvp columns | 4 eye-z row | 5 fog (start, end, enabled) | 6 ambient
     *   7 light info (count, fullbright) | 8-11 light dir+type | 12-15 light origin+spot
     *   16-19 light colour+spot scale | 20-115 bones (32 slots * 3 rows, w = translation) */
    "uniform   vec4 u_data[116];\n"
    "varying   vec4 v_tc;\n"             /* xy = uv, z = fog factor */
    "varying   vec4 v_lit;\n"
    "vec3 skinOne(int s, vec3 p) {\n"
    "    int o = s * 3;\n"
    "    vec4 q = vec4(p, 1.0);\n"
    "    return vec3(dot(u_data[o + 20], q), dot(u_data[o + 21], q), dot(u_data[o + 22], q));\n"
    "}\n"
    "vec3 rotOne(int s, vec3 n) {\n"
    "    int o = s * 3;\n"
    "    return vec3(dot(u_data[o + 20].xyz, n), dot(u_data[o + 21].xyz, n), dot(u_data[o + 22].xyz, n));\n"
    "}\n"
    /* One RB_Light_Real light. Called with constant indices (no loop/break or dynamic
     * light indexing, which the on-device shader compiler is fragile with). */
    "vec3 lightOne(vec4 dirT, vec4 org, vec4 col, vec3 n, vec3 sk) {\n"
    "    float t = dirT.w;\n"
    "    if (t > 0.5 && t < 1.5) {\n"                 /* LIGHT_DIRECTIONAL */
    "        return max(dot(dirT.xyz, n), 0.0) * col.xyz;\n"
    "    }\n"
    "    if (t > 1.5 && t < 2.5) {\n"                 /* LIGHT_SPOT */
    "        float d = dot(dirT.xyz, n);\n"
    "        if (d <= 0.0) return vec3(0.0);\n"
    "        vec3 v = org.xyz - sk;\n"
    "        float pr = dot(v, dirT.xyz); pr *= pr;\n"
    "        float d2 = dot(v, v);\n"
    "        float mi = (org.w - d2 / pr) * col.w;\n"
    "        return mi > 0.0 ? (d / d2) * min(mi, 1.0) * col.xyz : vec3(0.0);\n"
    "    }\n"
    "    if (t > 2.5) {\n"                            /* LIGHT_SPOT_FAST */
    "        float d = dot(dirT.xyz, n);\n"
    "        vec3 v = org.xyz - sk;\n"
    "        return d > 0.0 ? (d / dot(v, v)) * col.xyz : vec3(0.0);\n"
    "    }\n"
    "    vec3 v = org.xyz - sk;\n"                    /* LIGHT_POINT */
    "    float d = dot(v, n);\n"
    "    return d > 0.0 ? (d / dot(v, v)) * col.xyz : vec3(0.0);\n"
    "}\n"
    "void main(void) {\n"
    "    int s0 = int(a_idx.x); int s1 = int(a_idx.y); int s2 = int(a_idx.z); int s3 = int(a_idx.w);\n"
    /* The CPU path adds the morph offset to the vertex's first weight only. */
    "    vec3 m0 = a_morph.w < 0.5 ? a_morph.xyz : vec3(0.0);\n"
    "    vec3 m1 = (a_morph.w > 0.5 && a_morph.w < 1.5) ? a_morph.xyz : vec3(0.0);\n"
    "    vec3 m2 = (a_morph.w > 1.5 && a_morph.w < 2.5) ? a_morph.xyz : vec3(0.0);\n"
    "    vec3 m3 = (a_morph.w > 2.5 && a_morph.w < 3.5) ? a_morph.xyz : vec3(0.0);\n"
    "    vec3 sk = a_w0.w * skinOne(s0, a_w0.xyz + m0)\n"
    "            + a_w1.w * skinOne(s1, a_w1.xyz + m1)\n"
    "            + a_w2.w * skinOne(s2, a_w2.xyz + m2)\n"
    "            + a_w3.w * skinOne(s3, a_w3.xyz + m3);\n"
    "    gl_Position = mat4(u_data[0], u_data[1], u_data[2], u_data[3]) * vec4(sk, 1.0);\n"
    "    v_tc.xy = a_texcoord;\n"
    "    vec3 n = normalize(rotOne(s0, a_normal));\n"
    "    vec3 c;\n"
    "    float a = 1.0;\n"
    "    vec4 li = u_data[7];\n"
    "    if (li.y > 0.5) {\n"
    "        c = vec3(255.0);\n"
    "    } else if (li.x < 0.5) {\n"
    "        c = u_data[6].rgb; a = u_data[6].a;\n"
    "    } else {\n"
    "        c = u_data[6].rgb + lightOne(u_data[8], u_data[12], u_data[16], n, sk);\n"
    "        if (li.x > 1.5) c += lightOne(u_data[9], u_data[13], u_data[17], n, sk);\n"
    "        if (li.x > 2.5) c += lightOne(u_data[10], u_data[14], u_data[18], n, sk);\n"
    "        if (li.x > 3.5) c += lightOne(u_data[11], u_data[15], u_data[19], n, sk);\n"
    "    }\n"
    "    v_lit = vec4(clamp(c, 0.0, 255.0) * (1.0 / 255.0), a);\n"
    "    vec4 fg = u_data[5];\n"
    "    float ez = -dot(u_data[4], vec4(sk, 1.0));\n"
    "    v_tc.z = fg.z > 0.5 ? clamp((fg.y - ez) / (fg.y - fg.x), 0.0, 1.0) : 1.0;\n"
    "    v_tc.w = 1.0;\n"
    "}\n";

static const char *s_skin_frag_src =
    "#version 100\n"
    "precision mediump float;\n"
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
#define ATTR_COUNT    7   /* built per surface; ATTR_MORPH comes from the buffers below */
#define ATTR_MORPH    7

#define VITA_SKIN_MAX_WEIGHTS      4
#define VITA_SKIN_MAX_BONESLOTS    32   /* u_boneMat[96]; more → CPU path */
#define VITA_SKIN_MAX_LIGHTS       4    /* u_lDir/u_lOrg/u_lCol[4]; more → CPU path */

#define VITA_SKIN_CACHE_CAP   1024

/* Face animation (morphs) on the GPU: the offsets of each animating face are written per
 * frame into a ring of VITA_SKIN_MORPH_FRAMES buffers (a buffer is reused only once the
 * GPU is done with its frame); everything else reads a buffer of zeros. Batches are cut
 * at VITA_SKIN_MAX_BATCH_VERTS vertices so the zeros cover any of them. */
#define VITA_SKIN_MAX_BATCH_VERTS  4096

/* Level of detail: the skeletal meshes are progressive (a vertex past N collapses into
 * an earlier one), the CPU path draws a far model with fewer vertices. A one-batch
 * surface gets index lists at these fractions of its vertices; the draw uses the
 * smallest one that keeps at least the vertices the CPU path would. */
#define VITA_SKIN_LODS 5
static const float s_lodFraction[VITA_SKIN_LODS] = { 0.75f, 0.5f, 0.35f, 0.25f, 0.15f };
#define VITA_SKIN_MORPH_FRAMES     4
#define VITA_SKIN_MORPH_VERTS      12288  /* per frame (vec4 each): ~20 faces */
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
    /* boneChannel -> the tiki's local channel, for localChnTiki (the lookup depends on
     * the model only, so it is done once instead of per surface per frame). */
    const void        *localChnTiki;
    int                localChn[VITA_SKIN_MAX_BONESLOTS];
    /* The surface has morph targets (faces): drawn here only for entities whose
     * morphs are off this frame, which RB_SkelMesh also skins without them. */
    qboolean           hasMorphs;
    /* Sparse morph targets (first batch only, built at the first animated frame): the
     * vertices and offsets of target m are morphVert/morphOff[morphStart[m]..morphStart[m+1]),
     * so a frame walks only the targets whose weight is not zero. */
    int                morphCount;
    int               *morphStart;
    unsigned short    *morphVert;
    float             *morphOff;

    /* A surface with more than VITA_SKIN_MAX_BONESLOTS bones (hands: the fingers) is
     * split into batches of triangles that each use at most that many; they are chained
     * through 'next' (a slot, 0 = last). */
    int                next;
    /* The surface vertex behind each batch vertex and the weight slot its morph offset
     * goes to, 9 = none (malloc'd, morph surfaces only). */
    unsigned short    *orig;
    unsigned char     *morphSlot;
    /* LOD index lists, by decreasing vertex count (lodCount 0 = full detail only) */
    int                lodCount;
    int                lodVerts[VITA_SKIN_LODS];
    int                lodNumIdx[VITA_SKIN_LODS];
    unsigned short    *lodIbuf[VITA_SKIN_LODS];
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
/* The skin program stays bound across consecutive GPU-skinned surfaces (the surface
 * list is sorted by shader, so they usually come in runs); R_VitaGpuSkin_Unbind puts
 * program 0 back before any fixed-function draw. */
static qboolean            s_skin_program_bound = qfalse;

void R_VitaGpuSkin_Unbind(void)
{
    if (s_skin_program_bound) {
        glUseProgram(0);
        s_skin_program_bound = qfalse;
    }
}
static qboolean            s_skin_ready   = qfalse;
static int                 s_loc_data = -1, s_loc_diffuse = -1, s_loc_fogColor = -1, s_loc_alphaTest = -1;
/* u_data: the vertex uniforms of one draw, uploaded in one call (see the vertex shader) */
#define VITA_SKIN_DATA_BONES       20
#define VITA_SKIN_DATA_VECS        116
static float               s_vdata[VITA_SKIN_DATA_VECS * 4];
/* The fragment uniforms stay in the program between draws: set them only on change. */
static qboolean            s_fragCacheValid = qfalse;
static float               s_lastFogColor[4], s_lastAlphaTest[4];

cvar_t *r_vita_gpu_skinning = NULL;

/* TIKI bone cache + channel lookup (from RB_SkelMesh's path). */

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

static float *s_morphZero;                              /* vec4 zeros, VITA_SKIN_MAX_BATCH_VERTS */
static float *s_morphRing[VITA_SKIN_MORPH_FRAMES];      /* vec4, VITA_SKIN_MORPH_VERTS each */
static int    s_morphFrame, s_morphUsed;

static qboolean VitaSkin_AllocMorphBuffers(void)
{
    int i;
    s_morphZero = (float *)vglAlloc(sizeof(float) * 4 * VITA_SKIN_MAX_BATCH_VERTS, VGL_MEM_RAM_TYPE);
    if (!s_morphZero) return qfalse;
    Com_Memset(s_morphZero, 0, sizeof(float) * 4 * VITA_SKIN_MAX_BATCH_VERTS);
    for (i = 0; i < VITA_SKIN_MORPH_FRAMES; i++) {
        s_morphRing[i] = (float *)vglAlloc(sizeof(float) * 4 * VITA_SKIN_MORPH_VERTS, VGL_MEM_RAM_TYPE);
    }
    return qtrue;
}

/* End of a frame on the render thread: the next frame writes the next ring buffer. */
void R_VitaGpuSkin_EndFrame(void)
{
    s_morphFrame = (s_morphFrame + 1) % VITA_SKIN_MORPH_FRAMES;
    s_morphUsed  = 0;
}

void R_VitaGpuSkin_Init(void)
{
    r_vita_gpu_skinning = ri.Cvar_Get("r_vita_gpu_skinning", "0", CVAR_ARCHIVE);

    /* Only compile when enabled at boot: linking this program crashes on real
     * hardware (2026-09-23, right after "skin.frag compile OK"; fine in Vita3K),
     * so with the cvar off at init nothing here may run. */
    if (!r_vita_gpu_skinning->integer) {
        return;
    }
    if (s_skin_ready) {
        return;
    }

    {
        int maxVec = 0;
        glGetIntegerv(0x8DFB /* GL_MAX_VERTEX_UNIFORM_VECTORS */, &maxVec);
        glGetError();
        ri.Printf(PRINT_ALL, "[VITA-SKIN] GL_MAX_VERTEX_UNIFORM_VECTORS=%d (shader uses 116)\n", maxVec);
    }
    /* vitaGL's default VGL_MODE_POSTPONED makes glCompileShader only store the source
     * and compiles in glLinkProgram. But vglBindAttribLocation must run BEFORE the link
     * and looks the names up in the compiled program: with nothing compiled yet it called
     * sceGxmProgramFindParameterByName(NULL) (data abort on hardware; in Vita3K it
     * silently failed, leaving the vertex program NULL). Compile this pair immediately
     * (SHADER_PAIR: vertex then fragment, as done here), then restore the default. */
    vglSetSemanticBindingMode(VGL_MODE_SHADER_PAIR_ENUM);
    unsigned int vs = VitaSkin_CompileStage(GL_VERTEX_SHADER,   s_skin_vert_src, "skin.vert");
    if (!vs) { vglSetSemanticBindingMode(VGL_MODE_POSTPONED_ENUM); return; }
    unsigned int fs = VitaSkin_CompileStage(GL_FRAGMENT_SHADER, s_skin_frag_src, "skin.frag");
    if (!fs) { glDeleteShader(vs); vglSetSemanticBindingMode(VGL_MODE_POSTPONED_ENUM); return; }

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
    vglBindAttribLocation(prog, ATTR_MORPH,    "a_morph",    4, GL_FLOAT);

    glLinkProgram(prog);
    vglSetSemanticBindingMode(VGL_MODE_POSTPONED_ENUM);

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
    s_loc_data       = glGetUniformLocation(prog, "u_data");
    s_loc_diffuse    = glGetUniformLocation(prog, "u_diffuse");
    s_loc_fogColor   = glGetUniformLocation(prog, "u_fogColor");
    s_loc_alphaTest  = glGetUniformLocation(prog, "u_alphaTest");
    s_fragCacheValid = qfalse;
    if (!VitaSkin_AllocMorphBuffers()) {
        ri.Printf(PRINT_WARNING, "[VITA-SKIN] no memory for the morph buffers: GPU skinning off\n");
        return;
    }
    s_skin_ready     = qtrue;

    ri.Printf(PRINT_ALL,
        "[VITA-SKIN] program LINK OK, prog=%u (vgl* pipeline) "
        "data=%d diffuse=%d\n",
        prog, s_loc_data, s_loc_diffuse);
}

void R_VitaGpuSkin_Shutdown(void)
{
    R_VitaGpuSkin_LevelReset();
    if (s_skin_program) {
        glDeleteProgram(s_skin_program);
        s_skin_program = 0;
    }
    if (s_morphZero) vglFree(s_morphZero);
    s_morphZero = NULL;
    for (int i = 0; i < VITA_SKIN_MORPH_FRAMES; i++) {
        if (s_morphRing[i]) vglFree(s_morphRing[i]);
        s_morphRing[i] = NULL;
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
    if (e->orig) free(e->orig);
    e->orig = NULL;
    if (e->morphSlot) free(e->morphSlot);
    e->morphSlot = NULL;
    free(e->morphStart);
    free(e->morphVert);
    free(e->morphOff);
    e->morphStart = NULL;
    e->morphVert  = NULL;
    e->morphOff   = NULL;
    e->morphCount = 0;

    for (int l = 0; l < e->lodCount; l++) {
        if (e->lodIbuf[l]) vglFree(e->lodIbuf[l]);
        e->lodIbuf[l] = NULL;
    }
    e->lodCount = 0;
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

/* The weights a vertex keeps on the GPU: its VITA_SKIN_MAX_WEIGHTS strongest, renormalized
 * when some were dropped (e.g. Ranger_pants has 5), with the channel of each one's bone. */
typedef struct {
    const skeletorVertex_t *v;
    const skelWeight_t     *w;
    int                     numPick;
    int                     pick[VITA_SKIN_MAX_WEIGHTS];
    int                     channel[VITA_SKIN_MAX_WEIGHTS];
    float                   weightScale;
} vitaSkinVert_t;

static void VitaSkin_PickWeights(vitaSkinVert_t *out, const skeletorVertex_t *v, const skelHeaderGame_t *skelmodel)
{
    const skelWeight_t *w = (const skelWeight_t *)((const byte *)v + sizeof(skeletorVertex_t)
                                                   + sizeof(skeletorMorph_t) * v->numMorphs);
    float kept = 0.0f, total = 0.0f;
    int   k, m;

    out->v           = v;
    out->w           = w;
    out->numPick     = v->numWeights < VITA_SKIN_MAX_WEIGHTS ? v->numWeights : VITA_SKIN_MAX_WEIGHTS;
    out->weightScale = 1.0f;
    for (k = 0; k < v->numWeights; k++) {
        total += w[k].boneWeight;
    }
    for (m = 0; m < out->numPick; m++) {
        int best = -1;
        for (k = 0; k < v->numWeights; k++) {
            int used = 0, u;
            for (u = 0; u < m; u++) {
                if (out->pick[u] == k) used = 1;
            }
            if (!used && (best < 0 || w[k].boneWeight > w[best].boneWeight)) {
                best = k;
            }
        }
        out->pick[m]    = best;
        out->channel[m] = skelmodel->pBones[w[best].boneIndex].channel;
        kept += w[best].boneWeight;
    }
    if (v->numWeights > out->numPick && kept > 0.0f) {
        out->weightScale = total / kept;
    }
}

/* Channels of triangle t's vertices that the batch channel set 'set' lacks. */
static int VitaSkin_NewChannels(const vitaSkinVert_t *vtx, const skelIndex_t *tri, const int *set, int setCount,
                                int *added)
{
    int n = 0, c, k, j;

    for (c = 0; c < 3; c++) {
        const vitaSkinVert_t *vi = &vtx[tri[c]];
        for (k = 0; k < vi->numPick; k++) {
            const int ch = vi->channel[k];
            int       seen = 0;
            for (j = 0; j < setCount && !seen; j++) seen = set[j] == ch;
            for (j = 0; j < n && !seen; j++) seen = added[j] == ch;
            if (!seen) added[n++] = ch;
        }
    }
    return n;
}

/* Build the attribute + index buffers for a surface: one batch, or several when it uses
 * more bones than the shader's palette holds. Returns the first batch's cache slot (>0),
 * or a negative ineligibility code. */
static int VitaSkin_BuildSurf(skelSurfaceGame_t *sf, skelHeaderGame_t *skelmodel)
{
    vitaSkinVert_t   *vtx;
    const skeletorVertex_t *v;
    short            *remap;
    int              *batchOf;
    int               i, j, t, numBatches, first = 0, prev = 0, hasMorphs = 0, result;
    int               set[VITA_SKIN_MAX_BONESLOTS], setCount;

    if (sf->numVerts <= 0 || sf->numTriangles <= 0) return -2;
    if (sf->numVerts > 32767) return -8;          /* u16 indices, short remap */

    vtx     = (vitaSkinVert_t *)malloc(sizeof(*vtx) * sf->numVerts);
    remap   = (short *)malloc(sizeof(*remap) * sf->numVerts);
    batchOf = (int *)malloc(sizeof(*batchOf) * sf->numTriangles);
    if (!vtx || !remap || !batchOf) {
        free(vtx);
        free(remap);
        free(batchOf);
        return -9;
    }
    v = sf->pVerts;
    for (i = 0; i < sf->numVerts; i++) {
        VitaSkin_PickWeights(&vtx[i], v, skelmodel);
        if (v->numMorphs > 0) hasMorphs = 1;
        v = (const skeletorVertex_t *)((const byte *)v + sizeof(skeletorVertex_t)
                                       + sizeof(skeletorMorph_t) * v->numMorphs
                                       + sizeof(skelWeight_t) * v->numWeights);
    }

    /* Triangles in order, a new batch whenever the bones would overflow the palette or
     * the vertices the zero morph buffer covers (remap marks the current batch's). */
    numBatches = 1;
    setCount   = 0;
    for (i = 0; i < sf->numVerts; i++) remap[i] = -1;
    {
    int batchVerts = 0;
    for (t = 0; t < sf->numTriangles; t++) {
        int added[3 * VITA_SKIN_MAX_WEIGHTS];
        int n = VitaSkin_NewChannels(vtx, &sf->pTriangles[t * 3], set, setCount, added);
        int newVerts = 0;
        for (j = 0; j < 3; j++) {
            const int vi = sf->pTriangles[t * 3 + j];
            if (remap[vi] != numBatches - 1) newVerts++;
        }
        if (setCount + n > VITA_SKIN_MAX_BONESLOTS || batchVerts + newVerts > VITA_SKIN_MAX_BATCH_VERTS) {
            batchVerts = 0;
            numBatches++;
            setCount = 0;
            n        = VitaSkin_NewChannels(vtx, &sf->pTriangles[t * 3], set, 0, added);
            if (n > VITA_SKIN_MAX_BONESLOTS) {    /* cannot happen: 3 verts x 4 weights */
                result = -7;
                goto done;
            }
        }
        for (j = 0; j < n; j++) set[setCount++] = added[j];
        for (j = 0; j < 3; j++) {
            const int vi = sf->pTriangles[t * 3 + j];
            if (remap[vi] != numBatches - 1) {
                remap[vi] = (short)(numBatches - 1);
                batchVerts++;
            }
        }
        batchOf[t] = numBatches - 1;
    }
    }

    for (int b = 0; b < numBatches; b++) {
        vitaSkinCacheEntry_t *entry;
        int                   numBV = 0, numIdx = 0, slot, a;

        for (i = 0; i < sf->numVerts; i++) remap[i] = -1;
        for (t = 0; t < sf->numTriangles; t++) {
            if (batchOf[t] != b) continue;
            numIdx += 3;
            for (j = 0; j < 3; j++) {
                const int vi = sf->pTriangles[t * 3 + j];
                if (remap[vi] < 0) remap[vi] = (short)numBV++;
            }
        }

        if (s_skin_cache_count >= VITA_SKIN_CACHE_CAP) {
            result = -6;
            goto fail;
        }
        slot  = s_skin_cache_count++;
        entry = &s_skin_cache[slot];
        Com_Memset(entry, 0, sizeof(*entry));
        entry->sf           = sf;
        entry->localChnTiki = NULL;
        entry->hasMorphs    = hasMorphs;
        if (!first) first = slot;
        if (prev) s_skin_cache[prev].next = slot;
        prev = slot;

        /* One tightly packed GPU-mapped array per attribute + a u16 index list, built
         * once. vglVertexAttribPointerMapped / vglIndexPointerMapped then draw straight
         * from them with no per-draw copy. */
        for (a = 0; a < ATTR_COUNT; a++) {
            entry->attr[a] = (float *)VitaSkin_GpuAlloc(sizeof(float) * s_attr_size[a] * numBV);
            if (!entry->attr[a]) break;
        }
        entry->ibuf = (unsigned short *)VitaSkin_GpuAlloc(sizeof(unsigned short) * numIdx);
        if (a < ATTR_COUNT || !entry->ibuf) {
            result = -9;                          /* out of GPU-mapped RAM → CPU */
            goto fail;
        }
        if (hasMorphs) {
            entry->orig      = (unsigned short *)malloc(sizeof(unsigned short) * numBV);
            entry->morphSlot = (unsigned char *)malloc(numBV);
            if (!entry->orig || !entry->morphSlot) {
                result = -9;
                goto fail;
            }
        }

        for (i = 0; i < sf->numVerts; i++) {
            const vitaSkinVert_t *vi = &vtx[i];
            const int             o  = remap[i];
            if (o < 0) continue;
            if (entry->orig) {
                entry->orig[o]      = (unsigned short)i;
                entry->morphSlot[o] = 9;
                for (j = 0; j < vi->numPick; j++) {
                    if (vi->pick[j] == 0) entry->morphSlot[o] = (unsigned char)j; /* the file's first weight */
                }
            }
            for (j = 0; j < VITA_SKIN_MAX_WEIGHTS; j++) {
                float *wo = entry->attr[ATTR_W0 + j] + o * 4;
                if (j < vi->numPick) {
                    const skelWeight_t *wj      = &vi->w[vi->pick[j]];
                    const int           slotIdx = VitaSkin_LookupOrAddBoneSlot(entry, vi->channel[j]);
                    if (slotIdx < 0) {            /* the partition above keeps this within the palette */
                        result = -7;
                        goto fail;
                    }
                    wo[0] = wj->offset[0];
                    wo[1] = wj->offset[1];
                    wo[2] = wj->offset[2];
                    wo[3] = wj->boneWeight * vi->weightScale;
                    entry->attr[ATTR_IDX][o * 4 + j] = (float)slotIdx;
                } else {
                    wo[0] = wo[1] = wo[2] = wo[3] = 0.0f;
                    entry->attr[ATTR_IDX][o * 4 + j] = 0.0f;
                }
            }
            entry->attr[ATTR_TEXCOORD][o * 2 + 0] = vi->v->texCoords[0];
            entry->attr[ATTR_TEXCOORD][o * 2 + 1] = vi->v->texCoords[1];
            entry->attr[ATTR_NORMAL][o * 3 + 0]   = vi->v->normal[0];
            entry->attr[ATTR_NORMAL][o * 3 + 1]   = vi->v->normal[1];
            entry->attr[ATTR_NORMAL][o * 3 + 2]   = vi->v->normal[2];
        }
        numIdx = 0;
        for (t = 0; t < sf->numTriangles; t++) {
            if (batchOf[t] != b) continue;
            for (j = 0; j < 3; j++) {
                entry->ibuf[numIdx++] = (unsigned short)remap[sf->pTriangles[t * 3 + j]];
            }
        }
        entry->numVerts   = numBV;
        entry->numIndexes = numIdx;
    }

    /* LOD index lists (one-batch surfaces: a collapsed vertex of a split surface may be
     * in another batch). Same collapse as RB_SkelMesh: vertices past renderCount go to
     * their collapse target, and triangles that become degenerate are dropped. */
    if (numBatches == 1 && sf->pCollapse) {
        vitaSkinCacheEntry_t *entry = &s_skin_cache[first];
        short                *collapse = (short *)malloc(sizeof(short) * sf->numVerts);
        if (collapse) {
            for (int l = 0; l < VITA_SKIN_LODS; l++) {
                const int rc = (int)(sf->numVerts * s_lodFraction[l]);
                unsigned short *ib;
                int             n = 0;
                if (rc < 3) break;
                for (i = 0; i < rc; i++) collapse[i] = (short)i;
                for (i = rc; i < sf->numVerts; i++) collapse[i] = collapse[sf->pCollapse[i]];
                ib = (unsigned short *)VitaSkin_GpuAlloc(sizeof(unsigned short) * sf->numTriangles * 3);
                if (!ib) break;
                for (t = 0; t < sf->numTriangles; t++) {
                    const int a = collapse[sf->pTriangles[t * 3]], b = collapse[sf->pTriangles[t * 3 + 1]],
                              c = collapse[sf->pTriangles[t * 3 + 2]];
                    if (a == b || b == c || c == a || remap[a] < 0 || remap[b] < 0 || remap[c] < 0) continue;
                    ib[n++] = (unsigned short)remap[a];
                    ib[n++] = (unsigned short)remap[b];
                    ib[n++] = (unsigned short)remap[c];
                }
                entry->lodIbuf[l]   = ib;
                entry->lodVerts[l]  = rc;
                entry->lodNumIdx[l] = n;
                entry->lodCount     = l + 1;
            }
            free(collapse);
        }
    }

    ri.Printf(PRINT_DEVELOPER, "[VITA-SKIN] built slot %d '%s' (%d verts, %d tris, %d batch%s)\n", first,
              sf->name[0] ? sf->name : "?", sf->numVerts, sf->numTriangles, numBatches, numBatches > 1 ? "es" : "");
    result = first;
    goto done;

fail:
    /* the batches are the last slots allocated: give them all back */
    if (first) {
        for (i = first; i < s_skin_cache_count; i++) {
            VitaSkin_FreeEntry(&s_skin_cache[i]);
            Com_Memset(&s_skin_cache[i], 0, sizeof(s_skin_cache[i]));
        }
        s_skin_cache_count = first;
    }
done:
    free(vtx);
    free(remap);
    free(batchOf);
    return result;
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
/* Why the last VitaSkin_EligibleStage call refused (log only): the n-th check. */
static int s_stageFail;

static shaderStage_t *VitaSkin_EligibleStage(int *alphaTestMode, float *entityAlpha)
{
    shaderStage_t *st;
    unsigned int   atest;

    if (!tess.shader || !tess.xstages || tess.shader->numUnfoggedPasses != 1) { s_stageFail = 1; return NULL; }
    st = tess.xstages[0];
    if (!st || !st->active) { s_stageFail = 2; return NULL; }
    if (tess.xstages[1] && tess.xstages[1]->active) { s_stageFail = 3; return NULL; }   /* multi-stage */

    if (st->bundle[0].numImageAnimations > 1 || !st->bundle[0].image[0]) { s_stageFail = 4; return NULL; }
    if (st->bundle[0].tcGen != TCGEN_TEXTURE || st->bundle[0].numTexMods) { s_stageFail = 5; return NULL; }
    if (st->bundle[1].image[0]) { s_stageFail = 6; return NULL; }                       /* multitexture */

    /* rgbGen: only the spherical-lighting path that RB_Light_Real / fullbright feed. */
    if (st->rgbGen != CGEN_LIGHTING_SPHERICAL) { s_stageFail = 7; return NULL; }
    if (!r_drawspherelights->integer || !backEnd.currentSphere) { s_stageFail = 8; return NULL; }
    if (backEnd.currentSphere->TessFunction == RB_Light_Real) {
        if (backEnd.currentSphere->bUsesCubeMap) { s_stageFail = 9; return NULL; }
        /* more than VITA_SKIN_MAX_LIGHTS: the strongest ones are used (DrawSurf) */
    } else if (backEnd.currentSphere->TessFunction != RB_Light_Fullbright) {
        { s_stageFail = 11; return NULL; }                                               /* grid / none → CPU */
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
        { s_stageFail = 12; return NULL; }
    }

    atest = st->stateBits & GLS_ATEST_BITS;
    switch (atest) {
    case 0:               *alphaTestMode = 0; break;
    case GLS_ATEST_GT_0:  *alphaTestMode = 1; break;
    case GLS_ATEST_LT_80: *alphaTestMode = 2; break;
    case GLS_ATEST_GE_80: *alphaTestMode = 3; break;
    default:              { s_stageFail = 13; return NULL; }                             /* foliage tests → CPU */
    }
    return st;
}

/*
 * Try to draw a TIKI skeletal surface on the GPU. Returns qtrue if it
 * handled the draw (caller must then skip the CPU path), qfalse to fall
 * back to CPU (ineligible surface/shader, missing bone channel, or not ready).
 */
/* Log each refused surface (by build) or shader (by stage check) once. */
static void VitaSkin_LogRefusal(const char *what, const char *name, int code, const skelSurfaceGame_t *sf)
{
    static const void *logged[128];
    static int         numLogged;
    const void        *key = sf ? (const void *)sf : (const void *)tess.shader;
    int                i, maxWeights = 0, morphVerts = 0;

    for (i = 0; i < numLogged; i++) {
        if (logged[i] == key) {
            return;
        }
    }
    if (numLogged == (int)ARRAY_LEN(logged)) {
        return;
    }
    logged[numLogged++] = key;
    if (sf && sf->pVerts) {
        const skeletorVertex_t *v = sf->pVerts;
        for (i = 0; i < sf->numVerts; i++) {
            if (v->numWeights > maxWeights) maxWeights = v->numWeights;
            if (v->numMorphs > 0) morphVerts++;
            v = (const skeletorVertex_t *)((const byte *)v + sizeof(skeletorVertex_t)
                                          + sizeof(skelWeight_t) * v->numWeights
                                          + sizeof(skeletorMorph_t) * v->numMorphs);
        }
    }
    ri.Printf(PRINT_ALL, "[VITA-SKIN] refused %s '%s' code %d | shader '%s' | verts %d maxWeights %d morphVerts %d\n",
        what, name, code, tess.shader ? tess.shader->name : "?", sf ? sf->numVerts : 0, maxWeights, morphVerts);
}

int vita_skin_fail;	// RT-PROF: why the last R_VitaGpuSkin_DrawSurf fell back to the CPU
int vita_skin_failsub;	// RT-PROF3: the stage check or surface build code behind it

/* The sparse morph lists of a surface (see vitaSkinCacheEntry_t). */
static qboolean VitaSkin_BuildMorphLists(vitaSkinCacheEntry_t *e, const skelSurfaceGame_t *sf)
{
    const skeletorVertex_t *v;
    int                     i, k, total = 0, count = 0;
    int                    *fill;

    v = sf->pVerts;
    for (i = 0; i < sf->numVerts; i++) {
        const skeletorMorph_t *m = (const skeletorMorph_t *)((const byte *)v + sizeof(skeletorVertex_t));
        for (k = 0; k < v->numMorphs; k++, m++) {
            if (m->morphIndex + 1 > count) count = m->morphIndex + 1;
            total++;
        }
        v = (const skeletorVertex_t *)((const byte *)v + sizeof(skeletorVertex_t)
                                       + sizeof(skeletorMorph_t) * v->numMorphs + sizeof(skelWeight_t) * v->numWeights);
    }
    e->morphStart = (int *)calloc(count + 1, sizeof(int));
    e->morphVert  = (unsigned short *)malloc((total ? total : 1) * sizeof(unsigned short));
    e->morphOff   = (float *)malloc((total ? total : 1) * 3 * sizeof(float));
    fill          = (int *)calloc(count + 1, sizeof(int));
    if (!e->morphStart || !e->morphVert || !e->morphOff || !fill) {
        free(fill);
        return qfalse;
    }
    /* count per target, then prefix sums, then fill */
    v = sf->pVerts;
    for (i = 0; i < sf->numVerts; i++) {
        const skeletorMorph_t *m = (const skeletorMorph_t *)((const byte *)v + sizeof(skeletorVertex_t));
        for (k = 0; k < v->numMorphs; k++, m++) e->morphStart[m->morphIndex + 1]++;
        v = (const skeletorVertex_t *)((const byte *)v + sizeof(skeletorVertex_t)
                                       + sizeof(skeletorMorph_t) * v->numMorphs + sizeof(skelWeight_t) * v->numWeights);
    }
    for (i = 0; i < count; i++) e->morphStart[i + 1] += e->morphStart[i];
    v = sf->pVerts;
    for (i = 0; i < sf->numVerts; i++) {
        const skeletorMorph_t *m = (const skeletorMorph_t *)((const byte *)v + sizeof(skeletorVertex_t));
        for (k = 0; k < v->numMorphs; k++, m++) {
            const int at = e->morphStart[m->morphIndex] + fill[m->morphIndex]++;
            e->morphVert[at]        = (unsigned short)i;
            e->morphOff[at * 3 + 0] = m->offset[0];
            e->morphOff[at * 3 + 1] = m->offset[1];
            e->morphOff[at * 3 + 2] = m->offset[2];
        }
        v = (const skeletorVertex_t *)((const byte *)v + sizeof(skeletorVertex_t)
                                       + sizeof(skeletorMorph_t) * v->numMorphs + sizeof(skelWeight_t) * v->numWeights);
    }
    free(fill);
    e->morphCount = count;
    return qtrue;
}

qboolean R_VitaGpuSkin_DrawSurf(void *sfV, void *tikiV, void *skelmodelV, void *bonesV, float scale, int renderCount)
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
    static float lDir[VITA_SKIN_MAX_LIGHTS * 4], lOrg[VITA_SKIN_MAX_LIGHTS * 4], lCol[VITA_SKIN_MAX_LIGHTS * 4];

    if (!s_skin_ready || !r_vita_gpu_skinning || !r_vita_gpu_skinning->integer) { vita_skin_fail = 1; return qfalse; }

    stage = VitaSkin_EligibleStage(&alphaTestMode, &entityAlpha);
    vita_skin_failsub = 0;
    if (!stage) { vita_skin_fail = 2; vita_skin_failsub = s_stageFail; VitaSkin_LogRefusal("stage", sf->name, s_stageFail, NULL); return qfalse; }

    slot = VitaSkin_HashFind(sf);
    if (slot == 0) {
        /* First time seen — build (or mark ineligible) and register. */
        slot = VitaSkin_BuildSurf(sf, skelmodel);
        VitaSkin_HashInsert(sf, slot > 0 ? slot : -1);
    }
    if (slot <= 0) { vita_skin_fail = 3; vita_skin_failsub = -slot; VitaSkin_LogRefusal("surface", sf->name, slot, sf); return qfalse; }                     /* ineligible → CPU */
    if (slot >= s_skin_cache_count) { vita_skin_fail = 4; return qfalse; }    /* stale guard */
    e = &s_skin_cache[slot];
    if (e->sf != sf || !e->ibuf) { vita_skin_fail = 4; return qfalse; }       /* stale guard */
    /* Face animating this frame: its offsets go to this frame's ring buffer (all of the
     * surface's batches, or none: then the CPU path draws it). */
    const float *morphData = NULL;
    if (e->hasMorphs && backEnd.currentEntity->e.hasMorph) {
        static float tmp[VITA_SKIN_MAX_BATCH_VERTS * 3];
        const int   *weights = &backEnd.data->morphCache[backEnd.currentEntity->e.morphstart];
        int          need = 0, active = 0, m;
        float       *dst;

        if (!e->morphStart && !VitaSkin_BuildMorphLists(e, sf)) {
            vita_skin_fail = 0;                     /* no memory → CPU morphs */
            return qfalse;
        }
        for (m = 0; m < e->morphCount && !active; m++) {
            active = weights[m] && e->morphStart[m + 1] > e->morphStart[m];
        }
        /* a face at rest draws with the zero offsets, like a surface without morphs */
        if (active) {
            for (vitaSkinCacheEntry_t *be = e;; be = &s_skin_cache[be->next]) {
                need += be->numVerts;
                if (!be->next || be->next >= s_skin_cache_count) break;
            }
            if (sf->numVerts > VITA_SKIN_MAX_BATCH_VERTS || !s_morphRing[s_morphFrame]
                || s_morphUsed + need > VITA_SKIN_MORPH_VERTS) {
                vita_skin_fail = 0;                 /* no room this frame → CPU morphs */
                return qfalse;
            }
            memset(tmp, 0, sf->numVerts * 3 * sizeof(float));
            for (m = 0; m < e->morphCount; m++) {
                const int w = weights[m];
                int       k;
                if (!w) continue;
                for (k = e->morphStart[m]; k < e->morphStart[m + 1]; k++) {
                    float       *t = &tmp[e->morphVert[k] * 3];
                    const float *o = &e->morphOff[k * 3];
                    t[0] += w * o[0];
                    t[1] += w * o[1];
                    t[2] += w * o[2];
                }
            }
            dst       = s_morphRing[s_morphFrame] + s_morphUsed * 4;
            morphData = dst;
            for (vitaSkinCacheEntry_t *be = e;; be = &s_skin_cache[be->next]) {
                for (i = 0; i < be->numVerts; i++, dst += 4) {
                    const float *t = &tmp[be->orig[i] * 3];
                    dst[0] = t[0];
                    dst[1] = t[1];
                    dst[2] = t[2];
                    dst[3] = (float)be->morphSlot[i];
                }
                if (!be->next || be->next >= s_skin_cache_count) break;
            }
            s_morphUsed += need;
        }
    }

    /* Every batch needs all its bones on this model: checked before drawing any of them. */
    for (vitaSkinCacheEntry_t *be = e;; be = &s_skin_cache[be->next]) {
        if (be->sf != sf || !be->ibuf) { vita_skin_fail = 4; return qfalse; }    /* stale guard */
        if (be->localChnTiki != tiki) {
            for (i = 0; i < be->numBoneSlots; i++) {
                be->localChn[i] = ri.TIKI_GetLocalChannel(tiki, be->boneChannel[i]);
            }
            be->localChnTiki = tiki;
        }
        for (i = 0; i < be->numBoneSlots; i++) {
            if (be->localChn[i] < 0) { vita_skin_fail = 5; return qfalse; }      /* channel absent → CPU */
        }
        if (!be->next || be->next >= s_skin_cache_count) break;
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
        const int numAll     = fullbright ? 0 : backEnd.currentSphere->numRealLights;
        const int numLights  = numAll < VITA_SKIN_MAX_LIGHTS ? numAll : VITA_SKIN_MAX_LIGHTS;
        int       lightIdx[VITA_SKIN_MAX_LIGHTS];

        /* The shader takes VITA_SKIN_MAX_LIGHTS lights: with more around the entity,
         * use the strongest (the rest add little and would send the whole model to
         * the CPU path, which is what most soldier surfaces did near 5+ lights). */
        {
            int k, m;
            for (m = 0; m < numLights; m++) {
                int best = -1;
                for (k = 0; k < numAll; k++) {
                    int used = 0, u;
                    for (u = 0; u < m; u++) {
                        if (lightIdx[u] == k) used = 1;
                    }
                    if (!used && (best < 0
                                  || backEnd.currentSphere->light[k].fIntensity
                                         > backEnd.currentSphere->light[best].fIntensity)) {
                        best = k;
                    }
                }
                lightIdx[m] = best;
            }
        }
        ambient[0] = backEnd.currentSphere->ambient.level[0];
        ambient[1] = backEnd.currentSphere->ambient.level[1];
        ambient[2] = backEnd.currentSphere->ambient.level[2];
        ambient[3] = backEnd.currentSphere->ambient.level[3] * (1.0f / 255.0f);
        lightInfo[0] = (float)numLights;
        lightInfo[1] = fullbright ? 1.0f : 0.0f;
        lightInfo[2] = lightInfo[3] = 0.0f;
        for (i = 0; i < numLights; i++) {
            const reallightinfo_t *l = &backEnd.currentSphere->light[lightIdx[i]];
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

    if (!s_skin_program_bound || !s_skin_err_traced) {
        glUseProgram(s_skin_program);                                SKIN_GLCHK("glUseProgram");
        s_skin_program_bound = qtrue;
    }
    /* Header of u_data; the bones follow per batch below. */
    memcpy(&s_vdata[0 * 4], mvp, sizeof(mvp));
    memcpy(&s_vdata[4 * 4], mvZ, sizeof(mvZ));
    memcpy(&s_vdata[5 * 4], fog, sizeof(fog));
    memcpy(&s_vdata[6 * 4], ambient, sizeof(ambient));
    memcpy(&s_vdata[7 * 4], lightInfo, sizeof(lightInfo));
    if (lightInfo[0] > 0.0f) {
        const int numLights = (int)lightInfo[0];
        memcpy(&s_vdata[8 * 4], lDir, numLights * 4 * sizeof(float));
        memcpy(&s_vdata[12 * 4], lOrg, numLights * 4 * sizeof(float));
        memcpy(&s_vdata[16 * 4], lCol, numLights * 4 * sizeof(float));
    }
    if (!s_fragCacheValid || memcmp(s_lastFogColor, fogColor, sizeof(fogColor))) {
        glUniform4fv(s_loc_fogColor, 1, fogColor);
        memcpy(s_lastFogColor, fogColor, sizeof(fogColor));
    }
    if (!s_fragCacheValid || memcmp(s_lastAlphaTest, alphaTest, sizeof(alphaTest))) {
        glUniform4fv(s_loc_alphaTest, 1, alphaTest);
        memcpy(s_lastAlphaTest, alphaTest, sizeof(alphaTest));
    }
    if (!s_fragCacheValid) {
        glUniform1i(s_loc_diffuse, 0);                               SKIN_GLCHK("uniform diffuse");
        s_fragCacheValid = qtrue;
    }                                                                SKIN_GLCHK("uniform fog/alpha");

    /* vgl* pipeline, copy-less: attributes and indices come straight from the
     * GPU-mapped arrays built once in VitaSkin_BuildSurf. It does NOT touch
     * fixed-function client state, so the next CPU surface stays intact. */
    /* One draw per batch, each with its own bone palette (transposed rows, pre-multiplied
     * by the model scale: the CPU path scales the final skinned position, and scaling the
     * bone rotation rows + translation gives the same; the shader normalizes the normal). */
    for (vitaSkinCacheEntry_t *be = e;; be = &s_skin_cache[be->next]) {
        for (i = 0; i < be->numBoneSlots; i++) {
            const skelBoneCache_t *b    = &bones[be->localChn[i]];
            float                 *m    = &s_vdata[(VITA_SKIN_DATA_BONES + i * 3) * 4];
            m[0]  = b->matrix[0][0] * scale;
            m[1]  = b->matrix[1][0] * scale;
            m[2]  = b->matrix[2][0] * scale;
            m[3]  = b->offset[0] * scale;
            m[4]  = b->matrix[0][1] * scale;
            m[5]  = b->matrix[1][1] * scale;
            m[6]  = b->matrix[2][1] * scale;
            m[7]  = b->offset[1] * scale;
            m[8]  = b->matrix[0][2] * scale;
            m[9]  = b->matrix[1][2] * scale;
            m[10] = b->matrix[2][2] * scale;
            m[11] = b->offset[2] * scale;
        }
        glUniform4fv(s_loc_data, VITA_SKIN_DATA_BONES + be->numBoneSlots * 3, s_vdata); SKIN_GLCHK("uniform data");
        for (a = 0; a < ATTR_COUNT; a++) {
            vglVertexAttribPointerMapped(a, be->attr[a]);
        }
        vglVertexAttribPointerMapped(ATTR_MORPH, morphData ? morphData : s_morphZero);
                                                                     SKIN_GLCHK("vglVertexAttribPointerMapped");
        if (morphData) {
            morphData += be->numVerts * 4;
        }
        {
            /* the smallest LOD list keeping at least renderCount vertices */
            const unsigned short *ib = be->ibuf;
            int                   numIdx = be->numIndexes;
            for (int l = 0; l < be->lodCount && be->lodVerts[l] >= renderCount; l++) {
                ib     = be->lodIbuf[l];
                numIdx = be->lodNumIdx[l];
            }
            if (numIdx <= 0) {
                if (!be->next || be->next >= s_skin_cache_count) break;
                continue;
            }
            vglIndexPointerMapped(ib);                               SKIN_GLCHK("vglIndexPointerMapped");
            vglDrawObjects(GL_TRIANGLES, numIdx, 0 /* shader does mvp */); SKIN_GLCHK("vglDrawObjects");
        }
        if (!be->next || be->next >= s_skin_cache_count) break;
    }

    if (!s_skin_err_traced) {
        R_VitaGpuSkin_Unbind();                                      SKIN_GLCHK("glUseProgram(0)");
    }
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
