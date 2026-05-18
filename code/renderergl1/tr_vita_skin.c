/*
===========================================================================
OpenMoHAA — PS Vita renderer Phase 2: GPU skeletal skinning.

This file is built ONLY on Vita (#ifdef __vita__).

Goal: move TIKI mesh skinning from CPU (RB_SkelMesh, ~23% of m1l1 frame
time per Mac sample profile) to a vertex shader. The CPU pipeline
currently iterates every vertex, fetches up to 4 bone matrices, does
three scalar dot products + add per weight, accumulates into output
xyz. With shader skinning all that runs in parallel on the SGX543 ALU
and the CPU's job per NPC drops to "set bone uniforms + indexed draw".

Phase 2a (this commit): infrastructure only. We compile + link a tiny
GLSL vertex+fragment shader pair at renderer init, log success or
failure, and stop. The cvar `r_vita_gpu_skinning` defaults to 0 so
nothing in the live draw path uses this yet. Phase 2b wires the
program into `RB_SkelMesh`. Phase 2c handles morphs / non-trivial
weight counts.

Why split: shader compilation on vitaGL (vitashark → SGX cgshader) is
the riskiest step. If the source string has a typo or vitashark
chokes, we want that visible in the log immediately rather than as
the cause of a runtime crash inside the draw path.
===========================================================================
*/

#ifdef __vita__

#include "tr_local.h"

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
#ifndef GL_INFO_LOG_LENGTH
#define GL_INFO_LOG_LENGTH        0x8B84
#endif

/* Vertex shader: matrix-palette skinning, up to MAX_BONES_PER_VERT
 * weights per vertex. The bone matrices are stored as 3 vec4 rows
 * (mat3x4, ARM-friendly transposed layout matching the engine's
 * skelBoneCache_t.matrix[3][4] memory layout). Translations live in
 * a parallel uniform vec4 array.
 *
 * Inputs:
 *   a_position    vec3   local-space vertex offset (drawVert.xyz)
 *   a_texcoord    vec2   diffuse UV (drawVert.st)
 *   a_boneIndices vec4   up to 4 bone indices (cast from u8 attribs)
 *   a_boneWeights vec4   matching weights, summing to ~1.0
 *
 * Uniforms:
 *   u_mvp                  model-view-projection (already concatenated)
 *   u_boneMatrix[MAX*3]    rotation portion (mat3x4 rows)
 *   u_boneOffset[MAX]      translation
 *
 * For weight==0 lanes the contribution is zero so the branch-free
 * accumulate is correct.  */
static const char *s_skin_vert_src =
    "#version 100\n"
    "precision highp float;\n"
    "attribute vec3 a_position;\n"
    "attribute vec2 a_texcoord;\n"
    "attribute vec4 a_boneIndices;\n"
    "attribute vec4 a_boneWeights;\n"
    "uniform   mat4 u_mvp;\n"
    "uniform   vec4 u_boneMatrix[300];\n"   /* 100 bones * 3 rows */
    "uniform   vec4 u_boneOffset[100];\n"
    "varying   vec2 v_texcoord;\n"
    "vec3 skinOne(int bi, vec3 p) {\n"
    "    int o = bi * 3;\n"
    "    return vec3(\n"
    "        dot(u_boneMatrix[o    ].xyz, p),\n"
    "        dot(u_boneMatrix[o + 1].xyz, p),\n"
    "        dot(u_boneMatrix[o + 2].xyz, p)) + u_boneOffset[bi].xyz;\n"
    "}\n"
    "void main(void) {\n"
    "    vec3 skinned = vec3(0.0);\n"
    "    skinned += a_boneWeights.x * skinOne(int(a_boneIndices.x), a_position);\n"
    "    skinned += a_boneWeights.y * skinOne(int(a_boneIndices.y), a_position);\n"
    "    skinned += a_boneWeights.z * skinOne(int(a_boneIndices.z), a_position);\n"
    "    skinned += a_boneWeights.w * skinOne(int(a_boneIndices.w), a_position);\n"
    "    gl_Position = u_mvp * vec4(skinned, 1.0);\n"
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

static unsigned int s_skin_program = 0;
static qboolean     s_skin_ready   = qfalse;

cvar_t *r_vita_gpu_skinning = NULL;

/* Compile a single shader stage, dump info log on failure. */
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
        /* Cvar off — don't even compile. Saves shader cache slot. */
        return;
    }

    if (s_skin_ready) {
        return; /* idempotent */
    }

    unsigned int vs = VitaSkin_CompileStage(GL_VERTEX_SHADER,   s_skin_vert_src, "skin.vert");
    if (!vs) return;
    unsigned int fs = VitaSkin_CompileStage(GL_FRAGMENT_SHADER, s_skin_frag_src, "skin.frag");
    if (!fs) {
        glDeleteShader(vs);
        return;
    }

    unsigned int prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
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

    /* On vitaGL the shaders can be marked for deletion once they're
     * attached and linked — the program retains them. */
    glDeleteShader(vs);
    glDeleteShader(fs);

    s_skin_program = prog;
    s_skin_ready   = qtrue;
    ri.Printf(PRINT_ALL, "[VITA-SKIN] program LINK OK, prog=%u — Phase 2b will wire into RB_SkelMesh\n",
              prog);
}

void R_VitaGpuSkin_Shutdown(void)
{
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

#endif /* __vita__ */
