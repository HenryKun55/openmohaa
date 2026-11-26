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
// cl.input.c  -- builds an intended movement command to send to the server

#include "client.h"
#include "cl_ui.h"
#ifdef __vita__
#include "../qcommon/vita_aim_assist.h"

static int vitaTouchX, vitaTouchY;
static struct {
	vitaAimTarget_t target;
	vitaAimLook_t touch, stick;
	float radius, engagement, friction, magnetism;
	vitaAimRotationLimits_t rotationLimits;
	int rotational, sampledMessage, preferredEntity, idleMsec;
} vitaAim;
static cvar_t *vita_aim_assist, *vita_aim_friction, *vita_aim_radius;
static cvar_t *vita_aim_magnetism, *vita_aim_rotational;
static cvar_t *vita_aim_magnetism_gain, *vita_aim_rotational_cap, *vita_aim_rotational_rate;
static cvar_t *vita_aim_debug;
extern qboolean CL_VitaPerfMenu_IsActive( void );

struct VitaAimDebugFrame {
	vitaAimDebug_t game;
	const char *status;
	int touchX, touchY, stickX, stickY, rawTouchX, rawTouchY, emittedX, emittedY, padX, padY;
	int target, serverId;
	vitaAimLook_t touch, stick, before, final, wire;
};
static VitaAimDebugFrame vitaDebugFrame, vitaDebugLastLook;
static bool vitaDebugActive, vitaDebugHaveLook;
static unsigned vitaDebugFrames, vitaDebugQueries, vitaDebugSelected, vitaDebugApplied, vitaDebugEncoded;
static unsigned vitaDebugStickOnly, vitaDebugStickOnlyEncoded;
static int vitaDebugRawTouchX, vitaDebugRawTouchY, vitaDebugEmittedX, vitaDebugEmittedY, vitaDebugPadX, vitaDebugPadY;
#endif

unsigned	frame_msec;
int			old_com_frameTime;

/*
===============================================================================

KEY BUTTONS

Continuous button event tracking is complicated by the fact that two different
input sources (say, mouse button 1 and the control key) can both press the
same button, but the button should only be released when both of the
pressing key have been released.

When a key event issues a button command (+forward, +attackprimary, etc), it appends
its key number as argv(1) so it can be matched up with the release.

argv(2) will be set to the time the event happened, which allows exact
control even at low framerates when the down and up events may both get qued
at the same time.

===============================================================================
*/


kbutton_t	in_left, in_right, in_forward, in_back;
kbutton_t	in_lookup, in_lookdown, in_moveleft, in_moveright;
kbutton_t	in_strafe, in_speed;

qboolean	in_guimouse;

kbutton_t	in_up, in_down;

#ifdef USE_VOIP
kbutton_t	in_voiprecord;
#endif

kbutton_t	in_buttons[16];

qboolean	in_mlooking;

void IN_ToggleMouse( void ) {
	if( in_guimouse )
	{
		IN_MouseOff();
	}
	else
	{
		IN_MouseOn();
	}
}

void IN_MouseOn( void ) {
	if( !in_guimouse )
	{
		for( int k = 0; k <= K_MWHEELUP; k++ )
		{
			if( keys[ k ].down )
			{
				CL_KeyEvent( k, qfalse, 0 );
			}
		}
	}

	in_guimouse = qtrue;
}

void IN_MouseOff( void ) {
	in_guimouse = qfalse;
}

void IN_MLookDown( void ) {
	in_mlooking = qtrue;
}

void IN_MLookUp( void ) {
	in_mlooking = qfalse;
	if ( !cl_freelook->integer ) {
		IN_CenterView ();
	}
}

void IN_KeyDown( kbutton_t *b ) {
	int		k;
	char	*c;

	c = Cmd_Argv(1);
	if ( c[0] ) {
		k = atoi(c);
	} else {
		k = -1;		// typed manually at the console for continuous down
	}

	if ( k == b->down[0] || k == b->down[1] ) {
		return;		// repeating key
	}

	if ( !b->down[0] ) {
		b->down[0] = k;
	} else if ( !b->down[1] ) {
		b->down[1] = k;
	} else {
		Com_Printf ("Three keys down for a button!\n");
		return;
	}

	if ( b->active ) {
		return;		// still down
	}

	// save timestamp for partial frame summing
	c = Cmd_Argv(2);
	b->downtime = atoi(c);

	b->active = qtrue;
	b->wasPressed = qtrue;
}

void IN_KeyUp( kbutton_t *b ) {
	int		k;
	char	*c;
	unsigned	uptime;

	c = Cmd_Argv(1);
	if ( c[0] ) {
		k = atoi(c);
	} else {
		// typed manually at the console, assume for unsticking, so clear all
		b->down[0] = b->down[1] = 0;
		b->active = qfalse;
		return;
	}

	if ( b->down[0] == k ) {
		b->down[0] = 0;
	} else if ( b->down[1] == k ) {
		b->down[1] = 0;
	} else {
		return;		// key up without coresponding down (menu pass through)
	}
	if ( b->down[0] || b->down[1] ) {
		return;		// some other key is still holding it down
	}

	b->active = qfalse;

	// save timestamp for partial frame summing
	c = Cmd_Argv(2);
	uptime = atoi(c);
	if ( uptime ) {
		b->msec += uptime - b->downtime;
	} else {
		b->msec += frame_msec / 2;
	}

	b->active = qfalse;
}



/*
===============
CL_KeyState

Returns the fraction of the frame that the key was down
===============
*/
float CL_KeyState( kbutton_t *key ) {
	float		val;
	int			msec;

	msec = key->msec;
	key->msec = 0;

	if ( key->active ) {
		// still down
		if ( !key->downtime ) {
			msec = com_frameTime;
		} else {
			msec += com_frameTime - key->downtime;
		}
		key->downtime = com_frameTime;
	}

#if 0
	if (msec) {
		Com_Printf ("%i ", msec);
	}
#endif

	val = (float)msec / frame_msec;
	if ( val < 0 ) {
		val = 0;
	}
	if ( val > 1 ) {
		val = 1;
	}

	return val;
}



void IN_UpDown(void) {IN_KeyDown(&in_up);}
void IN_UpUp(void) {IN_KeyUp(&in_up);}
void IN_DownDown(void) {IN_KeyDown(&in_down);}
void IN_DownUp(void) {IN_KeyUp(&in_down);}
void IN_LeftDown(void) {IN_KeyDown(&in_left);}
void IN_LeftUp(void) {IN_KeyUp(&in_left);}
void IN_RightDown(void) {IN_KeyDown(&in_right);}
void IN_RightUp(void) {IN_KeyUp(&in_right);}
void IN_ForwardDown(void) {IN_KeyDown(&in_forward);}
void IN_ForwardUp(void) {IN_KeyUp(&in_forward);}
void IN_BackDown(void) {IN_KeyDown(&in_back);}
void IN_BackUp(void) {IN_KeyUp(&in_back);}
void IN_LookupDown(void) {IN_KeyDown(&in_lookup);}
void IN_LookupUp(void) {IN_KeyUp(&in_lookup);}
void IN_LookdownDown(void) {IN_KeyDown(&in_lookdown);}
void IN_LookdownUp(void) {IN_KeyUp(&in_lookdown);}
void IN_MoveleftDown(void) {IN_KeyDown(&in_moveleft);}
void IN_MoveleftUp(void) {IN_KeyUp(&in_moveleft);}
void IN_MoverightDown(void) {IN_KeyDown(&in_moveright);}
void IN_MoverightUp(void) {IN_KeyUp(&in_moveright);}
void IN_AttackPrimaryDown(void) { IN_KeyDown(&in_buttons[0]);}
void IN_AttackPrimaryUp(void) { IN_KeyUp(&in_buttons[0]); }
void IN_AttackSecondaryDown(void) { IN_KeyDown(&in_buttons[1]); }
void IN_AttackSecondaryUp(void) { IN_KeyUp(&in_buttons[1]); }
void IN_UseDown(void) { IN_KeyDown(&in_buttons[3]); }
void IN_UseUp(void) { IN_KeyUp(&in_buttons[3]); }
void IN_LeanLeftDown(void) { IN_KeyDown(&in_buttons[4]); }
void IN_LeanLeftUp(void) { IN_KeyUp(&in_buttons[4]); }
void IN_LeanRightDown(void) { IN_KeyDown(&in_buttons[5]); }
void IN_LeanRightUp(void) { IN_KeyUp(&in_buttons[5]); }

void IN_SpeedDown(void) {IN_KeyDown(&in_speed);}
void IN_SpeedUp(void) {IN_KeyUp(&in_speed);}
void IN_StrafeDown(void) {IN_KeyDown(&in_strafe);}
void IN_StrafeUp(void) {IN_KeyUp(&in_strafe);}

#ifdef USE_VOIP
void IN_VoipRecordDown(void)
{
	IN_KeyDown(&in_voiprecord);
	Cvar_Set("cl_voipSend", "1");
}

void IN_VoipRecordUp(void)
{
	IN_KeyUp(&in_voiprecord);
	Cvar_Set("cl_voipSend", "0");
}
#endif

