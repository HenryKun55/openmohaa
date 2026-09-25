/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/
// tr_draw.c -- drawing

#include "tr_local.h"

vec4_t r_colorWhite = { 1.0, 1.0, 1.0, 1.0 };

/*
================
Draw_SetColor
================
*/
static void Draw_SetColor_Exec(const vec4_t rgba) {
#if 1
	if (!rgba) {
		rgba = r_colorWhite;
	}

	backEnd.color2D[0] = (byte)(rgba[0] * tr.identityLightByte);
	backEnd.color2D[1] = (byte)(rgba[1] * tr.identityLightByte);
	backEnd.color2D[2] = (byte)(rgba[2] * tr.identityLightByte);
	backEnd.color2D[3] = (byte)(rgba[3] * 255.0);
	qglColor4ubv(backEnd.color2D);
#else
	RE_SetColor(rgba);
#endif
}

/*
================
Draw_StretchPic
================
*/
static void Draw_StretchPic_Exec(float x, float y, float w, float h, float s1, float t1, float s2, float t2, qhandle_t hShader) {
#if 1
	shader_t* shader;


	if (hShader) {
		shader = R_GetShaderByHandle(hShader);
	}
	else {
		shader = tr.defaultShader;
	}

	if (w <= 0) {
		w = shader->unfoggedStages[0]->bundle[0].image[0]->width;
		h = shader->unfoggedStages[0]->bundle[0].image[0]->height;
	}

	// draw the pic
	RB_Color4f(backEnd.color2D[0], backEnd.color2D[1], backEnd.color2D[2], backEnd.color2D[3]);
	RB_BeginSurface(shader);

	RB_Texcoord2f(s1, t1);
	RB_Vertex2f(x, y);

	RB_Texcoord2f(s2, t1);
	RB_Vertex2f(x + w, y);

	RB_Texcoord2f(s1, t2);
	RB_Vertex2f(x, y + h);

	RB_Texcoord2f(s2, t2);
	RB_Vertex2f(x + w, y + h);

	RB_StreamEnd();
#else
	RE_StretchPic(x, y, w, h, s1, t1, s2, t2, hShader);
#endif
}

/*
================
Draw_StretchPic2
================
*/
static void Draw_StretchPic2_Exec(float x, float y, float w, float h, float s1, float t1, float s2, float t2, float sx, float sy, qhandle_t hShader) {
	shader_t* shader;
	float halfWidth, halfHeight;
	float scaledWidth1, scaledHeight1;
	float scaledWidth2, scaledHeight2;


	if (hShader) {
		shader = R_GetShaderByHandle(hShader);
	}
	else {
		shader = tr.defaultShader;
	}

	if (w <= 0) {
		w = shader->unfoggedStages[0]->bundle[0].image[0]->width;
		h = shader->unfoggedStages[0]->bundle[0].image[0]->height;
	}

	halfWidth = w * 0.5f;
	halfHeight = h * 0.5f;
	scaledWidth1 = halfWidth * sy;
	scaledHeight1 = halfHeight * sx;
	scaledWidth2 = halfWidth * sx;
	scaledHeight2 = halfHeight * sy;

	// draw the pic
	RB_Color4f(backEnd.color2D[0], backEnd.color2D[1], backEnd.color2D[2], backEnd.color2D[3]);
	RB_BeginSurface(shader);

	RB_Texcoord2f(s1, t1);
	RB_Vertex3f(x + halfWidth - (scaledWidth2 + -scaledHeight2), y + halfWidth - scaledHeight1 - scaledWidth1, 0);

	RB_Texcoord2f(t2, t1);
	RB_Vertex3f(scaledWidth2 - -scaledHeight2 + x + halfWidth, scaledWidth1 - scaledHeight1 + y + halfWidth, 0);

	RB_Texcoord2f(s1, s2);
	RB_Vertex3f(x+ halfWidth - (scaledWidth2 + scaledHeight2), scaledHeight1 - scaledWidth1 + y + halfWidth, 0);

	RB_Texcoord2f(t2, s2);
	RB_Vertex3f(scaledWidth2 - scaledHeight2 + x + halfWidth, scaledWidth1 + scaledHeight1 + y + halfWidth, 0);

	RB_StreamEnd();
}


