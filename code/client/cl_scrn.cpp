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
// cl_scrn.c -- master for refresh, status bar, console, chat, notify, etc

#include "client.h"
#include "cl_ui.h"

qboolean	scr_initialized;		// ready to draw
stereoFrame_t	s_scr_stereoFrame;

cvar_t		*cl_timegraph;
cvar_t		*cl_debuggraph;
cvar_t		*cl_graphheight;
cvar_t		*cl_graphscale;
cvar_t		*cl_graphshift;

/*
================
SCR_DrawNamedPic

Coordinates are 640*480 virtual values
=================
*/
void SCR_DrawNamedPic( float x, float y, float width, float height, const char *picname ) {
	qhandle_t	hShader;

	assert( width != 0 );

	hShader = re.RegisterShader( picname );
	SCR_AdjustFrom640( &x, &y, &width, &height );
	re.DrawStretchPic( x, y, width, height, 0, 0, 1, 1, hShader );
}


/*
================
SCR_AdjustFrom640

Adjusted for resolution and screen aspect ratio
================
*/
void SCR_AdjustFrom640( float *x, float *y, float *w, float *h ) {
	float	xscale;
	float	yscale;

#if 0
		// adjust for wide screens
		if ( cls.glconfig.vidWidth * 480 > cls.glconfig.vidHeight * 640 ) {
			*x += 0.5 * ( cls.glconfig.vidWidth - ( cls.glconfig.vidHeight * 640 / 480 ) );
		}
#endif

	// scale for screen sizes
	xscale = cls.glconfig.vidWidth / 640.0;
	yscale = cls.glconfig.vidHeight / 480.0;
	if ( x ) {
		*x *= xscale;
	}
	if ( y ) {
		*y *= yscale;
	}
	if ( w ) {
		*w *= xscale;
	}
	if ( h ) {
		*h *= yscale;
	}
}

/*
================
SCR_FillRect

Coordinates are 640*480 virtual values
=================
*/
void SCR_FillRect( float x, float y, float width, float height, const float *color ) {
	re.SetColor( color );

	SCR_AdjustFrom640( &x, &y, &width, &height );
	re.DrawStretchPic( x, y, width, height, 0, 0, 0, 0, cls.whiteShader );

	re.SetColor( NULL );
}


/*
================
SCR_DrawPic

Coordinates are 640*480 virtual values
=================
*/
void SCR_DrawPic( float x, float y, float width, float height, qhandle_t hShader ) {
	SCR_AdjustFrom640( &x, &y, &width, &height );
	re.DrawStretchPic( x, y, width, height, 0, 0, 1, 1, hShader );
}



/*
** SCR_DrawChar
** chars are drawn at 640*480 virtual screen size
*/
static void SCR_DrawChar( int x, int y, float size, int ch ) {
	int row, col;
	float frow, fcol;
	float	ax, ay, aw, ah;

	ch &= 255;

	if ( ch == ' ' ) {
		return;
	}

	if ( y < -size ) {
		return;
	}

	ax = x;
	ay = y;
	aw = size;
	ah = size;
	SCR_AdjustFrom640( &ax, &ay, &aw, &ah );

	row = ch>>4;
	col = ch&15;

	frow = row*0.0625;
	fcol = col*0.0625;
	size = 0.0625;

	re.DrawStretchPic( ax, ay, aw, ah,
					   fcol, frow, 
					   fcol + size, frow + size, 
					   cls.charSetShader );
}

/*
** SCR_DrawSmallChar
** small chars are drawn at native screen resolution
*/
void SCR_DrawSmallChar( int x, int y, int ch ) {
	int row, col;
	float frow, fcol;
	float size;

	ch &= 255;

	if ( ch == ' ' ) {
		return;
	}

	if ( y < -SMALLCHAR_HEIGHT ) {
		return;
	}

	row = ch>>4;
	col = ch&15;

	frow = row*0.0625;
	fcol = col*0.0625;
	size = 0.0625;

	re.DrawStretchPic( x, y, SMALLCHAR_WIDTH, SMALLCHAR_HEIGHT,
					   fcol, frow, 
					   fcol + size, frow + size, 
					   cls.charSetShader );
}