void IN_Button0Down(void) {IN_KeyDown(&in_buttons[0]);}
void IN_Button0Up(void) {IN_KeyUp(&in_buttons[0]);}
void IN_Button1Down(void) {IN_KeyDown(&in_buttons[1]);}
void IN_Button1Up(void) {IN_KeyUp(&in_buttons[1]);}
void IN_Button2Down(void) {IN_KeyDown(&in_buttons[2]);}
void IN_Button2Up(void) {IN_KeyUp(&in_buttons[2]);}
void IN_Button3Down(void) {IN_KeyDown(&in_buttons[3]);}
void IN_Button3Up(void) {IN_KeyUp(&in_buttons[3]);}
void IN_Button4Down(void) {IN_KeyDown(&in_buttons[4]);}
void IN_Button4Up(void) {IN_KeyUp(&in_buttons[4]);}
void IN_Button5Down(void) {IN_KeyDown(&in_buttons[5]);}
void IN_Button5Up(void) {IN_KeyUp(&in_buttons[5]);}
void IN_Button6Down(void) {IN_KeyDown(&in_buttons[6]);}
void IN_Button6Up(void) {IN_KeyUp(&in_buttons[6]);}
void IN_Button7Down(void) {IN_KeyDown(&in_buttons[7]);}
void IN_Button7Up(void) {IN_KeyUp(&in_buttons[7]);}
void IN_Button8Down(void) {IN_KeyDown(&in_buttons[8]);}
void IN_Button8Up(void) {IN_KeyUp(&in_buttons[8]);}
void IN_Button9Down(void) {IN_KeyDown(&in_buttons[9]);}
void IN_Button9Up(void) {IN_KeyUp(&in_buttons[9]);}
void IN_Button10Down(void) {IN_KeyDown(&in_buttons[10]);}
void IN_Button10Up(void) {IN_KeyUp(&in_buttons[10]);}
void IN_Button11Down(void) {IN_KeyDown(&in_buttons[11]);}
void IN_Button11Up(void) {IN_KeyUp(&in_buttons[11]);}
void IN_Button12Down(void) {IN_KeyDown(&in_buttons[12]);}
void IN_Button12Up(void) {IN_KeyUp(&in_buttons[12]);}
void IN_Button13Down(void) {IN_KeyDown(&in_buttons[13]);}
void IN_Button13Up(void) {IN_KeyUp(&in_buttons[13]);}
void IN_Button14Down(void) {IN_KeyDown(&in_buttons[14]);}
void IN_Button14Up(void) {IN_KeyUp(&in_buttons[14]);}
void IN_Button15Down(void) {IN_KeyDown(&in_buttons[15]);}
void IN_Button15Up(void) {IN_KeyUp(&in_buttons[15]);}

void IN_ButtonDown (void) {
	IN_KeyDown(&in_buttons[1]);}
void IN_ButtonUp (void) {
	IN_KeyUp(&in_buttons[1]);}

void IN_CenterView (void) {
	cl.viewangles[PITCH] = -SHORT2ANGLE(cl.snap.ps.delta_angles[PITCH]);
}


//==========================================================================

cvar_t	*cl_upspeed;
cvar_t	*cl_forwardspeed;
cvar_t	*cl_sidespeed;

cvar_t	*cl_yawspeed;
cvar_t	*cl_pitchspeed;

cvar_t	*cl_run;

cvar_t	*cl_anglespeedkey;


/*
================
CL_AdjustAngles

Moves the local angle positions
================
*/
void CL_AdjustAngles( void ) {
	float	speed;

	if ( in_speed.active ) {
		speed = 0.001 * cls.frametime * cl_anglespeedkey->value;
	} else {
		speed = 0.001 * cls.frametime;
	}

	if ( !in_strafe.active ) {
		cl.viewangles[YAW] -= speed*cl_yawspeed->value*CL_KeyState (&in_right);
		cl.viewangles[YAW] += speed*cl_yawspeed->value*CL_KeyState (&in_left);
	}

	cl.viewangles[PITCH] -= speed*cl_pitchspeed->value * CL_KeyState (&in_lookup);
	cl.viewangles[PITCH] += speed*cl_pitchspeed->value * CL_KeyState (&in_lookdown);
}

/*
================
CL_KeyMove

Sets the usercmd_t based on key states
================
*/
void CL_KeyMove( usercmd_t *cmd ) {
	int		movespeed;
	int		forward, side, up;

	forward = 0;
	side = 0;
	up = 0;
	movespeed = 127;
	if ( in_strafe.active ) {
		side += movespeed * CL_KeyState (&in_right);
		side -= movespeed * CL_KeyState (&in_left);
	}

	side += movespeed * CL_KeyState (&in_moveright);
	side -= movespeed * CL_KeyState (&in_moveleft);


	up += movespeed * CL_KeyState (&in_up);
	up -= movespeed * CL_KeyState (&in_down);

	forward += movespeed * CL_KeyState (&in_forward);
	forward -= movespeed * CL_KeyState (&in_back);

	cmd->forwardmove = ClampChar( forward );
	cmd->rightmove = ClampChar( side );
	cmd->upmove = ClampChar( up );
}

/*
=================
CL_GetMouseState
=================
*/
void CL_GetMouseState( int *x, int *y, unsigned int *buttons )
{
	if( x )
		*x = cl.mousex;

	if( y )
		*y = cl.mousey;

	if( buttons )
	{
		if( in_guimouse ) {
			*buttons = cl.mouseButtons;
		} else {
			*buttons = 0;
		}
	}
}

/*
=================
CL_SetMousePos
=================
*/
void CL_SetMousePos( int x, int y )
{
	cl.mousex = x;
	cl.mousey = y;
}

/*
=================
CL_MouseEvent
=================
*/
void CL_MouseEvent( int dx, int dy, int time ) {
	if( in_guimouse )
	{
		cl.mousex += dx;
		cl.mousey += dy;

		if( cl.mousex < 0 )
			cl.mousex = 0;

		if( cl.mousex > cls.glconfig.vidWidth )
			cl.mousex = cls.glconfig.vidWidth;

		if( cl.mousey < 0 )
			cl.mousey = 0;

		if( cl.mousey > cls.glconfig.vidHeight )
			cl.mousey = cls.glconfig.vidHeight;

#if defined(__vita__) || defined(__SWITCH__)
		/* Engine upstream allows cl.mousex == vidWidth (one column past
		 * the last visible pixel) because the click hotspot is the
		 * top-left of the cursor and a desktop OS cursor would still be
		 * visible past the framebuffer. Vita/Switch render the whole cursor
		 * into the framebuffer, so a hotspot at vidWidth means the
		 * arrow's tip pixel is off-screen. Clamp 4 px inside the edge
		 * so the tip always lands on a visible pixel — matches what the
		 * user sees on the physical display. */
		if( cl.mousex > cls.glconfig.vidWidth  - 4 )
			cl.mousex = cls.glconfig.vidWidth  - 4;
		if( cl.mousey > cls.glconfig.vidHeight - 4 )
			cl.mousey = cls.glconfig.vidHeight - 4;
#endif
	}
	else if ( !paused->integer )
	{
		cl.mouseDx[cl.mouseIndex] += dx;
		cl.mouseDy[cl.mouseIndex] += dy;
	}
}

/*
=================
CL_JoystickEvent

Joystick values stay set until changed
=================
*/
void CL_JoystickEvent( int axis, int value, int time ) {
	if ( axis < 0 || axis >= MAX_JOYSTICK_AXIS ) {
		Com_Error( ERR_DROP, "CL_JoystickEvent: bad axis %i", axis );
	}
	cl.joystickAxis[axis] = value;
}

#ifdef __vita__
extern "C" void CL_VitaAimDebugPadSample( int x, int y ) {
	if ( vita_aim_debug && vita_aim_debug->integer ) {
		vitaDebugPadX = x;
		vitaDebugPadY = y;
	}
}

extern "C" void CL_VitaAimDebugTouchSample( int dx, int dy, int mouseX, int mouseY ) {
	if ( vita_aim_debug && vita_aim_debug->integer ) {
		vitaDebugRawTouchX = dx;
		vitaDebugRawTouchY = dy;
		vitaDebugEmittedX = mouseX;
		vitaDebugEmittedY = mouseY;
	}
}

static bool CL_VitaDebugHasInput( const VitaAimDebugFrame& sample ) {
	return sample.touchX || sample.touchY || sample.stickX || sample.stickY ||
	       sample.rawTouchX || sample.rawTouchY || sample.padX || sample.padY;
}

static bool CL_VitaAimDebugInGame( void ) {
	return clc.state == CA_ACTIVE && cl.snap.valid && cge && !server_loading;
}

void CL_VitaTouchEvent( int dx, int dy, int time ) {
	CL_MouseEvent( dx, dy, time );
	if ( !in_guimouse && !paused->integer ) {
		vitaTouchX += dx;
		vitaTouchY += dy;
	}
}

