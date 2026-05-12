/*
 * vita_boot_splash.c — bridge the LiveArea startup.png onto the
 * in-app framebuffer.
 *
 * The PS Vita's LiveArea shows our `sce_sys/livearea/contents/startup.png`
 * as it zooms into the app launch transition. The moment the kernel
 * hands control to eboot.bin the image disappears, vitaGL's framebuffer
 * snaps in, and the user sees a brief flicker / vitaGL splash before
 * our engine renders its first frame.
 *
 * To make the transition seamless we re-draw the same startup.png as
 * the first GL frame after vglInit completes, then keep it on screen
 * through the slow chunks of CL_Init (shader compile, sound, pk3 scan)
 * until the engine starts drawing the EA cinematic or main menu.
 *
 * Notes:
 *   - We use immediate-mode GL on purpose: vitaGL exposes glBegin/glEnd
 *     even though desktop GL deprecates them, and that avoids having
 *     to set up a vertex array / shader pipeline before the engine
 *     wires those up itself.
 *   - The PNG is loaded with libpng (statically linked already).
 *   - We never free the texture: it lives for the duration of the
 *     bring-up only and the engine's own image cache won't touch it.
 */

#ifdef __vita__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <png.h>
#include <vitaGL.h>

/* bg.png is the LiveArea background art (840x500) with the OpenMoHAA
 * branding. startup.png is just the small launch-icon (the "star"
 * effect Sony uses during the LiveArea-to-app zoom transition); we
 * don't want that. */
#define SPLASH_PATH "app0:/sce_sys/livearea/contents/bg.png"

static GLuint s_splash_tex   = 0;
static int    s_splash_w     = 0;
static int    s_splash_h     = 0;
static int    s_splash_active = 0;
static int    s_engine_frames = 0;

static int load_png_rgba(const char *path,
                          unsigned char **out_pixels,
                          int *out_w, int *out_h)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stdout, "[splash] fopen(%s) failed\n", path);
        fflush(stdout);
        return 0;
    }
    fprintf(stdout, "[splash] fopen ok\n"); fflush(stdout);

    unsigned char header[8];
    if (fread(header, 1, 8, fp) != 8 || png_sig_cmp(header, 0, 8) != 0) {
        fclose(fp);
        return 0;
    }

    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png) { fclose(fp); return 0; }
    png_infop  info = png_create_info_struct(png);
    if (!info) { png_destroy_read_struct(&png, NULL, NULL); fclose(fp); return 0; }

    if (setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, NULL);
        fclose(fp);
        return 0;
    }

    png_init_io(png, fp);
    png_set_sig_bytes(png, 8);
    png_read_info(png, info);

    png_uint_32 w = png_get_image_width(png, info);
    png_uint_32 h = png_get_image_height(png, info);
    png_byte    color_type = png_get_color_type(png, info);
    png_byte    bit_depth  = png_get_bit_depth(png, info);

    if (bit_depth == 16)             png_set_strip_16(png);
    if (color_type == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) png_set_expand_gray_1_2_4_to_8(png);
    if (png_get_valid(png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
    if (color_type == PNG_COLOR_TYPE_RGB || color_type == PNG_COLOR_TYPE_GRAY ||
        color_type == PNG_COLOR_TYPE_PALETTE)
        png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
    if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
        png_set_gray_to_rgb(png);
    png_read_update_info(png, info);

    size_t rowbytes = png_get_rowbytes(png, info);
    unsigned char *pixels = (unsigned char *)malloc(rowbytes * h);
    if (!pixels) { png_destroy_read_struct(&png, &info, NULL); fclose(fp); return 0; }

    png_bytep *rows = (png_bytep *)malloc(sizeof(png_bytep) * h);
    if (!rows)   { free(pixels); png_destroy_read_struct(&png, &info, NULL); fclose(fp); return 0; }
    for (png_uint_32 y = 0; y < h; ++y) rows[y] = pixels + y * rowbytes;

    png_read_image(png, rows);
    free(rows);
    png_destroy_read_struct(&png, &info, NULL);
    fclose(fp);

    *out_pixels = pixels;
    *out_w = (int)w;
    *out_h = (int)h;
    return 1;
}

static void splash_upload_texture(void)
{
    unsigned char *pixels = NULL;
    int w = 0, h = 0;
    if (!load_png_rgba(SPLASH_PATH, &pixels, &w, &h)) return;

    glGenTextures(1, &s_splash_tex);
    glBindTexture(GL_TEXTURE_2D, s_splash_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

    free(pixels);
    s_splash_w = w;
    s_splash_h = h;
}

static void splash_draw_quad(void)
{
    if (!s_splash_tex) return;

    glViewport(0, 0, 960, 544);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, 1.0, 1.0, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_LIGHTING);
    glDisable(GL_BLEND);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, s_splash_tex);
    glColor4f(1.f, 1.f, 1.f, 1.f);

    glBegin(GL_QUADS);
        glTexCoord2f(0.f, 0.f); glVertex2f(0.f, 0.f);
        glTexCoord2f(1.f, 0.f); glVertex2f(1.f, 0.f);
        glTexCoord2f(1.f, 1.f); glVertex2f(1.f, 1.f);
        glTexCoord2f(0.f, 1.f); glVertex2f(0.f, 1.f);
    glEnd();
}

/* Called from sdl_glimp.c right after vglInitExtended succeeds. */
void Vita_BootSplash_Show(void)
{
    extern void GLimp_EndFrame(void);

    fprintf(stdout, "[splash] loading %s\n", SPLASH_PATH);
    fflush(stdout);
    splash_upload_texture();

    if (!s_splash_tex) {
        fprintf(stdout, "[splash] PNG load failed, fallback to black\n");
        fflush(stdout);
        glClearColor(0.f, 0.f, 0.f, 1.f);
        for (int i = 0; i < 3; ++i) {
            glClear(GL_COLOR_BUFFER_BIT);
            GLimp_EndFrame();
        }
        return;
    }

    fprintf(stdout, "[splash] loaded %dx%d, painting\n", s_splash_w, s_splash_h);
    fflush(stdout);

    /* Paint several frames so vitaGL's double / triple buffers all
     * carry our image. After this returns the engine continues into
     * R_Init etc; until it actually draws different content the
     * last-painted frame remains on screen — which is exactly what we
     * want. (vglSetDisplayCallback would have been ideal but it
     * crashes immediately on this vitaGL version.) */
    for (int i = 0; i < 6; ++i) {
        glClearColor(0.f, 0.f, 0.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        splash_draw_quad();
        GLimp_EndFrame();
    }
}

void Vita_BootSplash_NoteEngineFrame(void) { /* no-op now */ }
void Vita_BootSplash_Stop(void)            { /* no-op now */ }

#endif /* __vita__ */