/*
==================
SCR_DrawBigString[Color]

Draws a multi-colored string with a drop shadow, optionally forcing
to a fixed color.

Coordinates are at 640 by 480 virtual resolution
==================
*/
void SCR_DrawStringExt( int x, int y, float size, const char *string, float *setColor, qboolean forceColor,
		qboolean noColorEscape ) {
	vec4_t		color;
	const char	*s;
	int			xx;

	// draw the drop shadow
	color[0] = color[1] = color[2] = 0;
	color[3] = setColor[3];
	re.SetColor( color );
	s = string;
	xx = x;
	while ( *s ) {
		if ( !noColorEscape && Q_IsColorString( s ) ) {
			s += 2;
			continue;
		}
		SCR_DrawChar( xx+2, y+2, size, *s );
		xx += size;
		s++;
	}


	// draw the colored text
	s = string;
	xx = x;
	re.SetColor( setColor );
	while ( *s ) {
		if ( Q_IsColorString( s ) ) {
			if ( !forceColor ) {
				Com_Memcpy( color, g_color_table[ColorIndex(*(s+1))], sizeof( color ) );
				color[3] = setColor[3];
				re.SetColor( color );
			}
			if ( !noColorEscape ) {
				s += 2;
				continue;
			}
		}
		SCR_DrawChar( xx, y, size, *s );
		xx += size;
		s++;
	}
	re.SetColor( NULL );
}


void SCR_DrawBigString( int x, int y, const char *s, float alpha, qboolean noColorEscape ) {
	float	color[4];

	color[0] = color[1] = color[2] = 1.0;
	color[3] = alpha;
	SCR_DrawStringExt( x, y, BIGCHAR_WIDTH, s, color, qfalse, noColorEscape );
}

void SCR_DrawBigStringColor( int x, int y, const char *s, vec4_t color, qboolean noColorEscape ) {
	SCR_DrawStringExt( x, y, BIGCHAR_WIDTH, s, color, qtrue, noColorEscape );
}


/*
==================
SCR_DrawSmallString[Color]

Draws a multi-colored string with a drop shadow, optionally forcing
to a fixed color.
==================
*/
void SCR_DrawSmallStringExt( int x, int y, const char *string, float *setColor, qboolean forceColor,
		qboolean noColorEscape ) {
	vec4_t		color;
	const char	*s;
	int			xx;

	// draw the colored text
	s = string;
	xx = x;
	re.SetColor( setColor );
	while ( *s ) {
		if ( Q_IsColorString( s ) ) {
			if ( !forceColor ) {
				Com_Memcpy( color, g_color_table[ColorIndex(*(s+1))], sizeof( color ) );
				color[3] = setColor[3];
				re.SetColor( color );
			}
			if ( !noColorEscape ) {
				s += 2;
				continue;
			}
		}
		SCR_DrawSmallChar( xx, y, *s );
		xx += SMALLCHAR_WIDTH;
		s++;
	}
	re.SetColor( NULL );
}



/*
** SCR_Strlen -- skips color escape codes
*/
static int SCR_Strlen( const char *str ) {
	const char *s = str;
	int count = 0;

	while ( *s ) {
		if ( Q_IsColorString( s ) ) {
			s += 2;
		} else {
			count++;
			s++;
		}
	}

	return count;
}

/*
** SCR_GetBigStringWidth
*/ 
int	SCR_GetBigStringWidth( const char *str ) {
	return SCR_Strlen( str ) * BIGCHAR_WIDTH;
}


//===============================================================================

/*
=================
SCR_DrawDemoRecording
=================
*/
void SCR_DrawDemoRecording( void ) {
	char	string[1024];
	int		pos;

	if ( !clc.demorecording ) {
		return;
	}
	if ( clc.spDemoRecording ) {
		return;
	}

	pos = FS_FTell( clc.demofile );
	Com_sprintf( string, sizeof( string ), "RECORDING %s: %ik", clc.demoName, pos / 1024 );

	SCR_DrawStringExt( 320 - strlen( string ) * 4, 20, 8, string, g_color_table[7], qtrue, qfalse );
}


/*
===============================================================================

DEBUG GRAPH

===============================================================================
*/

static	int			current;
static	float		values[1024];

/*
==============
SCR_DebugGraph
==============
*/
void SCR_DebugGraph (float value)
{
	values[current] = value;
	current = (current + 1) % ARRAY_LEN(values);
}