static qboolean CL_VitaAimSnapshotVisible( int entityNum ) {
	// Packet entities are sorted by number in the snapshot ring.
	int low = 0, high = cl.snap.numEntities;
	if ( cl.parseEntitiesNum - cl.snap.parseEntitiesNum > MAX_PARSE_ENTITIES )
		return qfalse;
	while ( low < high ) {
		const int mid = low + (high - low) / 2;
		const entityState_t *ent = &cl.parseEntities[(cl.snap.parseEntitiesNum + mid) & (MAX_PARSE_ENTITIES - 1)];
		if ( ent->number == entityNum )
			return ent->modelindex && ent->alpha > 0.0f && !(ent->eFlags & EF_DEAD) &&
			       !(ent->renderfx & (RF_DONTDRAW | RF_SKYENTITY | RF_SHADOW_PLANE)) ? qtrue : qfalse;
		if ( ent->number < entityNum )
			low = mid + 1;
		else
			high = mid;
	}
	return qfalse;
}

static const char *CL_VitaAssistBlock( void ) {
	const playerState_t& ps = cl.snap.ps;
	if ( VitaAimSetting( vita_aim_assist->value, 1.0f, 1.0f ) < 1.0f ) return "assist off";
	if ( clc.state != CA_ACTIVE || !cl.snap.valid || !cge ) return "waiting for gameplay snapshot";
	if ( !NET_IsLocalAddress( clc.serverAddress ) ) return "non-local server";
	if ( clc.demoplaying ) return "demo playback";
	if ( Key_GetCatcher() ) return "key catcher active";
	if ( in_guimouse ) return "GUI mouse active";
	if ( paused->integer ) return "game paused";
	if ( UI_MenuActive() ) return "game UI menu active";
	if ( CL_VitaPerfMenu_IsActive() ) return "Vita settings active";
	if ( ps.pm_type != PM_NORMAL ) return "player movement mode";
	if ( ps.stats[STAT_HEALTH] <= 0 ) return "player health";
	if ( (ps.stats[STAT_CINEMATIC] & 1) ||
	     (ps.pm_flags & (PMF_CAMERA_VIEW | PMF_FROZEN | PMF_INTERMISSION | PMF_TURRET)) )
		return "camera/player flags";
	return nullptr;
}

static void CL_VitaInitAimSettings( void ) {
	if ( !vita_aim_assist ) {
		const bool configured = Cvar_FindVar( "vita_aim_assist" ) || Cvar_FindVar( "vita_aim_friction" ) ||
		                        Cvar_FindVar( "vita_aim_radius" ) || Cvar_FindVar( "vita_aim_magnetism" ) ||
		                        Cvar_FindVar( "vita_aim_rotational" );
		const auto& recommended = vitaAimPresets[VITA_AIM_STRONG];
		vita_aim_assist = Cvar_Get( "vita_aim_assist", "1", CVAR_ARCHIVE );
		vita_aim_friction = Cvar_Get( "vita_aim_friction", configured ? "0.35" : va("%g", recommended.friction), CVAR_ARCHIVE );
		vita_aim_radius = Cvar_Get( "vita_aim_radius", configured ? "7.0" : va("%g", recommended.radius), CVAR_ARCHIVE );
		vita_aim_magnetism = Cvar_Get( "vita_aim_magnetism", "0", CVAR_ARCHIVE );
		vita_aim_magnetism_gain = Cvar_Get( "vita_aim_magnetism_gain",
		                                   configured ? "-1" : va("%g", recommended.magnetism), CVAR_ARCHIVE );
		vita_aim_rotational = Cvar_Get( "vita_aim_rotational", configured ? "0" : "1", CVAR_ARCHIVE );
		vita_aim_rotational_cap = Cvar_Get( "vita_aim_rotational_cap",
		                                   configured ? "0.12" : va("%g", recommended.rotation.perCommand), CVAR_ARCHIVE );
		vita_aim_rotational_rate = Cvar_Get( "vita_aim_rotational_rate",
		                                    configured ? "6" : va("%g", recommended.rotation.perSecond), CVAR_ARCHIVE );
		vita_aim_debug = Cvar_Get( "vita_aim_debug", "0", CVAR_TEMP | CVAR_NORESTART );
		Cvar_CheckRange( vita_aim_assist, 0.0f, 1.0f, qfalse );
		Cvar_CheckRange( vita_aim_friction, 0.0f, VITA_AIM_MAX_FRICTION, qfalse );
		Cvar_CheckRange( vita_aim_radius, 0.0f, VITA_AIM_MAX_RADIUS, qfalse );
		Cvar_CheckRange( vita_aim_magnetism, 0.0f, 1.0f, qfalse );
		Cvar_CheckRange( vita_aim_magnetism_gain, -1.0f, VITA_AIM_MAX_MAGNETISM, qfalse );
		Cvar_CheckRange( vita_aim_rotational, 0.0f, 1.0f, qfalse );
		Cvar_CheckRange( vita_aim_rotational_cap, 0.0f, VITA_AIM_MAX_ROTATION_CAP, qfalse );
		Cvar_CheckRange( vita_aim_rotational_rate, 0.0f, VITA_AIM_MAX_ROTATION_RATE, qfalse );
	}
}

static vitaAimPreset_t CL_VitaAimTuning( void ) {
	return {
		VitaAimSetting( vita_aim_friction->value, VITA_AIM_MAX_FRICTION, 0.35f ),
		VitaAimSetting( vita_aim_radius->value, VITA_AIM_MAX_RADIUS, 7.0f ),
		VitaAimMagnetismGain( vita_aim_magnetism_gain->value, vita_aim_magnetism->value ),
		{VitaAimSetting( vita_aim_rotational_cap->value, VITA_AIM_MAX_ROTATION_CAP, 0.12f ),
		 VitaAimSetting( vita_aim_rotational_rate->value, VITA_AIM_MAX_ROTATION_RATE, 6.0f )}
	};
}

int CL_VitaAimPresetIndex( void ) {
	CL_VitaInitAimSettings();
	return VitaAimPresetIndex( CL_VitaAimTuning() );
}

void CL_VitaApplyAimPreset( int index ) {
	if ( index < 0 || index >= VITA_AIM_PRESET_COUNT )
		return;
	CL_VitaInitAimSettings();
	const auto& preset = vitaAimPresets[index];
	Cvar_SetValue( "vita_aim_friction", preset.friction );
	Cvar_SetValue( "vita_aim_radius", preset.radius );
	Cvar_SetValue( "vita_aim_magnetism_gain", preset.magnetism );
	Cvar_SetValue( "vita_aim_rotational_cap", preset.rotation.perCommand );
	Cvar_SetValue( "vita_aim_rotational_rate", preset.rotation.perSecond );
}

