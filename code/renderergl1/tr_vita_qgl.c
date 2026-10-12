/*
=============================================================================
tr_vita_qgl.c - PS Vita: drop redundant client array calls before vitaGL

vitaGL marks its fixed-function vertex setup dirty on EVERY gl*Pointer and
gl(En|Dis)ableClientState call, same values or not (seen in the shipped libvitaGL
with REA: ffp_dirty_vert_attr / ffp_dirty_vert set unconditionally). The next draw then
redoes reload_ffp_shaders' slow part: the shader mask lookup, the vertex attribute
setup and a sceGxmShaderPatcherCreateVertexProgram call. The renderer repeats these
calls with the same tess arrays for every surface, so at the m3l1 beach that path was
about a fifth of the render thread in Vita3K.

The qgl pointers for these calls are pointed here at R_Init: a call reaches vitaGL only
when its values differ from the last ones passed (the array buffer bound at the time is
part of a pointer's value, as vitaGL reads it there). Raw gl calls elsewhere in the
renderer would make this tracking wrong: tr_vita_vbo.c goes through these too
(R_VitaQgl_NormalPointer / R_VitaQgl_BindBuffer have no qgl pointer in renderergl1).
=============================================================================
*/

#ifdef __vita__

#include "tr_local.h"

#ifndef GL_ARRAY_BUFFER
#define GL_ARRAY_BUFFER 0x8892
#endif

extern void glVertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *ptr);
extern void glNormalPointer(GLenum type, GLsizei stride, const GLvoid *ptr);
extern void glColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *ptr);
extern void glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *ptr);
extern void glEnableClientState(GLenum array);
extern void glDisableClientState(GLenum array);
extern void glClientActiveTexture(GLenum texture);
extern void glBindBuffer(GLenum target, GLuint buffer);

#define VQ_UNITS 4

enum { VQ_VERTEX, VQ_NORMAL, VQ_COLOR, VQ_TEX0, VQ_COUNT = VQ_TEX0 + VQ_UNITS };

typedef struct {
    const void *ptr;
    GLint       size;
    GLenum      type;
    GLsizei     stride;
    GLuint      buf;
    qboolean    valid;
} vitaQglArray_t;

static vitaQglArray_t s_arr[VQ_COUNT];
static unsigned int   s_enabled;  /* bit per VQ_* */
static unsigned int   s_known;    /* bit per VQ_*: s_enabled is what vitaGL has */
static GLuint         s_buf;      /* GL_ARRAY_BUFFER */
static int            s_unit;     /* client active texture unit */
static qboolean       s_on;

cvar_t *r_vita_qgl_cache;

static qboolean VQ_Same(int a, GLint size, GLenum type, GLsizei stride, const void *ptr)
{
    vitaQglArray_t *v = &s_arr[a];

    if (s_on && v->valid && v->ptr == ptr && v->size == size && v->type == type && v->stride == stride
        && v->buf == s_buf) {
        return qtrue;
    }
    v->ptr    = ptr;
    v->size   = size;
    v->type   = type;
    v->stride = stride;
    v->buf    = s_buf;
    v->valid  = qtrue;
    return qfalse;
}

static void APIENTRY VQ_VertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *ptr)
{
    if (!VQ_Same(VQ_VERTEX, size, type, stride, ptr)) glVertexPointer(size, type, stride, ptr);
}

void R_VitaQgl_NormalPointer(GLenum type, GLsizei stride, const GLvoid *ptr)
{
    if (!VQ_Same(VQ_NORMAL, 3, type, stride, ptr)) glNormalPointer(type, stride, ptr);
}

static void APIENTRY VQ_ColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *ptr)
{
    if (!VQ_Same(VQ_COLOR, size, type, stride, ptr)) glColorPointer(size, type, stride, ptr);
}

static void APIENTRY VQ_TexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *ptr)
{
    if (s_unit >= VQ_UNITS || !VQ_Same(VQ_TEX0 + s_unit, size, type, stride, ptr)) {
        glTexCoordPointer(size, type, stride, ptr);
    }
}

static int VQ_ArrayBit(GLenum array)
{
    switch (array) {
    case GL_VERTEX_ARRAY:        return 1 << VQ_VERTEX;
    case GL_NORMAL_ARRAY:        return 1 << VQ_NORMAL;
    case GL_COLOR_ARRAY:         return 1 << VQ_COLOR;
    case GL_TEXTURE_COORD_ARRAY: return s_unit < VQ_UNITS ? 1 << (VQ_TEX0 + s_unit) : 0;
    default:                     return 0;
    }
}

static void APIENTRY VQ_EnableClientState(GLenum array)
{
    const unsigned int bit = VQ_ArrayBit(array);

    if (s_on && bit && (s_known & bit) && (s_enabled & bit)) return;
    s_known |= bit;
    s_enabled |= bit;
    glEnableClientState(array);
}

static void APIENTRY VQ_DisableClientState(GLenum array)
{
    const unsigned int bit = VQ_ArrayBit(array);

    if (s_on && bit && (s_known & bit) && !(s_enabled & bit)) return;
    s_known |= bit;
    s_enabled &= ~bit;
    glDisableClientState(array);
}

void APIENTRY R_VitaQgl_ClientActiveTexture(GLenum texture)
{
    s_unit = texture - GL_TEXTURE0;
    glClientActiveTexture(texture);
}

void R_VitaQgl_BindBuffer(GLenum target, GLuint buffer)
{
    if (target == GL_ARRAY_BUFFER) s_buf = buffer;
    glBindBuffer(target, buffer);
}

/* Forget what vitaGL has: the next call of each kind goes through. */
void R_VitaQgl_Invalidate(void)
{
    int i;

    for (i = 0; i < VQ_COUNT; i++) s_arr[i].valid = qfalse;
    s_known = 0;
}

void R_VitaQgl_Init(void)
{
    r_vita_qgl_cache = ri.Cvar_Get("r_vita_qgl_cache", "1", CVAR_ARCHIVE);
    s_on             = r_vita_qgl_cache->integer != 0;
    s_unit           = 0;
    s_buf            = 0;
    R_VitaQgl_Invalidate();

    qglVertexPointer          = VQ_VertexPointer;
    qglColorPointer           = VQ_ColorPointer;
    qglTexCoordPointer        = VQ_TexCoordPointer;
    qglEnableClientState      = VQ_EnableClientState;
    qglDisableClientState     = VQ_DisableClientState;
    if (qglClientActiveTextureARB) qglClientActiveTextureARB = R_VitaQgl_ClientActiveTexture;
}

/* Per frame: follow r_vita_qgl_cache changes (on → off takes effect at once). */
void R_VitaQgl_Frame(void)
{
    const qboolean on = r_vita_qgl_cache && r_vita_qgl_cache->integer;

    if (on != s_on) {
        s_on = on;
        R_VitaQgl_Invalidate();
    }
}

#endif /* __vita__ */