/*
==============
SCR_DrawDebugGraph
==============
*/
void SCR_DrawDebugGraph (void)
{
	int		a, x, y, w, i, h;
	float	v;

	//
	// draw the graph
	//
	w = cls.glconfig.vidWidth;
	x = 0;
	y = cls.glconfig.vidHeight;
	re.SetColor( g_color_table[0] );
	re.DrawStretchPic(x, y - cl_graphheight->integer, 
		w, cl_graphheight->integer, 0, 0, 0, 0, cls.whiteShader );
	re.SetColor( NULL );

	for (a=0 ; a<w ; a++)
	{
		i = (ARRAY_LEN(values)+current-1-(a % ARRAY_LEN(values))) % ARRAY_LEN(values);
		v = values[i];
		v = v * cl_graphscale->integer + cl_graphshift->integer;
		
		if (v < 0)
			v += cl_graphheight->integer * (1+(int)(-v / cl_graphheight->integer));
		h = (int)v % cl_graphheight->integer;
		re.DrawStretchPic( x+w-1-a, y - h, 1, h, 0, 0, 0, 0, cls.whiteShader );
	}
}

//=============================================================================

/*
==================
SCR_Init
==================
*/
void SCR_Init( void ) {
	cl_timegraph = Cvar_Get ("timegraph", "0", CVAR_CHEAT);
	cl_debuggraph = Cvar_Get ("debuggraph", "0", CVAR_CHEAT);
	cl_graphheight = Cvar_Get ("graphheight", "32", CVAR_CHEAT);
	cl_graphscale = Cvar_Get ("graphscale", "1", CVAR_CHEAT);
	cl_graphshift = Cvar_Get ("graphshift", "0", CVAR_CHEAT);

	scr_initialized = qtrue;
}


//=======================================================

/*
==================
SCR_DrawScreenField

This will be called twice if rendering in stereo mode
==================
*/
#ifdef __SWITCH__
/*
=================================================================
Switch on-screen DEV MENU
Toggle: click both analog sticks (L3+R3). Pages via L/R shoulders,
D-pad to navigate, left/right to tweak a value, A to use, B to close.
=================================================================
*/
extern "C" int Switch_PadButtonPressed(int sdlBtn);
extern "C" void Com_WriteConfiguration(void);   /* flush archived cvars to disk */

extern "C" { int cl_devmenu_active = 0; }   /* read by sdl_input to mute the game */
cvar_t *cl_ads_sensitivity = NULL;

typedef enum { DM_CMD, DM_CVARF, DM_CLOSE } dm_itype_t;
typedef struct { const char *label; dm_itype_t type; const char *arg; float step, lo, hi; } dm_item_t;