void CL_VitaPrepareAimAssist( int msec ) {
	static float lastRadius, radiusTangent;
	CL_VitaInitAimSettings();

	if ( vita_aim_debug->integer ) {
		if ( !vitaDebugActive ) {
			vitaDebugHaveLook = false;
			vitaDebugFrames = vitaDebugQueries = vitaDebugSelected = vitaDebugApplied = vitaDebugEncoded = 0;
			vitaDebugStickOnly = vitaDebugStickOnlyEncoded = 0;
		}
		vitaDebugActive = true;
		++vitaDebugFrames;
		vitaDebugFrame = {};
		vitaDebugFrame.status = "input pending";
		vitaDebugFrame.game.playerTeam = vitaDebugFrame.game.enemyTeam = -1;
		vitaDebugFrame.game.nearest = vitaDebugFrame.game.lastChecked = vitaDebugFrame.target = -1;
		vitaDebugFrame.game.frictionMultiplier = 1.0f;
		vitaDebugFrame.touchX = vitaTouchX;
		vitaDebugFrame.touchY = vitaTouchY;
		vitaDebugFrame.stickX = cl.joystickAxis[j_yaw_axis->integer];
		vitaDebugFrame.stickY = cl.joystickAxis[j_pitch_axis->integer];
		vitaDebugFrame.rawTouchX = vitaDebugRawTouchX;
		vitaDebugFrame.rawTouchY = vitaDebugRawTouchY;
		vitaDebugFrame.emittedX = vitaDebugEmittedX;
		vitaDebugFrame.emittedY = vitaDebugEmittedY;
		vitaDebugFrame.padX = vitaDebugPadX;
		vitaDebugFrame.padY = vitaDebugPadY;
		vitaDebugFrame.serverId = cl.serverId;
	} else {
		vitaDebugActive = false;
	}
	vitaDebugRawTouchX = vitaDebugRawTouchY = vitaDebugEmittedX = vitaDebugEmittedY = 0;
	vitaDebugPadX = vitaDebugPadY = 0;
	vitaAim.target = {};
	if ( const char *block = CL_VitaAssistBlock() ) {
		if ( vitaDebugActive ) vitaDebugFrame.status = block;
		vitaTouchX = vitaTouchY = 0;
		vitaAim.engagement = 0.0f;
		return;
	}
	const bool looking =
		(!in_strafe.active && (vitaTouchX || cl.joystickAxis[j_yaw_axis->integer])) ||
		(!in_strafe.active && (in_mlooking || cl_freelook->integer) && vitaTouchY) ||
		(!in_mlooking && cl.joystickAxis[j_pitch_axis->integer]);
	if ( !looking ) {
		if ( vitaDebugActive ) vitaDebugFrame.status = "no eligible look input";
		// A slow drag can briefly produce no mouse counts. Keep the preference,
		// then require a fresh sight check as soon as look input resumes.
		vitaAim.idleMsec += msec > 0 ? (msec < 100 ? msec : 100) : 0;
		if ( vitaAim.idleMsec >= 100 ) {
			vitaAim.idleMsec = 100;
			vitaAim.engagement = 0.0f;
		}
		return;
	}
	vitaAim.idleMsec = 0;
	if ( Cvar_VariableIntegerValue( "cg_3rd_person" ) ) {
		if ( vitaDebugActive ) vitaDebugFrame.status = "third person";
		vitaAim.engagement = 0.0f;
		return;
	}

	const auto tuning = CL_VitaAimTuning();
	vitaAim.radius = tuning.radius;
	vitaAim.friction = tuning.friction;
	vitaAim.magnetism = tuning.magnetism;
	vitaAim.rotationLimits = tuning.rotation;
	vitaAim.rotational = VitaAimSetting( vita_aim_rotational->value, 1.0f, 0.0f ) >= 1.0f;
	if ( vitaAim.radius == 0.0f || (vitaAim.friction == 0.0f && vitaAim.magnetism == 0.0f && !vitaAim.rotational) ) {
		if ( vitaDebugActive ) vitaDebugFrame.status = "zero radius or all effects off";
		vitaAim.engagement = 0.0f;
		return;
	}
	if ( vitaAim.radius != lastRadius ) {
		radiusTangent = tanf( DEG2RAD( vitaAim.radius ) );
		lastRadius = vitaAim.radius;
	}

	vec3_t eye, angles;
	cge->CG_EyePosition( &eye );
	cge->CG_EyeAngles( &angles );
	const int preferred = vitaAim.engagement > 0.0f ? vitaAim.preferredEntity : -1;
	vitaAim.target.entityNum = preferred;
	if ( vitaDebugActive ) {
		++vitaDebugQueries;
		vitaAim.target.debug = &vitaDebugFrame.game;
	}
	SV_VitaAimTarget( cl.snap.ps.clientNum, eye, angles, radiusTangent, CL_VitaAimSnapshotVisible, &vitaAim.target );
	if ( vitaDebugActive ) {
		switch ( vitaDebugFrame.game.stage ) {
		case VITA_AIM_NO_SERVER:      vitaDebugFrame.status = "server not in game"; break;
		case VITA_AIM_NO_GAME_EXPORT: vitaDebugFrame.status = "game export missing"; break;
		case VITA_AIM_BAD_SLOT:       vitaDebugFrame.status = "invalid server client slot"; break;
		case VITA_AIM_BAD_MODE:       vitaDebugFrame.status = "server mode is not single-player"; break;
		case VITA_AIM_BAD_PLAYER:     vitaDebugFrame.status = "server player invalid"; break;
		case VITA_AIM_BAD_STATE:      vitaDebugFrame.status = "server player state blocked"; break;
		case VITA_AIM_BAD_TEAM:       vitaDebugFrame.status = "server player team invalid"; break;
		case VITA_AIM_SCANNED:        vitaDebugFrame.status = "no visible target"; break;
		}
		if ( vitaAim.target.weight > 0.0f ) {
			++vitaDebugSelected;
			vitaDebugFrame.target = vitaAim.target.entityNum;
			vitaDebugFrame.status = "target visible";
		}
	}
	vitaAim.sampledMessage = cl.snap.messageNum;
	vitaAim.engagement = vitaAim.target.weight > 0.0f
		? VitaAimEngagement( vitaAim.target.entityNum == preferred ? vitaAim.engagement : 0.0f, msec )
		: 0.0f;
	vitaAim.preferredEntity = vitaAim.target.entityNum;
}
#endif

/*
=================
CL_UpdateMouse

Added in OPM
Update mouse position with absolute position (relative to the window)
when the UI catcher is active.
=================
*/
void CL_UpdateMouse() {
    if (!(Key_GetCatcher() & KEYCATCH_UI)) {
        return;
    }

    if (IN_IsCursorActive()) {
        // If it's grabbed, don't use absolute position
        return;
    }

    if (com_unfocused->integer) {
        // Ignore updates when unfocused
        return;
    }

    IN_GetMousePosition(&cl.mousex, &cl.mousey);
}

/*
=================
CL_JoystickMove
=================
*/
void CL_JoystickMove( usercmd_t *cmd ) {
	float	anglespeed;

	float yaw     = j_yaw->value     * cl.joystickAxis[j_yaw_axis->integer];
	float right   = j_side->value    * cl.joystickAxis[j_side_axis->integer];
	float forward = j_forward->value * cl.joystickAxis[j_forward_axis->integer];
	float pitch   = j_pitch->value   * cl.joystickAxis[j_pitch_axis->integer];
	float up      = j_up->value      * cl.joystickAxis[j_up_axis->integer];

#if defined(__vita__) || defined(__SWITCH__)
	/* Vita/Switch: separate look-sensitivity multipliers for hip-fire vs
	 * aiming (iron sights / zoom). Stored as integers 0..10 = 0.0x..1.0x so
	 * the in-game perf menu can cycle them; CVAR_ARCHIVE so they persist.
	 * Defaults: hip 1.0x (unchanged from j_yaw/j_pitch tuning), aim 0.5x
	 * for steadier shots. Only scales the camera look, not movement. */
	{
		static cvar_t *vita_hip_sens = NULL;
		static cvar_t *vita_aim_sens = NULL;
		if (!vita_hip_sens) vita_hip_sens = Cvar_Get("vita_hip_sens", "10", CVAR_ARCHIVE);
		if (!vita_aim_sens) vita_aim_sens = Cvar_Get("vita_aim_sens", "5",  CVAR_ARCHIVE);
		float lookScale = (cl.snap.ps.stats[STAT_INZOOM] != 0)
			? vita_aim_sens->integer * 0.1f
			: vita_hip_sens->integer * 0.1f;
		yaw   *= lookScale;
		pitch *= lookScale;
	}
#endif

	if ( in_speed.active ^ cl_run->integer ) {
		cmd->buttons |= BUTTON_RUN;
	}

	if ( in_speed.active ) {
		anglespeed = 0.001 * cls.frametime * cl_anglespeedkey->value;
	} else {
		anglespeed = 0.001 * cls.frametime;
	}

	if ( !in_strafe.active ) {
		cl.viewangles[YAW] += anglespeed * yaw;
#ifdef __vita__
		if ( vitaAim.target.weight > 0.0f || vitaDebugActive )
			vitaAim.stick.yaw = anglespeed * yaw;
#endif
		cmd->rightmove = ClampChar( cmd->rightmove + (int)right );
	} else {
		cl.viewangles[YAW] += anglespeed * right;
		cmd->rightmove = ClampChar( cmd->rightmove + (int)yaw );
	}

	if ( in_mlooking ) {
		cl.viewangles[PITCH] += anglespeed * forward;
		cmd->forwardmove = ClampChar( cmd->forwardmove + (int)pitch );
	} else {
		cl.viewangles[PITCH] += anglespeed * pitch;
#ifdef __vita__
		if ( vitaAim.target.weight > 0.0f || vitaDebugActive )
			vitaAim.stick.pitch = anglespeed * pitch;
#endif
		cmd->forwardmove = ClampChar( cmd->forwardmove + (int)forward );
	}

	cmd->upmove = ClampChar( cmd->upmove + (int)up );
}

