/*
 * vitaGL doesn't expose every desktop OpenGL entry point — a handful of
 * GL 2.0+ shader-management functions are absent because vitaGL's shader
 * path is driven by vitaShaRK (CG cross-compilation) rather than the
 * GLSL link/validate model. OpenMoHAA's renderer wires every qgl*
 * function pointer at startup even when the GL1 fixed-function path will
 * never call them, so we provide no-op symbols just to satisfy the
 * GLE() macro assignment in sdl_glimp.c.
 */

#ifdef __vita__

#include <vitaGL.h>

void glDetachShader(GLuint program, GLuint shader)
{
    (void)program; (void)shader;
}

void glValidateProgram(GLuint program)
{
    (void)program;
}

/* Legacy desktop fixed-function entry points vitaGL omits because they
 * have no GL ES counterpart. The Q3-derived renderer registers them in
 * its qgl* table at startup but only invokes them on code paths the
 * BUILD_RENDERER_GL1 fallback never takes (the immediate-mode debug
 * draws, line-stipple wireframes, and the back-buffer pixel readback). */
void glArrayElement(GLint i)
{
    (void)i;
}

void glDrawPixels(GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels)
{
    (void)width; (void)height; (void)format; (void)type; (void)pixels;
}

void glPixelZoom(GLfloat xfactor, GLfloat yfactor)
{
    (void)xfactor; (void)yfactor;
}

void glLineStipple(GLint factor, GLushort pattern)
{
    (void)factor; (void)pattern;
}

void glDrawBuffer(GLenum mode)
{
    (void)mode;
}

void glCompressedTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset,
                               GLsizei width, GLsizei height, GLenum format,
                               GLsizei imageSize, const GLvoid *data)
{
    (void)target; (void)level; (void)xoffset; (void)yoffset;
    (void)width; (void)height; (void)format; (void)imageSize; (void)data;
}

#endif /* __vita__ */