#define DMAP(n) { n, DM_CMD, "spmap " n, 0, 0, 0 }
static dm_item_t dm_fases[] = {
	DMAP("training"),
	DMAP("m1l1"), DMAP("m1l2a"), DMAP("m1l2b"), DMAP("m1l3a"), DMAP("m1l3b"), DMAP("m1l3c"),
	DMAP("m2l1"), DMAP("m2l2a"), DMAP("m2l2b"), DMAP("m2l2c"), DMAP("m2l3"),
	DMAP("m3l1a"), DMAP("m3l1b"), DMAP("m3l2"), DMAP("m3l3"),
	DMAP("m4l0"), DMAP("m4l1"), DMAP("m4l2"), DMAP("m4l3"),
	DMAP("m5l1a"), DMAP("m5l1b"), DMAP("m5l2a"), DMAP("m5l2b"), DMAP("m5l3"),
	DMAP("m6l1a"), DMAP("m6l1b"), DMAP("m6l1c"), DMAP("m6l2a"), DMAP("m6l2b"),
	DMAP("m6l3a"), DMAP("m6l3b"), DMAP("m6l3c"), DMAP("m6l3d"), DMAP("m6l3e"),
};
static dm_item_t dm_cheats[] = {
	{ "Ativar cheats (ON)",  DM_CMD, "cheats 1", 0,0,0 },
	{ "Desativar cheats",    DM_CMD, "cheats 0", 0,0,0 },
	{ "God mode",            DM_CMD, "god", 0,0,0 },
	{ "Noclip (atravessar)", DM_CMD, "noclip", 0,0,0 },
	{ "Notarget (invisivel)",DM_CMD, "notarget", 0,0,0 },
	{ "Dar TUDO",            DM_CMD, "give all", 0,0,0 },
	{ "Dar municao",         DM_CMD, "give ammo", 0,0,0 },
	{ "Dar saude",           DM_CMD, "give health", 0,0,0 },
};
static dm_item_t dm_sens[] = {
	{ "Sens. SEM mira (hip)", DM_CVARF, "sensitivity",        0.5f,  0.5f, 30.0f },
	{ "Sens. MIRANDO (ADS)",  DM_CVARF, "cl_ads_sensitivity", 0.05f, 0.05f, 2.0f },
	{ "Giro horiz. (yaw)",    DM_CVARF, "j_yaw",             -0.002f,-0.08f,-0.004f },
	{ "Giro vert. (pitch)",   DM_CVARF, "j_pitch",            0.002f, 0.004f, 0.08f },
	{ "Aceleracao do mouse",  DM_CVARF, "cl_mouseAccel",      0.05f, 0.0f, 1.0f },
};
static dm_item_t dm_misc[] = {
	{ "Menu principal",   DM_CMD,   "disconnect", 0,0,0 },
	{ "Reiniciar fase",   DM_CMD,   "restart", 0,0,0 },
	{ "Suicidio (kill)",  DM_CMD,   "kill", 0,0,0 },
	{ "Mostrar FPS",      DM_CVARF, "cg_drawfps", 1, 0, 1 },
	{ "Campo de visao",   DM_CVARF, "cg_fov", 5, 70, 120 },
	{ "Fechar menu",      DM_CLOSE, "", 0,0,0 },
};
typedef struct { const char *title; dm_item_t *items; int count; } dm_page_t;
static dm_page_t dm_pages[] = {
	{ "FASES",         dm_fases,  (int)(sizeof(dm_fases)/sizeof(dm_fases[0])) },
	{ "CHEATS",        dm_cheats, (int)(sizeof(dm_cheats)/sizeof(dm_cheats[0])) },
	{ "SENSIBILIDADE", dm_sens,   (int)(sizeof(dm_sens)/sizeof(dm_sens[0])) },
	{ "MISC",          dm_misc,   (int)(sizeof(dm_misc)/sizeof(dm_misc[0])) },
};
#define DM_NPAGES ((int)(sizeof(dm_pages)/sizeof(dm_pages[0])))

static int dm_page = 0, dm_sel = 0, dm_scroll = 0;
static int dm_prev[20];

void CL_DevMenu_Init(void) {
	cl_ads_sensitivity = Cvar_Get("cl_ads_sensitivity", "0.7", CVAR_ARCHIVE);
}

static int dm_edge(int btn) {
	int now = Switch_PadButtonPressed(btn);
	int e = (now && !dm_prev[btn]);
	dm_prev[btn] = now;
	return e;
}