/*
=================
CL_MouseMove
=================
*/
void CL_MouseMove( usercmd_t *cmd ) {
	float	mx, my;
	float	cgameSensitivity;

	// allow mouse smoothing
	if (m_filter->integer)
	{
		mx = (cl.mouseDx[0] + cl.mouseDx[1]) * 0.5f;
		my = (cl.mouseDy[0] + cl.mouseDy[1]) * 0.5f;
	}
	else
	{
		mx = cl.mouseDx[cl.mouseIndex];
		my = cl.mouseDy[cl.mouseIndex];
	}

	cl.mouseIndex ^= 1;
#ifdef __vita__
	float touchShareX = 0.0f, touchShareY = 0.0f;
	const bool debugOnly = vitaDebugActive && vitaAim.target.weight <= 0.0f;
	const int touchX = debugOnly ? vitaDebugFrame.touchX : vitaTouchX;
	const int touchY = debugOnly ? vitaDebugFrame.touchY : vitaTouchY;
	if ( (vitaAim.target.weight > 0.0f || vitaDebugActive) && (touchX || touchY) ) {
		const float filter = m_filter->integer ? 0.5f : 1.0f;
		touchShareX = VitaAimTouchShare( filter * touchX, mx );
		touchShareY = VitaAimTouchShare( filter * touchY, my );
	}
	vitaTouchX = vitaTouchY = 0;
#endif
	cl.mouseDx[cl.mouseIndex] = 0;
	cl.mouseDy[cl.mouseIndex] = 0;

    if (mx == 0.0f && my == 0.0f)
        return;

	if (cl_mouseAccel->value != 0.0f)
	{
		if(cl_mouseAccelStyle->integer == 0)
		{
			float accelSensitivity;
			float rate;
			
			rate = sqrt(mx * mx + my * my) / (float) frame_msec;

			accelSensitivity = cl_sensitivity->value + rate * cl_mouseAccel->value;
			mx *= accelSensitivity;
			my *= accelSensitivity;
			
			if(cl_showMouseRate->integer)
				Com_Printf("rate: %f, accelSensitivity: %f\n", rate, accelSensitivity);
		}
		else
		{
			float rate[2];
			float power[2];

			// sensitivity remains pretty much unchanged at low speeds
			// cl_mouseAccel is a power value to how the acceleration is shaped
			// cl_mouseAccelOffset is the rate for which the acceleration will have doubled the non accelerated amplification
			// NOTE: decouple the config cvars for independent acceleration setup along X and Y?

			rate[0] = fabs(mx) / (float) frame_msec;
			rate[1] = fabs(my) / (float) frame_msec;
			power[0] = powf(rate[0] / cl_mouseAccelOffset->value, cl_mouseAccel->value);
			power[1] = powf(rate[1] / cl_mouseAccelOffset->value, cl_mouseAccel->value);

			mx = cl_sensitivity->value * (mx + ((mx < 0) ? -power[0] : power[0]) * cl_mouseAccelOffset->value);
			my = cl_sensitivity->value * (my + ((my < 0) ? -power[1] : power[1]) * cl_mouseAccelOffset->value);

			if(cl_showMouseRate->integer)
				Com_Printf("ratex: %f, ratey: %f, powx: %f, powy: %f\n", rate[0], rate[1], power[0], power[1]);
		}
	}
	else
	{
		mx *= cl_sensitivity->value;
		my *= cl_sensitivity->value;
	}

	cgameSensitivity = 1.0f;
	if( cge && !UI_MenuActive() )
	{
		if( cge->CG_SensitivityScale() >= 0.0 ) {
			cgameSensitivity = cge->CG_SensitivityScale();
		}
	}

#ifdef __SWITCH__
	/* Dev-menu "Sens. MIRANDO (ADS)" scales the aim sensitivity only while the
	 * cgame is applying a zoom scale (aiming/scoped), independently of the
	 * hip-fire "sensitivity" cvar. */
	if( cgameSensitivity != 1.0f )
	{
		extern cvar_t *cl_ads_sensitivity;
		if( cl_ads_sensitivity ) cgameSensitivity *= cl_ads_sensitivity->value;
	}
#endif

	mx *= cgameSensitivity;
	my *= cgameSensitivity;

	cmd->buttons |= BUTTON_ANY;

	// add mouse X/Y movement to cmd
	if(in_strafe.active)
		cmd->rightmove = ClampChar(cmd->rightmove + m_side->value * mx);
	else {
		cl.viewangles[YAW] -= m_yaw->value * mx;
#ifdef __vita__
		if ( touchShareX > 0.0f )
			vitaAim.touch.yaw = -m_yaw->value * mx * touchShareX;
#endif
	}

	if ((in_mlooking || cl_freelook->integer) && !in_strafe.active) {
		cl.viewangles[PITCH] += m_pitch->value * my;
#ifdef __vita__
		if ( touchShareY > 0.0f )
			vitaAim.touch.pitch = m_pitch->value * my * touchShareY;
#endif
	} else
		cmd->forwardmove = ClampChar(cmd->forwardmove - m_forward->value * my);

	if (!isfinite(cl.viewangles[PITCH]) || !isfinite(cl.viewangles[YAW])) {
		Com_DPrintf("Invalid client viewangles encountered (view pitch: %f, view yaw: %f)!\n", cl.viewangles[PITCH], cl.viewangles[YAW]);
		Com_DPrintf("cgameSensitivity: %f | mx: %f | my: %f | m_pitch: %f | m_yaw: %f\n", cgameSensitivity, mx, my, m_pitch->value, m_yaw->value);
		Com_DPrintf("Resetting client viewangles\n");
		cl.viewangles[PITCH] = 0;
		cl.viewangles[YAW] = 0;
	}
}

/*
==============
CL_ClearButtons
==============
*/
void CL_ClearButtons( void ) {
	memset( in_buttons, 0, sizeof( in_buttons ) );
}

/*
==============
CL_CmdButtons
==============
*/
void CL_CmdButtons( usercmd_t *cmd ) {
	int		i;
	
	//
	// figure button bits
	// send a button bit even if the key was pressed and released in
	// less than a frame
	//
	for (i = 0 ; i < 15 ; i++) {
		if ( in_buttons[i].active || in_buttons[i].wasPressed ) {
			cmd->buttons |= 1 << i;
		}
		in_buttons[i].wasPressed = qfalse;
	}

	if (UI_MenuActive() || UI_ConsoleIsOpen()) {
		cmd->buttons |= BUTTON_TALK;
	}
	
	if ( in_speed.active ^ cl_run->integer ) {
		cmd->buttons |= BUTTON_RUN;
	}

	if (cge) {
		// send weapon commands from cg
		cmd->buttons |= cge->CG_WeaponCommandButtonBits();
	}

	// allow the game to know if any key at all is
	// currently pressed, even if it isn't bound to anything
	if ( anykeydown && Key_GetCatcher() == 0) {
		cmd->buttons |= BUTTON_MOUSE;
	}

	if (anykeydown) {
		cmd->buttons |= BUTTON_ANY;
	}
}


/*
==============
CL_FinishMove
==============
*/
void CL_FinishMove( usercmd_t *cmd ) {
	int		i;

	// send the current server time so the amount of movement
	// can be determined without allowing cheating
	cmd->serverTime = cl.serverTime;

	for (i=0 ; i<3 ; i++) {
		cmd->angles[i] = ANGLE2SHORT(cl.viewangles[i]);
	}
}


/*
=================
CL_CreateCmd
=================
*/
usercmd_t CL_CreateCmd( void ) {
	usercmd_t	cmd;
	vec3_t		oldAngles;

	VectorCopy( cl.viewangles, oldAngles );

	// keyboard angle adjustment
	CL_AdjustAngles ();

	Com_Memset( &cmd, 0, sizeof( cmd ) );

	CL_CmdButtons( &cmd );

	// get basic movement from keyboard
	CL_KeyMove( &cmd );

#ifdef __vita__
	vitaAim.touch = {};
	vitaAim.stick = {};
#endif

	// get basic movement from mouse
	CL_MouseMove( &cmd );

	// get basic movement from joystick
	CL_JoystickMove( &cmd );

#ifdef __vita__
	vitaAimLook_t correction = {};
	const float baseYaw = cl.viewangles[YAW], basePitch = cl.viewangles[PITCH];
	if ( vitaAim.target.weight > 0.0f &&
	     (vitaAim.sampledMessage == cl.snap.messageNum || CL_VitaAimSnapshotVisible( vitaAim.target.entityNum )) ) {
		const char *block = CL_VitaAssistBlock();
		correction = VitaAimCorrection( !block, vitaAim.target, vitaAim.touch, vitaAim.stick,
			vitaAim.radius, vitaAim.engagement, vitaAim.friction, vitaAim.magnetism,
			vitaAim.rotational, cmd.rightmove / 127.0f, frame_msec, vitaAim.rotationLimits );
		if ( vitaDebugActive && block )
			vitaDebugFrame.status = block;
		if ( correction.yaw != 0.0f )
			cl.viewangles[YAW] += correction.yaw;
		if ( correction.pitch != 0.0f )
			cl.viewangles[PITCH] += correction.pitch;
	} else if ( vitaDebugActive && vitaAim.target.weight > 0.0f ) {
		vitaDebugFrame.status = "target lost in newer snapshot";
	}
#endif

	// check to make sure the angles haven't wrapped
	if ( cl.viewangles[PITCH] - oldAngles[PITCH] > 90 ) {
		cl.viewangles[PITCH] = oldAngles[PITCH] + 90;
	} else if ( oldAngles[PITCH] - cl.viewangles[PITCH] > 90 ) {
		cl.viewangles[PITCH] = oldAngles[PITCH] - 90;
	}

	// store out the final values
	CL_FinishMove( &cmd );
#ifdef __vita__
	if ( vitaDebugActive && CL_VitaAimDebugInGame() && vitaDebugFrame.serverId == cl.serverId ) {
		vitaDebugFrame.touch = vitaAim.touch;
		vitaDebugFrame.stick = vitaAim.stick;
		vitaDebugFrame.game.strafe = cmd.rightmove / 127.0f;
		const bool stickOnly = (vitaAim.stick.yaw != 0.0f || vitaAim.stick.pitch != 0.0f) &&
		                       vitaAim.touch.yaw == 0.0f && vitaAim.touch.pitch == 0.0f;
		if ( stickOnly )
			++vitaDebugStickOnly;
		vitaDebugFrame.before = {baseYaw - oldAngles[YAW], basePitch - oldAngles[PITCH]};
		vitaDebugFrame.final = {cl.viewangles[YAW] - oldAngles[YAW], cl.viewangles[PITCH] - oldAngles[PITCH]};
		vitaDebugFrame.wire = {
			(float)SHORT2ANGLE( (short)(cmd.angles[YAW] - ANGLE2SHORT(oldAngles[YAW])) ),
			(float)SHORT2ANGLE( (short)(cmd.angles[PITCH] - ANGLE2SHORT(oldAngles[PITCH])) )
		};
		if ( correction.yaw != 0.0f || correction.pitch != 0.0f ) {
			++vitaDebugApplied;
			const float clampedPitch = Com_Clamp( oldAngles[PITCH] - 90, oldAngles[PITCH] + 90, basePitch );
			if ( cmd.angles[YAW] != (short)ANGLE2SHORT(baseYaw) ||
			     cmd.angles[PITCH] != (short)ANGLE2SHORT(clampedPitch) ) {
				++vitaDebugEncoded;
				if ( stickOnly )
					++vitaDebugStickOnlyEncoded;
			}
		}
		if ( CL_VitaDebugHasInput( vitaDebugFrame ) ) {
			vitaDebugLastLook = vitaDebugFrame;
			vitaDebugHaveLook = true;
		}
	}
#endif

	// draw debug graphs of turning for mouse testing
	if ( cl_debugMove->integer ) {
		if ( cl_debugMove->integer == 1 ) {
			SCR_DebugGraph( fabs(cl.viewangles[YAW] - oldAngles[YAW]) );
		}
		if ( cl_debugMove->integer == 2 ) {
			SCR_DebugGraph( fabs(cl.viewangles[PITCH] - oldAngles[PITCH]) );
		}
	}

	return cmd;
}

