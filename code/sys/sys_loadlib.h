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

#ifdef DEDICATED
#	ifdef _WIN32
#		include <windows.h>
#		define Sys_LoadLibrary(f) (void*)LoadLibrary(f)
#		define Sys_UnloadLibrary(h) FreeLibrary((HMODULE)h)
#		define Sys_LoadFunction(h,fn) (void*)GetProcAddress((HMODULE)h,fn)
#		define Sys_LibraryError() "unknown"
#	else
#	include <dlfcn.h>
#		define Sys_LoadLibrary(f) dlopen(f,RTLD_NOW)
#		define Sys_UnloadLibrary(h) dlclose(h)
#		define Sys_LoadFunction(h,fn) dlsym(h,fn)
#		define Sys_LibraryError() dlerror()
#	endif
#else
#	ifdef __SWITCH__
/* Switch homebrew links game/cgame statically into the NRO (libnx has no
 * general-purpose runtime code loader). Sys_GetGameAPI / Sys_GetCGameAPI
 * call the statically-linked GetGameAPI / GetCGameAPI directly via a
 * __SWITCH__ branch in sys_main_new.c, so these loader entry points are
 * never exercised — stub them so the header still compiles. */
#		ifdef USE_INTERNAL_SDL_HEADERS
#			include "SDL.h"
#		else
#			include <SDL.h>
#		endif
#		define Sys_LoadLibrary(f) ((void*)0)
#		define Sys_UnloadLibrary(h) ((void)(h))
#		define Sys_LoadFunction(h,fn) ((void*)0)
#		define Sys_LibraryError() "static-link (no dlopen on Switch)"
#	elif defined(__vita__)
/* SDL2-Vita has SDL_LoadObject stubbed. Use the custom dlopen wrapper
 * around sceKernelLoadStartModule (psp2/dll_psp2.c) for module
 * loading. We still pull SDL.h in because Sys_*Clipboard/etc. callers
 * via sys_loadlib.h's inclusion chain expect SDL to be visible. */
#		ifdef USE_INTERNAL_SDL_HEADERS
#			include "SDL.h"
#		else
#			include <SDL.h>
#		endif
#		include "psp2/dll_psp2.h"
#		define Sys_LoadLibrary(f) dlopen(f, RTLD_NOW)
#		define Sys_UnloadLibrary(h) dlclose(h)
#		define Sys_LoadFunction(h,fn) dlsym(h,fn)
#		define Sys_LibraryError() dlerror()
#	else
#		ifdef USE_INTERNAL_SDL_HEADERS
#			include "SDL.h"
#			include "SDL_loadso.h"
#		else
#			include <SDL.h>
#			include <SDL_loadso.h>
#		endif
#		define Sys_LoadLibrary(f) SDL_LoadObject(f)
#		define Sys_UnloadLibrary(h) SDL_UnloadObject(h)
#		define Sys_LoadFunction(h,fn) SDL_LoadFunction(h,fn)
#		define Sys_LibraryError() SDL_GetError()
#	endif
#endif

void * QDECL Sys_LoadDll(const char *name, qboolean useSystemLib);