/*
================
Draw_TilePic
================
*/
static void Draw_TilePic_Exec(float x, float y, float w, float h, qhandle_t hShader) {
	shader_t* shader;
	float		picw, pich;


	if (hShader) {
		shader = R_GetShaderByHandle(hShader);
	}
	else {
		shader = tr.defaultShader;
	}

	if (w <= 0) {
		w = shader->unfoggedStages[0]->bundle[0].image[0]->width;
		h = shader->unfoggedStages[0]->bundle[0].image[0]->height;
	}

	picw = shader->unfoggedStages[0]->bundle[0].image[0]->uploadWidth;
	pich = shader->unfoggedStages[0]->bundle[0].image[0]->uploadHeight;

	// draw the pic
	RB_Color4f(backEnd.color2D[0], backEnd.color2D[1], backEnd.color2D[2], backEnd.color2D[3]);

	RB_StreamBegin(shader);

	RB_Texcoord2f(x / picw, y / pich);
	RB_Vertex2f(x, y);

	RB_Texcoord2f((x + w) / picw, y / pich);
	RB_Vertex2f(x + w, y);

	RB_Texcoord2f(x / picw, (y + h) / pich);
	RB_Vertex2f(x, y + h);

	RB_Texcoord2f((x + w) / picw, (y + h) / pich);
	RB_Vertex2f(x + w, y + h);

	RB_StreamEnd();
}

/*
================
Draw_TilePicOffset
================
*/
static void Draw_TilePicOffset_Exec(float x, float y, float w, float h, qhandle_t hShader, int offsetX, int offsetY) {
	shader_t* shader;
	float		picw, pich;


	if (hShader) {
		shader = R_GetShaderByHandle(hShader);
	}
	else {
		shader = tr.defaultShader;
	}

	if (w <= 0) {
		w = shader->unfoggedStages[0]->bundle[0].image[0]->width;
		h = shader->unfoggedStages[0]->bundle[0].image[0]->height;
	}

	picw = shader->unfoggedStages[0]->bundle[0].image[0]->uploadWidth;
	pich = shader->unfoggedStages[0]->bundle[0].image[0]->uploadHeight;

	// draw the pic
	RB_Color4f(backEnd.color2D[0], backEnd.color2D[1], backEnd.color2D[2], backEnd.color2D[3]);

	RB_StreamBegin(shader);

	RB_Texcoord2f(x / picw, y / pich);
	RB_Vertex2f(x + offsetX, y + offsetY);

	RB_Texcoord2f((x + w) / picw, y / pich);
	RB_Vertex2f(x + offsetX + w, y + offsetY);

	RB_Texcoord2f(x / picw, (y + h) / pich);
	RB_Vertex2f(x + offsetX, y + offsetY + h);

	RB_Texcoord2f((x + w) / picw, (y + h) / pich);
	RB_Vertex2f(x + offsetX + w, y + offsetY + h);

	RB_StreamEnd();
}

/*
================
Draw_TrianglePic
================
*/
static void Draw_TrianglePic_Exec(const vec2_t vPoints[3], const vec2_t vTexCoords[3], qhandle_t hShader) {
	int			i;
	shader_t* shader;


	if (hShader) {
		shader = R_GetShaderByHandle(hShader);
	}
	else {
		shader = tr.defaultShader;
	}

	// draw the pic
	RB_Color4f(backEnd.color2D[0], backEnd.color2D[1], backEnd.color2D[2], backEnd.color2D[3]);

	RB_BeginSurface(shader);

	for (i = 0; i < 3; i++) {
		RB_Texcoord2f(vTexCoords[i][0], vTexCoords[i][1]);
		RB_Vertex2f(vPoints[i][0], vPoints[i][1]);
	}

	RB_StreamEnd();
}