#ifdef __vita__
static const char *CL_VitaMagReason( vitaAimMagReason_t reason ) {
	static const char *const labels[] = {
		"no target", "blocked", "off", "idle axis", "centered axis", "axis away",
		"swipe away", "tangent swipe", "no gain", "no room", "capped", "applied"
	};
	return labels[reason];
}

static const char *CL_VitaRotReason( vitaAimRotReason_t reason ) {
	static const char *const labels[] = {
		"no target", "blocked", "off", "no look input", "no time", "no strafe", "weak strafe",
		"no lateral velocity", "velocity opposes strafe", "centered yaw", "parallax away",
		"look away", "outer cone or ramp", "no room", "applied"
	};
	return labels[reason];
}

void CL_VitaAimDebugDraw( void ) {
	if ( !vitaDebugActive || !vita_aim_debug->integer || !CL_VitaAimDebugInGame() ||
	     vitaDebugFrame.serverId != cl.serverId || CL_VitaPerfMenu_IsActive() )
		return;

	const int screenWidth = cls.glconfig.vidWidth;
	const int screenHeight = cls.glconfig.vidHeight;
	if ( screenWidth < 17 || screenHeight < 1 )
		return;

	// The last HUD widget can leave its own viewport and clip rectangle active.
	re.Set2DWindow( 0, 0, screenWidth, screenHeight, 0, screenWidth, screenHeight, 0, -1, 1 );
	re.Scissor( 0, 0, screenWidth, screenHeight );
	static UIFont overlayFont( "vita-14" );
	UIFont *font = &overlayFont;

	const bool idle = !CL_VitaDebugHasInput( vitaDebugFrame );
	const bool held = idle && vitaDebugHaveLook && vitaDebugLastLook.serverId == cl.serverId;
	const VitaAimDebugFrame& sample = held ? vitaDebugLastLook : vitaDebugFrame;
	const vitaAimDebug_t& game = sample.game;
	const bool enabled = VitaAimSetting( vita_aim_assist->value, 1.0f, 1.0f ) >= 1.0f;
	const vec4_t background = {0.02f, 0.03f, 0.04f, 0.88f};
	re.SetColor( background );
	re.DrawBox( 8.0f, 8.0f, fminf( 610.0f, screenWidth - 16.0f ), 228.0f );
	re.SetColor( nullptr );

	font->setColor( UYellow );
	font->Print( 16, 13, va("Aim assist %s | %s%s", enabled ? "ON" : "OFF",
	                       held ? "last look: " : "", sample.status ? sample.status : "waiting"), -1, nullptr );
	font->setColor( UWhite );
	font->Print( 16, 30, va("Touch %d/%d  -> %+.3f/%+.3f   Stick %d/%d  -> %+.3f/%+.3f",
	                       sample.touchX, sample.touchY, sample.touch.yaw, sample.touch.pitch,
	                       sample.stickX, sample.stickY, sample.stick.yaw, sample.stick.pitch), -1, nullptr );
	font->Print( 16, 47, va("Team %d -> %d   Enemies %d  Eligible %d  Cone %d  Visible %d/%d",
	                       game.playerTeam, game.enemyTeam, game.enemies, game.eligible,
	                       game.inCone, game.visible, game.checked), -1, nullptr );
	font->Print( 16, 64, va("Target %d @ %.2f deg   Nearest %d @ %.2f   Tested #%d",
	                       sample.target, game.selectedAngle, game.nearest, game.nearestAngle, game.lastChecked), -1, nullptr );
	font->Print( 16, 81, va("Rejected   Snapshot %d   PVS %d   Solid %d   Obscured %d",
	                       game.snapshotRejected, game.pvsRejected, game.solidRejected, game.smokeRejected), -1, nullptr );
	const bool noLook = sample.touch.yaw + sample.stick.yaw == 0.0f && sample.touch.pitch + sample.stick.pitch == 0.0f;
	const char *approach = game.magYaw.reason == VITA_MAG_NO_TARGET ? "n/a" : noLook ? "idle" :
	                       game.approach > 0.0f ? "toward" : game.approach < 0.0f ? "away" : "tangent";
	font->Print( 16, 98, va("Error Y/P %+.3f/%+.3f deg   Swipe %s   Target velocity unused",
	                       game.magYaw.error, game.magPitch.error, approach), -1, nullptr );
	font->Print( 16, 115, va("Mag yaw   %-14s  Raw %+.6f  Applied %+.6f",
	                       CL_VitaMagReason(game.magYaw.reason), game.magYaw.raw, game.magYaw.applied), -1, nullptr );
	font->Print( 16, 132, va("Mag pitch %-14s  Raw %+.6f  Applied %+.6f",
	                       CL_VitaMagReason(game.magPitch.reason), game.magPitch.raw, game.magPitch.applied), -1, nullptr );
	font->Print( 16, 149, va("Rot %s   Strafe %+.2f   Parallax %+.2f d/s   Applied %+.5f",
	                       CL_VitaRotReason(game.rotReason), game.strafe, game.strafeYawRate, game.rotationalYaw), -1, nullptr );
	font->Print( 16, 166, va("Before %+.3f %+.3f   Final %+.3f %+.3f   Cmd %+.3f %+.3f deg",
	                       sample.before.yaw, sample.before.pitch, sample.final.yaw, sample.final.pitch,
	                       sample.wire.yaw, sample.wire.pitch), -1, nullptr );
	const auto tuning = CL_VitaAimTuning();
	font->Print( 16, 183, va("Friction x%.3f (cfg %.2f)   Radius %.1f   Mag %.2f   Rot %d %.2f/%.1f",
	                       game.frictionMultiplier, tuning.friction, tuning.radius, tuning.magnetism,
	                       vita_aim_rotational->integer, tuning.rotation.perCommand, tuning.rotation.perSecond), -1, nullptr );
	font->Print( 16, 200, va("F/Q/Sel/Adj/Enc %u/%u/%u/%u/%u   Stick-only cmd/enc %u/%u",
	                       vitaDebugFrames, vitaDebugQueries, vitaDebugSelected, vitaDebugApplied, vitaDebugEncoded,
	                       vitaDebugStickOnly, vitaDebugStickOnlyEncoded), -1, nullptr );
	font->Print( 16, 217, va("Polled   Touch %d %d -> %d %d   Right stick %d %d",
	                       sample.rawTouchX, sample.rawTouchY, sample.emittedX, sample.emittedY,
	                       sample.padX, sample.padY), -1, nullptr );
	re.SetColor( nullptr );
}
#endif


/*
=================
CL_CreateNewCommands

Create a new usercmd_t structure for this frame
=================
*/
void CL_CreateNewCommands( void ) {
	usercmd_t	*cmd;
	int			cmdNum;

	// no need to create usercmds until we have a gamestate
	if ( clc.state < CA_PRIMED ) {
		return;
	}

	frame_msec = com_frameTime - old_com_frameTime;

	// if running over 1000fps, act as if each frame is 1ms
	// prevents divisions by zero
	if ( frame_msec < 1 ) {
		frame_msec = 1;
	}

	// if running less than 5fps, truncate the extra time to prevent
	// unexpected moves after a hitch
	if ( frame_msec > 200 ) {
		frame_msec = 200;
	}
	old_com_frameTime = com_frameTime;


	// generate a command for this frame
	cl.cmdNumber++;
	cmdNum = cl.cmdNumber & CMD_MASK;
	cl.cmds[cmdNum] = CL_CreateCmd ();
	cmd = &cl.cmds[cmdNum];
}

/*
=================
CL_EyeInfo

Gather eyes data from client game module
=================
*/
void CL_EyeInfo(usereyes_t* info) {
	int i;
	vec3_t vAngles, vOfs;

	if (cge) {
		// copy the eye offset
		cge->CG_EyeOffset(&vOfs);

		for (i = 0; i < 3; i++) {
			vOfs[i] += 0.5;
			if (vOfs[i] < -127.0) {
				vOfs[i] = -127.0;
			} else if (vOfs[i] > 128.0) {
				vOfs[i] = 128.0;
			}

			info->ofs[i] = vOfs[i];
		}

		cge->CG_EyeAngles(&vAngles);
		info->angles[0] = vAngles[0];
		info->angles[1] = vAngles[1];
	} else {
		// clear the eye offset
		for (i = 0; i < 3; i++) {
			info->ofs[i] = 0;
		}

		info->angles[0] = info->angles[1] = 0.0;
	}
}