void CL_DevMenu_Frame(void) {
	int up, down, lsh, rsh, a, b, lf, rt;
	int n, l3;
	dm_page_t *pg;
	dm_item_t *it;
	static int prevL3 = 0;
	static int lastL3click = -100000;

	if (!cl_ads_sensitivity) CL_DevMenu_Init();

	/* Persist archived cvars on the Switch: the HOME button kills us with no
	 * clean Com_Shutdown, so the normal on-exit config write never runs and the
	 * user's settings were lost. Flush every 15s (cheap no-op unless something
	 * actually changed). */
	{
		static int lastCfg = 0;
		if (cls.realtime - lastCfg > 15000) {
			lastCfg = cls.realtime;
			Com_WriteConfiguration();
		}
	}

	/* toggle: double-tap Select / Minus (the (-) button), pressed twice quickly */
	l3 = Switch_PadButtonPressed(4);
	if (l3 && !prevL3) {                       /* L3 rising edge = one click */
		if (cls.realtime - lastL3click < 450) {
			cl_devmenu_active = !cl_devmenu_active;
			lastL3click = -100000;             /* reset so a 3rd click won't re-toggle */
			if (!cl_devmenu_active) {
				Com_WriteConfiguration();      /* persist tweaks immediately on close */
			}
		} else {
			lastL3click = cls.realtime;
		}
	}
	prevL3 = l3;

	if (!cl_devmenu_active) {
		int i;
		for (i = 0; i < 20; i++) dm_prev[i] = Switch_PadButtonPressed(i);
		return;
	}

	/* read every edge once per frame to keep prev fresh */
	up = dm_edge(11); down = dm_edge(12); lsh = dm_edge(9); rsh = dm_edge(10);
	a = dm_edge(0); b = dm_edge(1); lf = dm_edge(13); rt = dm_edge(14);
	dm_edge(7); dm_edge(8);

	if (lsh) { dm_page--; dm_sel = 0; dm_scroll = 0; }
	if (rsh) { dm_page++; dm_sel = 0; dm_scroll = 0; }
	if (dm_page < 0) dm_page = DM_NPAGES - 1;
	if (dm_page >= DM_NPAGES) dm_page = 0;

	pg = &dm_pages[dm_page];
	n = pg->count;
	if (up)   dm_sel--;
	if (down) dm_sel++;
	if (dm_sel < 0) dm_sel = n - 1;
	if (dm_sel >= n) dm_sel = 0;

	it = &pg->items[dm_sel];
	if (it->type == DM_CVARF && (lf || rt)) {
		float v = Cvar_VariableValue(it->arg);
		if (lf) v -= it->step;
		if (rt) v += it->step;
		if (v < it->lo) v = it->lo;
		if (v > it->hi) v = it->hi;
		Cvar_SetValue(it->arg, v);
	}
	if (a) {
		if (it->type == DM_CMD)   Cbuf_AddText(va("%s\n", it->arg));
		else if (it->type == DM_CLOSE) cl_devmenu_active = 0;
	}
	if (b) cl_devmenu_active = 0;

	if (dm_sel < dm_scroll) dm_scroll = dm_sel;
	if (dm_sel >= dm_scroll + 12) dm_scroll = dm_sel - 11;
}

static fontheader_t *dm_font = NULL;

/* MoHAA's SCR_DrawChar uses cls.charSetShader, which this engine never
 * registers -> the chars came out as white blocks. Draw with the real MoHAA
 * font system (re.LoadFont + re.DrawString) instead. Coords stay 640x480
 * virtual; the {w/640,h/480} scale maps them to the real screen. */
static void DM_Text(float x, float y, const char *text, const float *color)
{
	float vs[2];
	if (!dm_font) {
		dm_font = re.LoadFont("verdana-14");
	}
	if (!dm_font) {
		return;
	}
	vs[0] = cls.glconfig.vidWidth / 640.0f;
	vs[1] = cls.glconfig.vidHeight / 480.0f;
	re.SetColor(color);
	re.DrawString(dm_font, text, x, y, -1, vs);
}

void CL_DevMenu_Draw(void) {
	int i, y;
	dm_page_t *pg;
	vec4_t bg     = { 0.03f, 0.04f, 0.08f, 0.94f };
	vec4_t white  = { 0.85f, 0.85f, 0.9f, 1.0f };
	vec4_t yellow = { 1.0f, 0.85f, 0.25f, 1.0f };
	vec4_t gray   = { 0.55f, 0.55f, 0.6f, 1.0f };

	if (!cl_devmenu_active) return;
	pg = &dm_pages[dm_page];

	/* Dark panel via re.DrawBox -- SCR_FillRect uses cls.whiteShader, which this
	 * engine never registers, so it drew a WHITE box that swallowed the light
	 * text. re.DrawBox fills with the re.SetColor color (no shader needed).
	 * Coords are 640x480 virtual -> SCR_AdjustFrom640 maps to the real screen. */
	{
		/* centered: 416x360 panel -> x=(640-416)/2=112, y=(480-360)/2=60 */
		float bx = 112, by = 60, bw = 416, bh = 360;
		SCR_AdjustFrom640(&bx, &by, &bw, &bh);
		re.SetColor(bg);
		re.DrawBox(bx, by, bw, bh);
		re.SetColor(NULL);
	}

	DM_Text(130, 78, va("=== DEV MENU ===   %s   (pag %d/%d)", pg->title, dm_page + 1, DM_NPAGES), yellow);
	DM_Text(130, 100, "L/R:pag  Dpad:nav  </>:ajusta  A:usa  B:fecha", gray);

	y = 130;
	for (i = dm_scroll; i < pg->count && i < dm_scroll + 12; i++) {
		dm_item_t *it = &pg->items[i];
		char line[160];
		if (it->type == DM_CVARF)
			Com_sprintf(line, sizeof(line), "%s  %-22s %.3f", (i == dm_sel) ? ">>" : "  ", it->label, Cvar_VariableValue(it->arg));
		else
			Com_sprintf(line, sizeof(line), "%s  %s", (i == dm_sel) ? ">>" : "  ", it->label);
		DM_Text(142, y, line, (i == dm_sel) ? yellow : white);
		y += 23;
	}
	re.SetColor(NULL);
}
#endif /* __SWITCH__ */