/*
================
RE_DrawBackground_TexSubImage
================
*/
void RE_DrawBackground_TexSubImage(int cols, int rows, int bgr, byte* data) {
	GLenum	format;
	int		w, h;

	w = glConfig.vidWidth;
	h = glConfig.vidHeight;

	R_IssuePendingRenderCommands();
	qglFinish();

	if (bgr) {
		format = GL_BGR_EXT;
	}
	else {
		format = GL_RGB;
	}

	GL_Bind(tr.scratchImage);

	if (cols == tr.scratchImage->width && rows == tr.scratchImage->height && format == tr.scratchImage->internalFormat)
	{
		qglTexSubImage2D(3553, 0, 0, 0, cols, rows, format, 5121, data);
	}
	else
	{
		tr.scratchImage->uploadWidth = cols;
		tr.scratchImage->uploadHeight = rows;
		tr.scratchImage->internalFormat = format;
		qglTexImage2D(GL_TEXTURE_2D, 0, 3, cols, rows, 0, format, 5121, data);
		qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, 9729.0);
		qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, 9729.0);
	}

	qglDisable(GL_CULL_FACE);
	qglDisable(GL_DEPTH_TEST);
	qglEnable(GL_TEXTURE_2D);

	qglBegin(GL_QUADS);

	qglTexCoord2f(0.5 / (GLfloat)cols, ((GLfloat)rows - 0.5) / rows);
	qglVertex2f(0, 0);

	qglTexCoord2f(((GLfloat)cols - 0.5) / cols, ((GLfloat)rows - 0.5) / rows);
	qglVertex2f(w, 0);

	qglTexCoord2f(((GLfloat)cols - 0.5) / cols, 0.5 / (GLfloat)rows);
	qglVertex2f(w, h);

	qglTexCoord2f(0.5 / (GLfloat)rows, 0.5 / (GLfloat)rows);
	qglVertex2f(0, h);

	qglEnd();
}

/*
================
RE_DrawBackground_DrawPixels
================
*/
void RE_DrawBackground_DrawPixels(int cols, int rows, int bgr, byte* data) {
	R_IssuePendingRenderCommands();

	GL_State(0);
	qglDisable(GL_TEXTURE_2D);

	qglPixelZoom(glConfig.vidWidth / rows, glConfig.vidHeight / cols);

	if (bgr) {
		qglDrawPixels(cols, rows, GL_BGR, GL_UNSIGNED_BYTE, data);
	} else {
		qglDrawPixels(cols, rows, GL_RGB, GL_UNSIGNED_BYTE, data);
	}

	qglPixelZoom(1.0, 1.0);

	qglEnable(GL_TEXTURE_2D);
}

/*
================
AddBox
================
*/
static void AddBox_Exec(float x, float y, float w, float h) {

	qglColor4ubv(backEnd.color2D);
	qglDisable(GL_TEXTURE_2D);
	GL_State(GLS_DEPTHTEST_DISABLE | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE);

	qglBegin(GL_QUADS);

	qglVertex2f(x, y);
	qglVertex2f(x + w, y);
	qglVertex2f(x + w, y + h);
	qglVertex2f(x, y + h);

	qglEnd();

	qglEnable(GL_TEXTURE_2D);
}

/*
================
DrawBox
================
*/
static void DrawBox_Exec(float x, float y, float w, float h) {

	qglColor4ubv(backEnd.color2D);
	qglDisable(GL_TEXTURE_2D);
	GL_State(GLS_DEPTHTEST_DISABLE | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA | GLS_SRCBLEND_SRC_ALPHA);

	qglBegin(GL_QUADS);

	qglVertex2f(x, y);
	qglVertex2f(x + w, y);
	qglVertex2f(x + w, y + h);
	qglVertex2f(x, y + h);

	qglEnd();

	qglEnable(GL_TEXTURE_2D);
}

/*
================
DrawLineLoop
================
*/
static void DrawLineLoop_Exec(const vec2_t* points, int count, int stipple_factor, int stipple_mask) {
	int		i;


	qglDisable(GL_TEXTURE_2D);

	if (stipple_factor) {
		qglEnable(GL_LINE_STIPPLE);
		qglLineStipple(stipple_factor, stipple_mask);
	}

	qglBegin(GL_LINE_LOOP);

	for (i = 0; i < count; i++) {
		qglVertex2f(points[i][0], points[i][1]);
	}

	qglEnd();

	qglEnable(GL_TEXTURE_2D);

	if (stipple_factor) {
		qglDisable(GL_LINE_STIPPLE);
	}
}