/*
=================
CL_ReadyToSendPacket

Returns qfalse if we are over the maxpackets limit
and should choke back the bandwidth a bit by not sending
a packet this frame.  All the commands will still get
delivered in the next packet, but saving a header and
getting more delta compression will reduce total bandwidth.
=================
*/
qboolean CL_ReadyToSendPacket( void ) {
	int		oldPacketNum;
	int		delta;

	// don't send anything if playing back a demo
	if ( clc.demoplaying || clc.state == CA_CINEMATIC ) {
		return qfalse;
	}

	// If we are downloading, we send no less than 50ms between packets
	if ( *clc.downloadTempName &&
		cls.realtime - clc.lastPacketSentTime < 50 ) {
		return qfalse;
	}

	// if we don't have a valid gamestate yet, only send
	// one packet a second
	if ( clc.state != CA_ACTIVE &&
		clc.state != CA_PRIMED &&
		!*clc.downloadTempName &&
		cls.realtime - clc.lastPacketSentTime < 1000 ) {
		return qfalse;
	}

	// send every frame for loopbacks
	if ( clc.netchan.remoteAddress.type == NA_LOOPBACK ) {
		return qtrue;
	}

	// send every frame for LAN
	if ( cl_lanForcePackets->integer && Sys_IsLANAddress( clc.netchan.remoteAddress ) ) {
		return qtrue;
	}

	// check for exceeding cl_maxpackets
	if ( cl_maxpackets->integer < 30 ) {
		Cvar_Set( "cl_maxpackets", "30" );
	} else if ( cl_maxpackets->integer > 125 ) {
		Cvar_Set( "cl_maxpackets", "125" );
	}
	oldPacketNum = (clc.netchan.outgoingSequence - 1) & PACKET_MASK;
	delta = cls.realtime -  cl.outPackets[ oldPacketNum ].p_realtime;
	if ( delta < 1000 / cl_maxpackets->integer ) {
		// the accumulated commands will go out in the next packet
		return qfalse;
	}

	return qtrue;
}

/*
===================
CL_WritePacket

Create and send the command packet to the server
Including both the reliable commands and the usercmds

During normal gameplay, a client packet will contain something like:

4	sequence number
2	qport
4	serverid
4	acknowledged sequence number
4	clc.serverCommandSequence
<optional reliable commands>
1	clc_move or clc_moveNoDelta
1	command count
<count * usercmds>

===================
*/
void CL_WritePacket( void ) {
	msg_t		buf;
	byte		data[MAX_MSGLEN];
	int			i, j;
	usercmd_t	*cmd, *oldcmd;
	usercmd_t	nullcmd;
	int			packetNum;
	int			oldPacketNum;
	int			count, key;
	usereyes_t	eyeInfo;

	// don't send anything if playing back a demo
	if ( clc.demoplaying || clc.state == CA_CINEMATIC ) {
		return;
	}

	Com_Memset( &nullcmd, 0, sizeof(nullcmd) );
	oldcmd = &nullcmd;		
		
	MSG_Init( &buf, data, sizeof(data) );

	MSG_Bitstream( &buf );
	// write the current serverId so the server
	// can tell if this is from the current gameState
	MSG_WriteLong( &buf, cl.serverId );

	// write the last message we received, which can
	// be used for delta compression, and is also used
	// to tell if we dropped a gamestate
	MSG_WriteLong( &buf, clc.serverMessageSequence );

	// write the last reliable message we received
	MSG_WriteLong( &buf, clc.serverCommandSequence );

	// write any unacknowledged clientCommands
	for ( i = clc.reliableAcknowledge + 1 ; i <= clc.reliableSequence ; i++ ) {
		MSG_WriteByte( &buf, clc_clientCommand );
		MSG_WriteLong( &buf, i );
		MSG_WriteScrambledString( &buf, clc.reliableCommands[ i & (MAX_RELIABLE_COMMANDS-1) ] );
	}

	// we want to send all the usercmds that were generated in the last
	// few packet, so even if a couple packets are dropped in a row,
	// all the cmds will make it to the server
	if ( cl_packetdup->integer < 0 ) {
		Cvar_Set( "cl_packetdup", "0" );
	} else if ( cl_packetdup->integer > 5 ) {
		Cvar_Set( "cl_packetdup", "5" );
	}
	oldPacketNum = (clc.netchan.outgoingSequence - 1 - cl_packetdup->integer) & PACKET_MASK;
	count = cl.cmdNumber - cl.outPackets[ oldPacketNum ].p_cmdNumber;
	if ( count > MAX_PACKET_USERCMDS ) {
		count = MAX_PACKET_USERCMDS;
		Com_Printf("MAX_PACKET_USERCMDS\n");
	}

#ifdef USE_VOIP
	if (clc.voipOutgoingDataSize > 0)
	{
		if((clc.voipFlags & VOIP_SPATIAL) || Com_IsVoipTarget(clc.voipTargets, sizeof(clc.voipTargets), -1))
		{
			MSG_WriteByte (&buf, clc_voipOpus);
			MSG_WriteByte (&buf, clc.voipOutgoingGeneration);
			MSG_WriteLong (&buf, clc.voipOutgoingSequence);
			MSG_WriteByte (&buf, clc.voipOutgoingDataFrames);
			MSG_WriteData (&buf, clc.voipTargets, sizeof(clc.voipTargets));
			MSG_WriteByte(&buf, clc.voipFlags);
			MSG_WriteShort (&buf, clc.voipOutgoingDataSize);
			MSG_WriteData (&buf, clc.voipOutgoingData, clc.voipOutgoingDataSize);

			// If we're recording a demo, we have to fake a server packet with
			//  this VoIP data so it gets to disk; the server doesn't send it
			//  back to us, and we might as well eliminate concerns about dropped
			//  and misordered packets here.
			if(clc.demorecording && !clc.demowaiting)
			{
				const int voipSize = clc.voipOutgoingDataSize;
				msg_t fakemsg;
				byte fakedata[MAX_MSGLEN];
				MSG_Init (&fakemsg, fakedata, sizeof (fakedata));
				MSG_Bitstream (&fakemsg);
				MSG_WriteLong (&fakemsg, clc.reliableAcknowledge);
				MSG_WriteByte (&fakemsg, svc_voipOpus);
				MSG_WriteShort (&fakemsg, clc.clientNum);
				MSG_WriteByte (&fakemsg, clc.voipOutgoingGeneration);
				MSG_WriteLong (&fakemsg, clc.voipOutgoingSequence);
				MSG_WriteByte (&fakemsg, clc.voipOutgoingDataFrames);
				MSG_WriteShort (&fakemsg, clc.voipOutgoingDataSize );
				MSG_WriteBits (&fakemsg, clc.voipFlags, VOIP_FLAGCNT);
				MSG_WriteData (&fakemsg, clc.voipOutgoingData, voipSize);
				MSG_WriteByte (&fakemsg, svc_EOF);
				CL_WriteDemoMessage (&fakemsg, 0);
			}

			clc.voipOutgoingSequence += clc.voipOutgoingDataFrames;
			clc.voipOutgoingDataSize = 0;
			clc.voipOutgoingDataFrames = 0;
		}
		else
		{
			// We have data, but no targets. Silently discard all data
			clc.voipOutgoingDataSize = 0;
			clc.voipOutgoingDataFrames = 0;
		}
	}
#endif

	if ( count >= 1 ) {
		if ( cl_showSend->integer ) {
			Com_Printf( "(%i)", count );
		}

		// begin a client move command
		if ( cl_nodelta->integer || !cl.snap.valid || clc.demowaiting
			|| clc.serverMessageSequence != cl.snap.messageNum ) {
			MSG_WriteByte (&buf, clc_moveNoDelta);
		} else {
			MSG_WriteByte (&buf, clc_move);
		}

		// write the command count
		MSG_WriteByte( &buf, count );

		CL_EyeInfo( &eyeInfo );
		MSG_WriteDeltaEyeInfo( &buf, &cl.outPackets[ oldPacketNum ].p_eyeinfo, &eyeInfo );
		// use the checksum feed in the key
		key = clc.checksumFeed;
		// also use the message acknowledge
		key ^= clc.serverMessageSequence;
		// also use the last acknowledged server command in the key
		key ^= Com_HashKey(clc.serverCommands[ clc.serverCommandSequence & (MAX_RELIABLE_COMMANDS-1) ], 32);

		// write all the commands, including the predicted command
		for ( i = 0 ; i < count ; i++ ) {
			j = (cl.cmdNumber - count + i + 1) & CMD_MASK;
			cmd = &cl.cmds[j];
			MSG_WriteDeltaUsercmdKey (&buf, key, oldcmd, cmd);
			oldcmd = cmd;
		}
	}
	else {
        eyeInfo.ofs[0] = 0;
        eyeInfo.ofs[1] = 0;
        eyeInfo.ofs[2] = 0;
        eyeInfo.angles[0] = cl.viewangles[0];
        eyeInfo.angles[1] = cl.viewangles[1];
	}

	//
	// deliver the message
	//
	packetNum = clc.netchan.outgoingSequence & PACKET_MASK;
	cl.outPackets[ packetNum ].p_realtime = cls.realtime;
	cl.outPackets[ packetNum ].p_serverTime = oldcmd->serverTime;
	cl.outPackets[ packetNum ].p_cmdNumber = cl.cmdNumber;
	cl.outPackets[ packetNum ].p_eyeinfo = eyeInfo;
	clc.lastPacketSentTime = cls.realtime;

	if ( cl_showSend->integer ) {
		Com_Printf( "%zu ", buf.cursize );
	}

	CL_Netchan_Transmit (&clc.netchan, &buf);
}