void SCR_DrawScreenField( void ) {
	// wide aspect ratio screens need to have the sides cleared
	// unless they are displaying game renderings
	if ( clc.state != CA_ACTIVE && clc.state != CA_CINEMATIC ) {
		if ( cls.glconfig.vidWidth * 480 > cls.glconfig.vidHeight * 640 ) {
			re.SetColor( g_color_table[0] );
			//re.DrawStretchPic( 0, 0, cls.glconfig.vidWidth, cls.glconfig.vidHeight, 0, 0, 0, 0, cls.whiteShader );
			re.DrawBox( 0, 0, cls.glconfig.vidHeight, cls.glconfig.vidWidth );
			re.SetColor( NULL );
		}
	}

	switch( clc.state ) {
	case CA_UNINITIALIZED:
		Com_Error( ERR_FATAL, "SCR_DrawScreenField: clc.state == CA_UNINITIALIZED" );
		break;
	case CA_LOADING:
	case CA_PRIMED:
		// draw the game information screen and loading progress
		CL_CGameRendering(s_scr_stereoFrame);

		// also draw the connection information, so it doesn't
		// flash away too briefly on local or lan games
		// refresh to update the time
	case CA_CONNECTING:
	case CA_CHALLENGING:
	case CA_CONNECTED:
		// connecting clients will only show the connection dialog
		// refresh to update the time
		UI_DrawConnect();
		break;
	case CA_CINEMATIC:
		SCR_DrawCinematic();
		break;
	case CA_ACTIVE:
		CL_CGameRendering(s_scr_stereoFrame);
		break;
	default:
		break;
	}
}

/*
==================
UpdateStereoSide
==================
*/
void UpdateStereoSide( stereoFrame_t s ) {
	s_scr_stereoFrame = s;
	re.BeginFrame( s );
	if( clc.state == CA_CINEMATIC ) {
		SCR_DrawCinematic();
	}
	UI_Update();

#ifdef __SWITCH__
	/* Draw the dev-menu overlay at the very top, AFTER UI_Update() composites
	 * the game view + HUD + UI. Inside SCR_DrawScreenField it was rendered
	 * within the UIView3D widget's viewport/scissor and got clipped away. */
	CL_DevMenu_Draw();
#endif
}

/*
==================
SCR_SimpleUpdateScreen
==================
*/
void SCR_SimpleUpdateScreen( void ) {
	// if running in stereo, we need to draw the frame twice
	if( cls.glconfig.stereoEnabled ) {
		UpdateStereoSide( STEREO_LEFT );
		UpdateStereoSide( STEREO_RIGHT );
	}
	else {
		UpdateStereoSide( STEREO_CENTER );
	}

	if( com_speeds->integer ) {
		re.EndFrame( &time_frontend, &time_backend );
	}
	else {
		re.EndFrame( NULL, NULL );
	}
}

/*
==================
SCR_UpdateScreen

This is called every frame, and can also be called explicitly to flush
text to the screen.
==================
*/
void SCR_UpdateScreen( void ) {
	static qboolean screen_recursive;

	if ( !scr_initialized ) {
		return;				// not initialized yet
	}

	if (screen_recursive) {
		// already called
		return;
	}

	screen_recursive = qtrue;
	
	CL_StartHunkUsers(qfalse);
	SCR_SimpleUpdateScreen();

	// set the fps value
	if( fps->integer && clc.state == CA_ACTIVE ) {
		static int belowlastplaytime = 0;
		static qboolean belowframerate = qfalse;

		if( currentfps > 19.8 ) {
			belowframerate = qfalse;
		} else if( !belowframerate ) {
			if( currentfps < 18.0 ) {
				belowlastplaytime = 10000;
				belowframerate = qtrue;
			}
		}

		if( belowframerate && cls.realtime > belowlastplaytime ) {
			belowlastplaytime = cls.realtime + 4000;
		}
	}

	screen_recursive = qfalse;
}