/*
================
Set2DWindow
================
*/
static void Set2DWindow_Exec(int x, int y, int w, int h, float left, float right, float bottom, float top, float n, float f) {
	/* IMPORTANT: do NOT #ifndef __vita__ this out.
	 *
	 * Earlier I (FH) hypothesised that vitaGL captures GL state per-draw,
	 * so flushing here just to switch from frustum to ortho projection
	 * was redundant on Vita — and the skip gave a real ~4-5% FPS win.
	 *
	 * In practice this was wrong: with the flush skipped, the projection
	 * matrix change (qglMatrixMode/LoadIdentity/Ortho below) is not
	 * applied before the 2D draws that follow. The crosshair and fade
	 * end up rendered with the 3D camera frustum projection still active,
	 * which clips them to nothing on screen — silent invisibility, no
	 * GL error, no log. We pay the flush cost to keep 2D actually
	 * visible.
	 *
	 * (2026-09-25: the 2D calls are now render commands executed in order by the
	 * backend, so the 3D scene queued before this has always run by the time this
	 * executes; the flush only remains in the unqueued build, in the wrapper.) */
	qglViewport(x, y, w, h);
	qglScissor(x, y, w, h);
	qglMatrixMode(GL_PROJECTION);
	qglLoadIdentity();
	qglOrtho(left, right, bottom, top, n, f);
	qglMatrixMode(GL_MODELVIEW);

	qglLoadIdentity();
	GL_State(GLS_DEPTHTEST_DISABLE | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA | GLS_SRCBLEND_SRC_ALPHA);
	qglEnable(GL_BLEND);
	qglDisable(GL_CULL_FACE);
	qglDisable(GL_CLIP_PLANE0);
	// Make sure to disable the fog to avoid messing up with the UI
	qglDisable(GL_FOG);
	qglFogf(GL_FOG_START, 0);

	if (r_reset_tc_array->integer) {
		qglDisableClientState(GL_TEXTURE_COORD_ARRAY);
	}

	if (!backEnd.in2D)
	{
		backEnd.refdef.time = ri.Milliseconds();
		backEnd.in2D = qtrue;
		backEnd.refdef.floatTime = backEnd.refdef.time / 1000.0;
        backEnd.shaderStartTime = 0; 
	}
}

/*
================
RE_Scissor
================
*/
static void RE_Scissor_Exec(int x, int y, int width, int height) {
	qglEnable(GL_SCISSOR_TEST);
	qglScissor(x, y, width, height);
}

/*
================
Set2DInitialShaderTime
================
*/
static void Set2DInitialShaderTime_Exec(float startTime)
{
	if (backEnd.in2D)
	{
		backEnd.shaderStartTime = startTime;
	}
}

/*
=============================================================================

2D DRAWING ENTRY POINTS (refexport)

With R_QUEUE_2D each call is recorded as an RC_DRAW_2D command and runs in order
in the backend (RB_Draw2D). Without it, the original behaviour: flush the queued
commands, then draw immediately on the calling thread.

=============================================================================
*/

#ifdef R_QUEUE_2D
draw2DCommand_t *R_Queue2DCommand(int op, int payloadBytes) {
	draw2DCommand_t *cmd;

	if (!tr.registered) {
		return NULL;
	}
	cmd = (draw2DCommand_t *)R_GetCommandBuffer(sizeof(*cmd) + payloadBytes);
	if (!cmd) {
		return NULL;
	}
	cmd->commandId = RC_DRAW_2D;
	cmd->op = op;
	cmd->payload = payloadBytes;
	return cmd;
}