/*
=================
CL_SendCmd

Called every frame to builds and sends a command packet to the server.
=================
*/
void CL_SendCmd( void ) {
	// don't send any message if not connected
	if ( clc.state < CA_CONNECTED ) {
		return;
	}

	// don't send commands if paused
	if ( com_sv_running->integer && paused->integer  ) {
		return;
	}

	// we create commands even if a demo is playing,
	CL_CreateNewCommands();

	// don't send a packet if the last packet was sent too recently
	if ( !CL_ReadyToSendPacket() ) {
		if ( cl_showSend->integer ) {
			Com_Printf( ". " );
		}
		return;
	}

	CL_WritePacket();
}

/*
============
CL_InitInput
============
*/
void CL_InitInput( void ) {
	Cmd_AddCommand("centerview", IN_CenterView);

	Cmd_AddCommand("+moveup", IN_UpDown);
	Cmd_AddCommand("-moveup", IN_UpUp);
	Cmd_AddCommand("+movedown", IN_DownDown);
	Cmd_AddCommand("-movedown", IN_DownUp);
	Cmd_AddCommand("+left", IN_LeftDown);
	Cmd_AddCommand("-left", IN_LeftUp);
	Cmd_AddCommand("+right", IN_RightDown);
	Cmd_AddCommand("-right", IN_RightUp);
	Cmd_AddCommand("+forward", IN_ForwardDown);
	Cmd_AddCommand("-forward", IN_ForwardUp);
	Cmd_AddCommand("+back", IN_BackDown);
	Cmd_AddCommand("-back", IN_BackUp);
	Cmd_AddCommand("+lookup", IN_LookupDown);
	Cmd_AddCommand("-lookup", IN_LookupUp);
	Cmd_AddCommand("+lookdown", IN_LookdownDown);
	Cmd_AddCommand("-lookdown", IN_LookdownUp);
	Cmd_AddCommand("+strafe", IN_StrafeDown);
	Cmd_AddCommand("-strafe", IN_StrafeUp);
	Cmd_AddCommand("+moveleft", IN_MoveleftDown);
	Cmd_AddCommand("-moveleft", IN_MoveleftUp);
	Cmd_AddCommand("+moveright", IN_MoverightDown);
	Cmd_AddCommand("-moveright", IN_MoverightUp);
	Cmd_AddCommand("+attack", IN_AttackPrimaryDown);
	Cmd_AddCommand("-attack", IN_AttackPrimaryUp);
	Cmd_AddCommand("+attackprimary", IN_AttackPrimaryDown);
	Cmd_AddCommand("-attackprimary", IN_AttackPrimaryUp);
	Cmd_AddCommand("+attacksecondary", IN_AttackSecondaryDown);
	Cmd_AddCommand("-attacksecondary", IN_AttackSecondaryUp);
	Cmd_AddCommand("+use", IN_Button3Down);
	Cmd_AddCommand("-use", IN_Button3Up);
	Cmd_AddCommand("+leanleft", IN_LeanLeftDown);
	Cmd_AddCommand("-leanleft", IN_LeanLeftUp);
	Cmd_AddCommand("+leanright", IN_LeanRightDown);
	Cmd_AddCommand("-leanright", IN_LeanRightUp);
	Cmd_AddCommand("+speed", IN_SpeedDown);
	Cmd_AddCommand("-speed", IN_SpeedUp);
	Cmd_AddCommand("+button0", IN_Button0Down);
	Cmd_AddCommand("-button0", IN_Button0Up);
	Cmd_AddCommand("+button1", IN_Button1Down);
	Cmd_AddCommand("-button1", IN_Button1Up);
	Cmd_AddCommand("+button2", IN_Button2Down);
	Cmd_AddCommand("-button2", IN_Button2Up);
	Cmd_AddCommand("+button3", IN_Button3Down);
	Cmd_AddCommand("-button3", IN_Button3Up);
	Cmd_AddCommand("+button4", IN_Button4Down);
	Cmd_AddCommand("-button4", IN_Button4Up);
	Cmd_AddCommand("+button5", IN_Button5Down);
	Cmd_AddCommand("-button5", IN_Button5Up);
	Cmd_AddCommand("+button6", IN_Button6Down);
	Cmd_AddCommand("-button6", IN_Button6Up);
	Cmd_AddCommand("+button7", IN_Button7Down);
	Cmd_AddCommand("-button7", IN_Button7Up);
	Cmd_AddCommand("+button8", IN_Button8Down);
	Cmd_AddCommand("-button8", IN_Button8Up);
	Cmd_AddCommand("+button9", IN_Button9Down);
	Cmd_AddCommand("-button9", IN_Button9Up);
	Cmd_AddCommand("+button10", IN_Button10Down);
	Cmd_AddCommand("-button10", IN_Button10Up);
	Cmd_AddCommand("+button11", IN_Button11Down);
	Cmd_AddCommand("-button11", IN_Button11Up);
	Cmd_AddCommand("+button12", IN_Button12Down);
	Cmd_AddCommand("-button12", IN_Button12Up);
	Cmd_AddCommand("+button13", IN_Button13Down);
	Cmd_AddCommand("-button13", IN_Button13Up);
	Cmd_AddCommand("+button14", IN_Button14Down);
	Cmd_AddCommand("-button14", IN_Button14Up);
	Cmd_AddCommand("+mlook", IN_MLookDown);
	Cmd_AddCommand("-mlook", IN_MLookUp);

#ifdef USE_VOIP
	Cmd_AddCommand ("+voiprecord", IN_VoipRecordDown);
	Cmd_AddCommand ("-voiprecord", IN_VoipRecordUp);
#endif

	cl_nodelta = Cvar_Get ("cl_nodelta", "0", 0);
	cl_debugMove = Cvar_Get ("cl_debugMove", "0", 0);
}

/*
============
CL_ShutdownInput
============
*/
void CL_ShutdownInput(void)
{
	Cmd_RemoveCommand("centerview");

	Cmd_RemoveCommand("+moveup");
	Cmd_RemoveCommand("-moveup");
	Cmd_RemoveCommand("+movedown");
	Cmd_RemoveCommand("-movedown");
	Cmd_RemoveCommand("+left");
	Cmd_RemoveCommand("-left");
	Cmd_RemoveCommand("+right");
	Cmd_RemoveCommand("-right");
	Cmd_RemoveCommand("+forward");
	Cmd_RemoveCommand("-forward");
	Cmd_RemoveCommand("+back");
	Cmd_RemoveCommand("-back");
	Cmd_RemoveCommand("+lookup");
	Cmd_RemoveCommand("-lookup");
	Cmd_RemoveCommand("+lookdown");
	Cmd_RemoveCommand("-lookdown");
	Cmd_RemoveCommand("+strafe");
	Cmd_RemoveCommand("-strafe");
	Cmd_RemoveCommand("+moveleft");
	Cmd_RemoveCommand("-moveleft");
	Cmd_RemoveCommand("+moveright");
	Cmd_RemoveCommand("-moveright");
	Cmd_RemoveCommand("+speed");
	Cmd_RemoveCommand("-speed");
	Cmd_RemoveCommand("+attack");
	Cmd_RemoveCommand("-attack");
	Cmd_RemoveCommand("+button0");
	Cmd_RemoveCommand("-button0");
	Cmd_RemoveCommand("+button1");
	Cmd_RemoveCommand("-button1");
	Cmd_RemoveCommand("+button2");
	Cmd_RemoveCommand("-button2");
	Cmd_RemoveCommand("+button3");
	Cmd_RemoveCommand("-button3");
	Cmd_RemoveCommand("+button4");
	Cmd_RemoveCommand("-button4");
	Cmd_RemoveCommand("+button5");
	Cmd_RemoveCommand("-button5");
	Cmd_RemoveCommand("+button6");
	Cmd_RemoveCommand("-button6");
	Cmd_RemoveCommand("+button7");
	Cmd_RemoveCommand("-button7");
	Cmd_RemoveCommand("+button8");
	Cmd_RemoveCommand("-button8");
	Cmd_RemoveCommand("+button9");
	Cmd_RemoveCommand("-button9");
	Cmd_RemoveCommand("+button10");
	Cmd_RemoveCommand("-button10");
	Cmd_RemoveCommand("+button11");
	Cmd_RemoveCommand("-button11");
	Cmd_RemoveCommand("+button12");
	Cmd_RemoveCommand("-button12");
	Cmd_RemoveCommand("+button13");
	Cmd_RemoveCommand("-button13");
	Cmd_RemoveCommand("+button14");
	Cmd_RemoveCommand("-button14");
	Cmd_RemoveCommand("+mlook");
	Cmd_RemoveCommand("-mlook");

#ifdef USE_VOIP
	Cmd_RemoveCommand("+voiprecord");
	Cmd_RemoveCommand("-voiprecord");
#endif
}