const void *RB_Draw2D(const void *data) {
	const draw2DCommand_t *cmd = (const draw2DCommand_t *)data;
	const float *f = cmd->f;
	const int *i = cmd->i;

	switch (cmd->op) {
	case D2_SETCOLOR:
		Draw_SetColor_Exec(f);
		break;
	case D2_STRETCHPIC:
		Draw_StretchPic_Exec(f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7], cmd->hShader);
		break;
	case D2_STRETCHPIC2:
		Draw_StretchPic2_Exec(f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7], f[8], f[9], cmd->hShader);
		break;
	case D2_TILEPIC:
		Draw_TilePic_Exec(f[0], f[1], f[2], f[3], cmd->hShader);
		break;
	case D2_TILEPICOFFSET:
		Draw_TilePicOffset_Exec(f[0], f[1], f[2], f[3], cmd->hShader, i[0], i[1]);
		break;
	case D2_TRIANGLEPIC:
		Draw_TrianglePic_Exec((const vec2_t *)&f[0], (const vec2_t *)&f[6], cmd->hShader);
		break;
	case D2_ADDBOX:
		AddBox_Exec(f[0], f[1], f[2], f[3]);
		break;
	case D2_DRAWBOX:
		DrawBox_Exec(f[0], f[1], f[2], f[3]);
		break;
	case D2_LINELOOP:
		DrawLineLoop_Exec((const vec2_t *)(cmd + 1), i[0], i[1], i[2]);
		break;
	case D2_SET2DWINDOW:
		Set2DWindow_Exec(i[0], i[1], i[2], i[3], f[0], f[1], f[2], f[3], f[4], f[5]);
		break;
	case D2_SCISSOR:
		RE_Scissor_Exec(i[0], i[1], i[2], i[3]);
		break;
	case D2_SHADERTIME:
		Set2DInitialShaderTime_Exec(f[0]);
		break;
	case D2_STRING:
		R_DrawString_sgl_Exec(cmd);
		break;
	}

	return (const byte *)(cmd + 1) + cmd->payload;
}
#endif

void Draw_SetColor(const vec4_t rgba) {
#ifdef R_QUEUE_2D
	draw2DCommand_t *cmd = R_Queue2DCommand(D2_SETCOLOR, 0);
	if (!cmd) return;
	if (!rgba) {
		rgba = r_colorWhite;
	}
	Vector4Copy(rgba, cmd->f);
#else
	Draw_SetColor_Exec(rgba);
#endif
}

void Draw_StretchPic(float x, float y, float w, float h, float s1, float t1, float s2, float t2, qhandle_t hShader) {
#ifdef R_QUEUE_2D
	draw2DCommand_t *cmd = R_Queue2DCommand(D2_STRETCHPIC, 0);
	if (!cmd) return;
	cmd->f[0] = x; cmd->f[1] = y; cmd->f[2] = w; cmd->f[3] = h;
	cmd->f[4] = s1; cmd->f[5] = t1; cmd->f[6] = s2; cmd->f[7] = t2;
	cmd->hShader = hShader;
#else
	R_IssuePendingRenderCommands();
	Draw_StretchPic_Exec(x, y, w, h, s1, t1, s2, t2, hShader);
#endif
}

void Draw_StretchPic2(float x, float y, float w, float h, float s1, float t1, float s2, float t2, float sx, float sy, qhandle_t hShader) {
#ifdef R_QUEUE_2D
	draw2DCommand_t *cmd = R_Queue2DCommand(D2_STRETCHPIC2, 0);
	if (!cmd) return;
	cmd->f[0] = x; cmd->f[1] = y; cmd->f[2] = w; cmd->f[3] = h;
	cmd->f[4] = s1; cmd->f[5] = t1; cmd->f[6] = s2; cmd->f[7] = t2;
	cmd->f[8] = sx; cmd->f[9] = sy;
	cmd->hShader = hShader;
#else
	R_IssuePendingRenderCommands();
	Draw_StretchPic2_Exec(x, y, w, h, s1, t1, s2, t2, sx, sy, hShader);
#endif
}

void Draw_TilePic(float x, float y, float w, float h, qhandle_t hShader) {
#ifdef R_QUEUE_2D
	draw2DCommand_t *cmd = R_Queue2DCommand(D2_TILEPIC, 0);
	if (!cmd) return;
	cmd->f[0] = x; cmd->f[1] = y; cmd->f[2] = w; cmd->f[3] = h;
	cmd->hShader = hShader;
#else
	R_IssuePendingRenderCommands();
	Draw_TilePic_Exec(x, y, w, h, hShader);
#endif
}

void Draw_TilePicOffset(float x, float y, float w, float h, qhandle_t hShader, int offsetX, int offsetY) {
#ifdef R_QUEUE_2D
	draw2DCommand_t *cmd = R_Queue2DCommand(D2_TILEPICOFFSET, 0);
	if (!cmd) return;
	cmd->f[0] = x; cmd->f[1] = y; cmd->f[2] = w; cmd->f[3] = h;
	cmd->i[0] = offsetX; cmd->i[1] = offsetY;
	cmd->hShader = hShader;
#else
	R_IssuePendingRenderCommands();
	Draw_TilePicOffset_Exec(x, y, w, h, hShader, offsetX, offsetY);
#endif
}

void Draw_TrianglePic(const vec2_t vPoints[3], const vec2_t vTexCoords[3], qhandle_t hShader) {
#ifdef R_QUEUE_2D
	int k;
	draw2DCommand_t *cmd = R_Queue2DCommand(D2_TRIANGLEPIC, 0);
	if (!cmd) return;
	for (k = 0; k < 3; k++) {
		cmd->f[k * 2] = vPoints[k][0];
		cmd->f[k * 2 + 1] = vPoints[k][1];
		cmd->f[6 + k * 2] = vTexCoords[k][0];
		cmd->f[6 + k * 2 + 1] = vTexCoords[k][1];
	}
	cmd->hShader = hShader;
#else
	R_IssuePendingRenderCommands();
	Draw_TrianglePic_Exec(vPoints, vTexCoords, hShader);
#endif
}

void AddBox(float x, float y, float w, float h) {
#ifdef R_QUEUE_2D
	draw2DCommand_t *cmd = R_Queue2DCommand(D2_ADDBOX, 0);
	if (!cmd) return;
	cmd->f[0] = x; cmd->f[1] = y; cmd->f[2] = w; cmd->f[3] = h;
#else
	R_IssuePendingRenderCommands();
	AddBox_Exec(x, y, w, h);
#endif
}

void DrawBox(float x, float y, float w, float h) {
#ifdef R_QUEUE_2D
	draw2DCommand_t *cmd = R_Queue2DCommand(D2_DRAWBOX, 0);
	if (!cmd) return;
	cmd->f[0] = x; cmd->f[1] = y; cmd->f[2] = w; cmd->f[3] = h;
#else
	R_IssuePendingRenderCommands();
	DrawBox_Exec(x, y, w, h);
#endif
}

void DrawLineLoop(const vec2_t* points, int count, int stipple_factor, int stipple_mask) {
#ifdef R_QUEUE_2D
	draw2DCommand_t *cmd;
	if (count <= 0) return;
	cmd = R_Queue2DCommand(D2_LINELOOP, count * sizeof(vec2_t));
	if (!cmd) return;
	Com_Memcpy(cmd + 1, points, count * sizeof(vec2_t));
	cmd->i[0] = count; cmd->i[1] = stipple_factor; cmd->i[2] = stipple_mask;
#else
	R_IssuePendingRenderCommands();
	DrawLineLoop_Exec(points, count, stipple_factor, stipple_mask);
#endif
}

void Set2DWindow(int x, int y, int w, int h, float left, float right, float bottom, float top, float n, float f) {
#ifdef R_QUEUE_2D
	draw2DCommand_t *cmd = R_Queue2DCommand(D2_SET2DWINDOW, 0);
	if (!cmd) return;
	cmd->i[0] = x; cmd->i[1] = y; cmd->i[2] = w; cmd->i[3] = h;
	cmd->f[0] = left; cmd->f[1] = right; cmd->f[2] = bottom; cmd->f[3] = top;
	cmd->f[4] = n; cmd->f[5] = f;
#else
	R_IssuePendingRenderCommands();
	Set2DWindow_Exec(x, y, w, h, left, right, bottom, top, n, f);
#endif
}

void RE_Scissor(int x, int y, int width, int height) {
#ifdef R_QUEUE_2D
	draw2DCommand_t *cmd = R_Queue2DCommand(D2_SCISSOR, 0);
	if (!cmd) return;
	cmd->i[0] = x; cmd->i[1] = y; cmd->i[2] = width; cmd->i[3] = height;
#else
	RE_Scissor_Exec(x, y, width, height);
#endif
}

void Set2DInitialShaderTime(float startTime) {
#ifdef R_QUEUE_2D
	draw2DCommand_t *cmd = R_Queue2DCommand(D2_SHADERTIME, 0);
	if (!cmd) return;
	cmd->f[0] = startTime;
#else
	Set2DInitialShaderTime_Exec(startTime);
#endif
}
