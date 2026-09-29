/*
===========================================================================
Copyright (C) 2024 the OpenMoHAA team

This file is part of OpenMoHAA source code.

OpenMoHAA source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

OpenMoHAA source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with OpenMoHAA source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/

#include "cl_ui.h"
#include "../qcommon/localization.h"

#include "../server/server.h"
#ifdef __vita__
#include <psp2/apputil.h>
#include <psp2/system_param.h>
#endif

CLASS_DECLARATION(UIWidget, View3D, NULL) {
    {&W_Activated,     &View3D::OnActivate  },
    {&W_Deactivated,   &View3D::OnDeactivate},
    {&W_LeftMouseDown, &View3D::Pressed     },
    {NULL,             NULL                 }
};

cvar_t *subs[MAX_SUBTITLES];
cvar_t *teams[MAX_SUBTITLES];
float   fadeTime[MAX_SUBTITLES];
float   subLife[MAX_SUBTITLES];
float   alpha[MAX_SUBTITLES];
char    oldStrings[MAX_SUBTITLES][2048];

#if defined(__vita__) || defined(__SWITCH__)
/* ============================================================
 * VITA PERF MENU — categorised tree of EVERY perf-relevant cvar.
 *
 * Press Select on the Vita pad in-game to open. Navigation:
 *   D-pad LEFT/RIGHT  -> change category
 *   D-pad UP/DOWN     -> select item within category
 *   Cross (A)         -> toggle / cycle item
 *   Circle (B)        -> close menu
 *   Select (Back)     -> close menu
 *
 * The menu is drawn as an overlay during normal gameplay. All keys
 * are swallowed while open so accidental fire/jump don't happen.
 * ============================================================ */

/* A named level: `value` is matched against the item's cvar to show the current level,
 * `cmd` sets it (and any companion cvars). Values/names follow the original game's
 * Options menus (ui/video options.urc, ui/advancedoptions.urc) so both menus agree. */
struct VitaPerfChoice {
    const char *name;
    const char *value;
    const char *cmd;
};

struct VitaPerfMenuItem {
    const char *label;
    const char *cvarName;
    qboolean    inverted; /* "ON" means cvar=0 (e.g. r_fastsky) */
    int         cycleMax; /* if > 0, cycles 0..cycleMax instead of 0/1 toggle */
    const char *cmd;      /* if set, A runs this console command instead of touching a cvar */
    qboolean    restart;  /* only read at renderer/level load: needs a vid_restart (latched cvars are detected) */
    const VitaPerfChoice *choices; /* if set, A cycles these named levels */
    int         numChoices;
};

#define VPM_CHOICES(arr) arr, (int)(sizeof(arr) / sizeof(arr[0]))

/* ---------- QUALITY (named levels + presets) ---------- */
/* r_picmip 0 ("High") is not offered: m1l1 needs ~121 MB of textures at 0 against the
 * Vita's ~112 MB of video memory (41 MB at 1). The renderer also clamps it (r_picmip_cap). */
static const VitaPerfChoice g_pcTextures[] = {
    { "Lowest", "3", "seta r_picmip 3" },
    { "Low",    "2", "seta r_picmip 2" },
    { "Medium", "1", "seta r_picmip 1" },
};
static const VitaPerfChoice g_pcModels[] = {
    { "Lowest",  "0.25", "seta r_lodscale 0.25; seta r_lodcap 0.25; seta r_lodviewmodelcap 0.25" },
    { "Low",     "0.35", "seta r_lodscale 0.35; seta r_lodcap 0.35; seta r_lodviewmodelcap 0.25" },
    { "Medium",  "0.45", "seta r_lodscale 0.45; seta r_lodcap 0.35; seta r_lodviewmodelcap 0.45" },
    { "High",    "0.55", "seta r_lodscale 0.55; seta r_lodcap 0.5; seta r_lodviewmodelcap 0.55" },
    { "Higher",  "0.9",  "seta r_lodscale 0.9; seta r_lodcap 0.9; seta r_lodviewmodelcap 0.9" },
    { "Highest", "1.1",  "seta r_lodscale 1.1; seta r_lodcap 1.0; seta r_lodviewmodelcap 1.0" },
};
static const VitaPerfChoice g_pcDistant[] = {
    { "Lowest",  "4", "seta r_lodbias 4" },
    { "Low",     "3", "seta r_lodbias 3" },
    { "Medium",  "2", "seta r_lodbias 2" },
    { "High",    "1", "seta r_lodbias 1" },
    { "Highest", "0", "seta r_lodbias 0" },
};
static const VitaPerfChoice g_pcCurves[] = {  /* lower r_subdivisions = smoother */
    { "Lowest",  "20", "seta r_subdivisions 20" },
    { "Low",     "10", "seta r_subdivisions 10" },
    { "Medium",  "4",  "seta r_subdivisions 4" },
    { "High",    "3",  "seta r_subdivisions 3" },
};
static const VitaPerfChoice g_pcEffects[] = {
    { "Minimum", "0.2",  "seta cg_effectdetail 0.2; seta vss_maxcount 25" },
    { "Lower",   "0.3",  "seta cg_effectdetail 0.3; seta vss_maxcount 23" },
    { "Low",     "0.5",  "seta cg_effectdetail 0.5; seta vss_maxcount 22" },
    { "Medium",  "0.7",  "seta cg_effectdetail 0.7; seta vss_maxcount 20" },
    { "High",    "0.8",  "seta cg_effectdetail 0.8; seta vss_maxcount 18" },
    { "Higher",  "0.95", "seta cg_effectdetail 0.95; seta vss_maxcount 15" },
    { "Max",     "1.0",  "seta cg_effectdetail 1.0; seta vss_maxcount 10" },
};
static const VitaPerfChoice g_pcTerrain[] = {
    { "Low",    "10", "seta ter_error 10; seta ter_maxlod 3; seta ter_maxtris 16384" },
    { "Medium", "9",  "seta ter_error 9; seta ter_maxlod 4; seta ter_maxtris 16384" },
    { "High",   "7",  "seta ter_error 7; seta ter_maxlod 5; seta ter_maxtris 16384" },
    { "Max",    "4",  "seta ter_error 4; seta ter_maxlod 6; seta ter_maxtris 24576" },
};
static const VitaPerfChoice g_pcFilter[] = {
    { "Bilinear",  "gl_linear_mipmap_nearest", "seta r_texturemode gl_linear_mipmap_nearest" },
    { "Trilinear", "gl_linear_mipmap_linear",  "seta r_texturemode gl_linear_mipmap_linear" },
};
static const VitaPerfChoice g_pcShadows[] = {
    { "Off",     "0", "seta cg_shadows 0" },
    { "Blob",    "1", "seta cg_shadows 1" },
    { "Precise", "2", "seta cg_shadows 2" },
};
/* r_vita_smp_serial: where the main thread waits for the render thread (diagnostic). */
static const VitaPerfChoice g_pcSmpSerial[] = {
    { "Off",      "0", "set r_vita_smp_serial 0" },  /* full overlap (normal) */
    { "Full",     "1", "set r_vita_smp_serial 1" },  /* no overlap at all */
    { "Game",     "2", "set r_vita_smp_serial 2" },  /* overlaps only server/game/sound/input */
    { "Scene",    "3", "set r_vita_smp_serial 3" },  /* + cgame scene building */
    /* 4+: wait at the first call of a kind inside the cgame window */
    { "cg:Trace", "4", "set r_vita_smp_serial 4" },
    { "cg:Add",   "5", "set r_vita_smp_serial 5" },
    { "cg:Pose",  "6", "set r_vita_smp_serial 6" },
    { "cg:Light", "7", "set r_vita_smp_serial 7" },
    { "cg:Marks", "8", "set r_vita_smp_serial 8" },
    /* 9+: wait inside R_RenderView, in frame order */
    { "rv:World", "9",  "set r_vita_smp_serial 9" },
    { "rv:Terr",  "10", "set r_vita_smp_serial 10" },
    { "rv:Static","11", "set r_vita_smp_serial 11" },
    { "rv:Polys", "12", "set r_vita_smp_serial 12" },
    { "rv:Ents",  "13", "set r_vita_smp_serial 13" },
    { "rv:Sort",  "14", "set r_vita_smp_serial 14" },
};
/* g_subtitle (cgame): which speech gets a subtitle. */
static const VitaPerfChoice g_pcSubtitles[] = {
    { "German only", "0", "seta g_subtitle 0" }, /* the game's default */
    { "Nearby",      "1", "seta g_subtitle 1" },
    { "All",         "2", "seta g_subtitle 2" },
};

/* Game text language (misc/vita/lang/<code>.txt). "" = the player's own game data. */
static const VitaPerfChoice g_pcLanguage[] = {
    { "Game",      "",   "seta vita_language \"\"; vita_reloadlanguage" },
    { "Português", "pt", "seta vita_language pt; vita_reloadlanguage" },
};
static const VitaPerfChoice g_pcWeapon[] = {
    { "None",     "0", "seta cg_drawviewmodel 0" },
    { "Gun Only", "1", "seta cg_drawviewmodel 1" },
    { "Full",     "2", "seta cg_drawviewmodel 2" },
};

#define VPM_PRESET_PERF \
    "seta r_picmip 2; seta r_lodscale 0.35; seta r_lodcap 0.35; seta r_lodviewmodelcap 0.25; seta r_lodbias 2; " \
    "seta r_subdivisions 10; seta cg_effectdetail 0.5; seta vss_maxcount 22; seta ter_error 10; seta ter_maxlod 3; " \
    "seta r_texturemode gl_linear_mipmap_nearest; seta cg_shadows 0; seta r_dynamiclight 0; seta cg_marks_add 0; seta com_blood 1"
#define VPM_PRESET_BALANCED \
    "seta r_picmip 1; seta r_lodscale 0.45; seta r_lodcap 0.35; seta r_lodviewmodelcap 0.45; seta r_lodbias 1; " \
    "seta r_subdivisions 4; seta cg_effectdetail 0.7; seta vss_maxcount 20; seta ter_error 9; seta ter_maxlod 4; " \
    "seta r_texturemode gl_linear_mipmap_nearest; seta cg_shadows 1; seta r_dynamiclight 1; seta cg_marks_add 1; seta com_blood 1"
/* Quality = the highest value of every graphics choice in this menu. */
#define VPM_PRESET_QUALITY \
    "seta r_picmip 1; seta r_lodscale 1.1; seta r_lodcap 1.0; seta r_lodviewmodelcap 1.0; seta r_lodbias 0; " \
    "seta r_subdivisions 3; seta cg_effectdetail 1.0; seta vss_maxcount 10; " \
    "seta ter_error 4; seta ter_maxlod 6; seta ter_maxtris 24576; " \
    "seta r_texturemode gl_linear_mipmap_linear; seta cg_shadows 2; seta r_dynamiclight 1; seta r_flares 1; " \
    "seta cg_marks_add 1; seta com_blood 1"

/* ---------- translations ----------
 * Every string the menu shows, in English and Brazilian Portuguese, plus a one-line
 * description for items. Looked up by the English text; the Portuguese one is used
 * when the Vita's system language is Portuguese. Strings are UTF-8 here and converted
 * to the game fonts' Latin-1 when drawn. */
struct VitaMenuText {
    const char *en;
    const char *pt;
    const char *descEn;
    const char *descPt;
};

static const VitaMenuText g_vmTexts[] = {
    /* screens, tabs, hints */
    { "VITA SETTINGS", "CONFIGURAÇÕES DO VITA" },
    { "DEBUG", "DEBUG" },
    { "GRAPHICS", "GRÁFICOS" },
    { "DISPLAY", "TELA" },
    { "CONTROLS", "CONTROLES" },
    { "SYSTEM", "SISTEMA" },
    { "RENDER", "RENDER" },
    { "WORLD", "MUNDO" },
    { "GAME", "JOGO" },
    { "LEVELS", "FASES" },
    { "DIAG", "DIAG" },
    { "L/R: tab   D-pad: select   X: change   O: close",
      "L/R: aba   D-pad: escolher   X: alterar   O: fechar" },
    { "* applied when the menu closes (video restart)",
      "* aplicado ao fechar o menu (reinicia o vídeo)" },
    { "* changed: applied when the menu closes",
      "* alterado: aplicado ao fechar o menu" },
    { "Custom", "Personalizado" },
    { "On", "Sim" },
    { "Off", "Não" },

    /* choice names */
    { "Lowest", "Mínimo" }, { "Low", "Baixo" }, { "Medium", "Médio" }, { "High", "Alto" },
    { "Higher", "Muito alto" }, { "Highest", "Máximo" }, { "Minimum", "Mínimo" }, { "Lower", "Mais baixo" },
    { "Max", "Máximo" }, { "Bilinear", "Bilinear" }, { "Trilinear", "Trilinear" }, { "Blob", "Simples" },
    { "Game", "Do jogo" }, { "German only", "Só alemão" }, { "Nearby", "Próximas" }, { "All", "Todas" }, { "Precise", "Precisa" }, { "None", "Nenhum" }, { "Gun Only", "Só a arma" }, { "Full", "Completo" },

    /* SETTINGS: graphics */
    { "Preset: Performance", "Predefinição: Desempenho",
      "Lowest detail for the highest frame rate.",
      "Menos detalhe para o maior FPS." },
    { "Preset: Balanced", "Predefinição: Equilibrado",
      "Recommended: good image with smooth gameplay.",
      "Recomendado: boa imagem com jogo fluido." },
    { "Preset: Quality", "Predefinição: Qualidade",
      "Every option at its highest; lower frame rate in combat.",
      "Todas as opções no máximo; FPS menor em combate." },
    { "Texture Quality", "Qualidade das texturas",
      "Texture resolution. Higher uses more memory and loads slower.",
      "Resolução das texturas. Mais alto usa mais memória e carrega mais devagar." },
    { "Model Detail", "Detalhe dos modelos",
      "Polygon detail of soldiers and objects.",
      "Detalhe dos polígonos de soldados e objetos." },
    { "Distant Detail", "Detalhe à distância",
      "How early far models lose detail.",
      "Quão cedo os modelos distantes perdem detalhe." },
    { "Curve Detail", "Detalhe das curvas",
      "Smoothness of curved walls and arches.",
      "Suavidade de paredes curvas e arcos." },
    { "Effect Detail", "Detalhe dos efeitos",
      "Amount of smoke, debris and particles.",
      "Quantidade de fumaça, destroços e partículas." },
    { "Terrain Detail", "Detalhe do terreno",
      "Detail of outdoor terrain.",
      "Detalhe do terreno em áreas abertas." },
    { "Texture Filter", "Filtro de texturas",
      "Trilinear blends texture detail levels more smoothly.",
      "Trilinear suaviza a transição entre níveis de detalhe das texturas." },
    { "Shadows", "Sombras",
      "Precise: shadows cast by the lights. Blob: a simple shadow. Off is fastest.",
      "Precisa: sombras projetadas pelas luzes. Simples: uma sombra só. Desligada é mais rápida." },
    { "Dynamic Lights", "Luzes dinâmicas",
      "Light from muzzle flashes, explosions and fires.",
      "Luz de tiros, explosões e fogo." },
    { "Lens Flares", "Reflexos de luz",
      "Glare from the sun and bright lights.",
      "Brilho do sol e de luzes fortes." },
    { "Decals (Marks)", "Marcas",
      "Bullet holes, blood and scorch marks on walls.",
      "Buracos de bala, sangue e marcas de explosão nas paredes." },

    /* SETTINGS: display */
    { "Show FPS", "Mostrar FPS",
      "Frame rate counter in the corner of the screen.",
      "Contador de quadros por segundo no canto da tela." },
    { "HUD", "HUD",
      "Health, ammo and compass on screen.",
      "Vida, munição e bússola na tela." },
    { "Crosshair", "Mira",
      "Show the crosshair.",
      "Mostra a mira." },
    { "Weapon Model", "Modelo da arma",
      "Show the weapon (and hands) in first person.",
      "Mostra a arma (e as mãos) em primeira pessoa." },
    { "Blood / Gore", "Sangue",
      "Blood effects.",
      "Efeitos de sangue." },
    { "Subtitles", "Legendas",
      "Which speech gets subtitles: German only (game default), everyone nearby, or everyone.",
      "Quais falas têm legenda: só alemão (padrão do jogo), todas por perto ou todas." },
    { "Text Language", "Idioma do texto",
      "Language of the game's text: menus, messages, HUD. Voices stay as they are.",
      "Idioma dos textos do jogo: menus, mensagens, HUD. As vozes não mudam." },

    /* SETTINGS: controls */
    { "Look Sens (hip)", "Sensibilidade (normal)",
      "Right stick look speed when not aiming.",
      "Velocidade do analógico direito sem mirar." },
    { "Look Sens (aim)", "Sensibilidade (mirando)",
      "Right stick look speed while aiming down the sights.",
      "Velocidade do analógico direito mirando." },
    { "Crosshair (hip)", "Mira (normal)",
      "Show the crosshair when not aiming.",
      "Mostra a mira sem mirar." },
    { "Crosshair (aim)", "Mira (mirando)",
      "Show the crosshair while aiming.",
      "Mostra a mira mirando." },

    /* SETTINGS: system */
    { "Restore defaults", "Restaurar padrão",
      "Back to the port's recommended settings.",
      "Volta às configurações recomendadas do port." },
    { "Debug menu", "Menu de debug",
      "Renderer switches, level select, cheats and diagnostics.",
      "Opções do renderer, seleção de fases, trapaças e diagnóstico." },
    { "Close", "Fechar", "", "" },

    /* DEBUG: render */
    { "Render thread", "Render thread",
      "Draws on its own CPU core. Off is slower.",
      "Desenha num núcleo próprio da CPU. Desligado é mais lento." },
    { "GPU skinning", "GPU skinning",
      "Animates characters on the GPU. Off is slower.",
      "Anima os personagens na GPU. Desligado é mais lento." },
    { "World VBO", "VBO do mapa",
      "Keeps the map geometry on the GPU.",
      "Mantém a geometria do mapa na GPU." },
    { "Force multitexture", "Forçar multitextura",
      "Draws walls and their lighting in one pass.",
      "Desenha paredes e iluminação numa passada só." },
    { "Render thr. serial", "Render thread serial",
      "Diagnostic: limits the render thread's overlap with the game.",
      "Diagnóstico: limita a sobreposição da render thread com o jogo." },
    { "Engine 2D Pass", "Passo 2D",
      "Diagnostic: turns all 2D drawing off.",
      "Diagnóstico: desliga todo o desenho 2D." },
    { "Server thread", "Thread do servidor",
      "Experimental: game logic on its own CPU core.",
      "Experimental: lógica do jogo num núcleo próprio." },

    /* DEBUG: world */
    { "BSP World", "Mapa (BSP)", "", "" },
    { "Brush Models", "Brush models", "", "" },
    { "Static Models", "Modelos estáticos", "", "" },
    { "Static Polys", "Polígonos estáticos", "", "" },
    { "Entity Polys", "Polígonos de entidades", "", "" },
    { "Curves", "Curvas", "", "" },
    { "Fast Sky", "Céu simples", "", "" },
    { "Sky Box", "Skybox", "", "" },
    { "DLight Backfaces", "Luzes nas costas", "", "" },

    /* DEBUG: game */
    { "Main Menu", "Menu principal", "", "" },
    { "Restart Level", "Reiniciar fase", "", "" },
    { "Suicide (kill)", "Suicídio", "", "" },
    { "Cheats ON", "Trapaças ligadas", "", "" },
    { "Cheats OFF", "Trapaças desligadas", "", "" },
    { "God Mode", "Modo deus", "", "" },
    { "Noclip", "Atravessar paredes", "", "" },
    { "Notarget", "Invisível para a IA", "", "" },
    { "Give All", "Dar tudo", "", "" },
    { "Give Ammo", "Dar munição", "", "" },
    { "Give Health", "Dar vida", "", "" },

    /* DEBUG: diagnostics */
    { "NO REFRESH (perf test)", "SEM DESENHO (teste)",
      "Skips all rendering, to measure CPU cost alone.",
      "Pula todo o desenho, para medir só a CPU." },
    { "Show Tris", "Mostrar triângulos", "", "" },
    { "Show Normals", "Mostrar normais", "", "" },
    { "r_speeds Print", "r_speeds", "", "" },
    { "com_speeds Print", "com_speeds", "", "" },
    { "Perf log", "Log de desempenho",
      "Timing lines in boot.log every second.",
      "Linhas de tempo no boot.log a cada segundo." },
    { "Back to settings", "Voltar às configurações", "", "" },
};

static qboolean VitaMenu_IsPortuguese(void)
{
#ifdef __vita__
    static int s_lang = -1;
    /* A Text Language chosen in the menu wins over the system language. */
    const char *textLang = Cvar_VariableString("vita_language");
    if (textLang[0]) {
        return !Q_stricmp(textLang, "pt");
    }
    if (s_lang < 0) {
        int lang = 0;
        s_lang = 0;
        if (sceAppUtilSystemParamGetInt(SCE_SYSTEM_PARAM_ID_LANG, &lang) < 0) {
            /* AppUtil not initialised yet by anyone else: do it and ask again. */
            SceAppUtilInitParam initParam;
            SceAppUtilBootParam bootParam;
            memset(&initParam, 0, sizeof(initParam));
            memset(&bootParam, 0, sizeof(bootParam));
            sceAppUtilInit(&initParam, &bootParam);
            lang = -1;
            sceAppUtilSystemParamGetInt(SCE_SYSTEM_PARAM_ID_LANG, &lang);
        }
        if (lang >= 0
            && (lang == SCE_SYSTEM_PARAM_LANG_PORTUGUESE_PT || lang == SCE_SYSTEM_PARAM_LANG_PORTUGUESE_BR)) {
            s_lang = 1;
        }
    }
    return s_lang == 1;
#else
    return qfalse;
#endif
}

static const VitaMenuText *VitaMenu_FindText(const char *en)
{
    for (size_t i = 0; i < sizeof(g_vmTexts) / sizeof(g_vmTexts[0]); i++) {
        if (!strcmp(g_vmTexts[i].en, en)) {
            return &g_vmTexts[i];
        }
    }
    return NULL;
}

/* The text in the system language (English when there is no translation). */
static const char *VT(const char *en)
{
    const VitaMenuText *t;
    if (!en || !VitaMenu_IsPortuguese()) {
        return en;
    }
    t = VitaMenu_FindText(en);
    return (t && t->pt) ? t->pt : en;
}

/* The item's description in the system language ("" if none). */
static const char *VD(const char *en)
{
    const VitaMenuText *t = VitaMenu_FindText(en);
    if (!t) {
        return "";
    }
    if (VitaMenu_IsPortuguese()) {
        return t->descPt ? t->descPt : "";
    }
    return t->descEn ? t->descEn : "";
}

/* UTF-8 -> Latin-1 for the game fonts (characters beyond Latin-1 become '?'). */
static const char *VitaMenu_Latin1(const char *s)
{
    static char buf[4][256];
    static int  which;
    char       *out = buf[which++ & 3];
    int         n   = 0;

    while (*s && n < 255) {
        unsigned char c = (unsigned char)*s;
        if (c < 0x80) {
            out[n++] = (char)c;
            s++;
        } else if ((c & 0xE0) == 0xC0 && s[1]) {
            unsigned int cp = ((c & 0x1F) << 6) | ((unsigned char)s[1] & 0x3F);
            out[n++] = cp < 0x100 ? (char)cp : '?';
            s += 2;
        } else {
            out[n++] = '?';
            s++;
            while ((*s & 0xC0) == 0x80) {
                s++;
            }
        }
    }
    out[n] = 0;
    return out;
}

/* ---------- SETTINGS (player-facing) ---------- */
#define VPM_DEFAULTS "exec vita_defaults.cfg"

static VitaPerfMenuItem g_smGraphics[] = {
    { "Preset: Performance",  NULL, qfalse, 0, VPM_PRESET_PERF,     qtrue },
    { "Preset: Balanced",     NULL, qfalse, 0, VPM_PRESET_BALANCED, qtrue },
    { "Preset: Quality",      NULL, qfalse, 0, VPM_PRESET_QUALITY,  qtrue },
    { "Texture Quality",  "r_picmip",        qfalse, 0, NULL, qfalse, VPM_CHOICES(g_pcTextures) },
    { "Model Detail",     "r_lodscale",      qfalse, 0, NULL, qfalse, VPM_CHOICES(g_pcModels) },
    { "Distant Detail",   "r_lodbias",       qfalse, 0, NULL, qfalse, VPM_CHOICES(g_pcDistant) },
    { "Curve Detail",     "r_subdivisions",  qfalse, 0, NULL, qfalse, VPM_CHOICES(g_pcCurves) },
    { "Effect Detail",    "cg_effectdetail", qfalse, 0, NULL, qfalse, VPM_CHOICES(g_pcEffects) },
    { "Terrain Detail",   "ter_error",       qfalse, 0, NULL, qtrue,  VPM_CHOICES(g_pcTerrain) },
    { "Texture Filter",   "r_texturemode",   qfalse, 0, NULL, qtrue,  VPM_CHOICES(g_pcFilter) },
    { "Shadows",          "cg_shadows",      qfalse, 0, NULL, qfalse, VPM_CHOICES(g_pcShadows) },
    { "Dynamic Lights",   "r_dynamiclight",  qfalse, 0 },
    { "Lens Flares",      "r_flares",        qfalse, 0 },
    { "Decals (Marks)",   "cg_marks_add",    qfalse, 0 },
};

static VitaPerfMenuItem g_smDisplay[] = {
    { "Show FPS",         "fps",             qfalse, 0 },
    { "HUD",              "cg_hud",          qfalse, 0 },
    { "Crosshair",        "ui_crosshair",    qfalse, 0 },
    { "Weapon Model",     "cg_drawviewmodel",qfalse, 0, NULL, qfalse, VPM_CHOICES(g_pcWeapon) },
    { "Blood / Gore",     "com_blood",       qfalse, 0 },
    { "Subtitles",        "g_subtitle",      qfalse, 0, NULL, qfalse, VPM_CHOICES(g_pcSubtitles) },
    { "Text Language",    "vita_language",   qfalse, 0, NULL, qfalse, VPM_CHOICES(g_pcLanguage) },
};

static VitaPerfMenuItem g_smControls[] = {
    { "Look Sens (hip)",  "vita_hip_sens",     qfalse, 10 }, /* 0..10 = 0.0x..1.0x look speed while NOT aiming */
    { "Look Sens (aim)",  "vita_aim_sens",     qfalse, 10 }, /* 0..10 = 0.0x..1.0x look speed while aiming */
    { "Crosshair (hip)",  "cg_crosshair_hip",  qfalse, 0 },
    { "Crosshair (aim)",  "cg_crosshair_zoom", qfalse, 0 },
};

/* SYSTEM items run menu actions (see VitaPerfMenu_Action), not console commands. */
#define VPM_ACTION_DEFAULTS "@defaults"
#define VPM_ACTION_DEBUG    "@debug"
#define VPM_ACTION_SETTINGS "@settings"
#define VPM_ACTION_CLOSE    "@close"

static VitaPerfMenuItem g_smSystem[] = {
    { "Restore defaults", NULL, qfalse, 0, VPM_ACTION_DEFAULTS },
    { "Debug menu",       NULL, qfalse, 0, VPM_ACTION_DEBUG },
    { "Close",            NULL, qfalse, 0, VPM_ACTION_CLOSE },
};

/* ---------- DEBUG (developer) ---------- */
static VitaPerfMenuItem g_dmRender[] = {
    { "Render thread",       "r_vita_rthread",      qfalse, 0, NULL, qtrue },
    { "GPU skinning",        "r_vita_gpu_skinning", qfalse, 0, NULL, qtrue },
    { "World VBO",           "r_vita_vbo_world",    qfalse, 0, NULL, qtrue },
    { "Force multitexture",  "r_vita_force_mtex",   qfalse, 0 },
    { "Render thr. serial",  "r_vita_smp_serial",   qfalse, 0, NULL, qfalse, VPM_CHOICES(g_pcSmpSerial) },
    { "Server thread",       "sv_vita_thread",      qfalse, 0 },
    { "Engine 2D Pass",      "vita_skip_draw2d",    qtrue,  0 }, /* ON = normal, OFF = stripped */
};

static VitaPerfMenuItem g_dmWorld[] = {
    { "BSP World",        "r_drawworld",          qfalse, 0 },
    { "Brush Models",     "r_drawbrushes",        qfalse, 0 },
    { "Static Models",    "r_drawstaticmodels",   qfalse, 0 },
    { "Static Polys",     "r_drawstaticmodelpoly",qfalse, 0 },
    { "Entity Polys",     "r_drawentitypoly",     qfalse, 0 },
    { "Curves",           "r_nocurves",           qtrue,  0 }, /* inverted: ON when r_nocurves=0 */
    { "Fast Sky",         "r_fastsky",            qfalse, 0 },
    { "Sky Box",          "r_drawSun",            qfalse, 0 },
    { "DLight Backfaces", "r_dlightBacks",        qfalse, 0 },
    /* r_vertexLight / r_lightmap / r_drawSpheres removed (2026-09-23): debug lighting
     * modes that need a vid_restart; toggled live with the world VBO + collapsed
     * multitexture on, they turned every BSP surface into flat garbage colours. */
};

static VitaPerfMenuItem g_dmGame[] = {
    { "Main Menu",        NULL, qfalse, 0, "disconnect" },
    { "Restart Level",    NULL, qfalse, 0, "restart" },
    { "Suicide (kill)",   NULL, qfalse, 0, "kill" },
    { "Cheats ON",        NULL, qfalse, 0, "cheats 1" },
    { "Cheats OFF",       NULL, qfalse, 0, "cheats 0" },
    { "God Mode",         NULL, qfalse, 0, "god" },
    { "Noclip",           NULL, qfalse, 0, "noclip" },
    { "Notarget",         NULL, qfalse, 0, "notarget" },
    { "Give All",         NULL, qfalse, 0, "give all" },
    { "Give Ammo",        NULL, qfalse, 0, "give ammo" },
    { "Give Health",      NULL, qfalse, 0, "give health" },
};

/* Level loader. Same campaign list as the Switch dev menu (cl_scrn.cpp). */
#define VFASE(n) { n, NULL, qfalse, 0, "spmap " n }
static VitaPerfMenuItem g_dmLevels[] = {
    VFASE("training"),
    VFASE("m1l1"), VFASE("m1l2a"), VFASE("m1l2b"), VFASE("m1l3a"), VFASE("m1l3b"), VFASE("m1l3c"),
    VFASE("m2l1"), VFASE("m2l2a"), VFASE("m2l2b"), VFASE("m2l2c"), VFASE("m2l3"),
    VFASE("m3l1a"), VFASE("m3l1b"), VFASE("m3l2"), VFASE("m3l3"),
    VFASE("m4l0"), VFASE("m4l1"), VFASE("m4l2"), VFASE("m4l3"),
    VFASE("m5l1a"), VFASE("m5l1b"), VFASE("m5l2a"), VFASE("m5l2b"), VFASE("m5l3"),
    VFASE("m6l1a"), VFASE("m6l1b"), VFASE("m6l1c"), VFASE("m6l2a"), VFASE("m6l2b"),
    VFASE("m6l3a"), VFASE("m6l3b"), VFASE("m6l3c"), VFASE("m6l3d"), VFASE("m6l3e"),
};

static VitaPerfMenuItem g_dmDiag[] = {
    { "NO REFRESH (perf test)", "r_norefresh",    qfalse, 0 },
    /* "Skip Backend" and "Measure Overdraw" removed 2026-05-19: r_skipBackEnd freezes the
     * backend mid-frame, r_measureOverdraw trips a GL error in vitaGL's stencil emulation.
     * Both stay available from the console. */
    { "Show Tris",              "r_showtris",     qfalse, 0 },
    { "Show Normals",           "r_shownormals",  qfalse, 0 },
    { "r_speeds Print",         "r_speeds",       qfalse, 6 },
    { "com_speeds Print",       "com_speeds",     qfalse, 0 },
    { "Perf log",               "r_vita_perflog", qfalse, 0 },
    { "Back to settings",       NULL, qfalse, 0, VPM_ACTION_SETTINGS },
};

struct VitaPerfMenuCategory {
    const char        *label;
    VitaPerfMenuItem  *items;
    int                itemCount;
};

#define VPM_CAT(label, arr) { label, arr, (int)(sizeof(arr) / sizeof(arr[0])) }

static VitaPerfMenuCategory g_smCats[] = {
    VPM_CAT("GRAPHICS", g_smGraphics),
    VPM_CAT("DISPLAY",  g_smDisplay),
    VPM_CAT("CONTROLS", g_smControls),
    VPM_CAT("SYSTEM",   g_smSystem),
};

static VitaPerfMenuCategory g_dmCats[] = {
    VPM_CAT("RENDER", g_dmRender),
    VPM_CAT("WORLD",  g_dmWorld),
    VPM_CAT("GAME",   g_dmGame),
    VPM_CAT("LEVELS", g_dmLevels),
    VPM_CAT("DIAG",   g_dmDiag),
};

/* The screen being shown: settings or debug. */
static VitaPerfMenuCategory *g_pmCats     = g_smCats;
static int                   g_pmCatCount = sizeof(g_smCats) / sizeof(g_smCats[0]);
static qboolean              g_pmDebug    = qfalse;

static qboolean g_pmActive   = qfalse;
static int      g_pmOpenTime = 0;       /* keys right after opening are ignored (see HandleKey) */
static qboolean g_pmNeedRestart = qfalse; /* a restart-only setting changed: vid_restart on close */
static int      g_pmCatIdx   = 0;
static int      g_pmItemIdx  = 0;
static int      g_pmScroll   = 0;       /* first visible item (LEVELS is long) */
#define VPM_VISIBLE 12                  /* items shown at once */

static void VitaPerfMenu_SetScreen(qboolean debug)
{
    g_pmDebug    = debug;
    g_pmCats     = debug ? g_dmCats : g_smCats;
    g_pmCatCount = debug ? (int)(sizeof(g_dmCats) / sizeof(g_dmCats[0])) : (int)(sizeof(g_smCats) / sizeof(g_smCats[0]));
    g_pmCatIdx   = 0;
    g_pmItemIdx  = 0;
    g_pmScroll   = 0;
}

static int VitaPerfMenu_GetValue(const VitaPerfMenuItem *it)
{
    if (!it->cvarName) return 0;
    return Cvar_VariableIntegerValue(it->cvarName);
}

/* Keep the selected item inside the visible window. */
static void VitaPerfMenu_ClampScroll(void)
{
    int n = g_pmCats[g_pmCatIdx].itemCount;
    if (g_pmItemIdx < g_pmScroll)               g_pmScroll = g_pmItemIdx;
    if (g_pmItemIdx >= g_pmScroll + VPM_VISIBLE) g_pmScroll = g_pmItemIdx - VPM_VISIBLE + 1;
    if (g_pmScroll < 0) g_pmScroll = 0;
    if (g_pmScroll > n - 1) g_pmScroll = (n > 0) ? n - 1 : 0;
}

/* The cvar's pending value if it is latched (applied at the next vid_restart). */
static const char *VitaPerfMenu_CvarString(const char *name)
{
    const cvar_t *cv = Cvar_FindVar(name);
    if (!cv) return "";
    return cv->latchedString ? cv->latchedString : cv->string;
}

static int VitaPerfMenu_ChoiceIndex(const VitaPerfMenuItem *it)
{
    const char *cur = VitaPerfMenu_CvarString(it->cvarName);
    for (int i = 0; i < it->numChoices; i++) {
        const char *v = it->choices[i].value;
        const qboolean numeric = (v[0] >= '0' && v[0] <= '9') || v[0] == '.';
        if (numeric ? (fabs(atof(cur) - atof(v)) < 0.001) : !Q_stricmp(cur, v)) {
            return i;
        }
    }
    return -1;
}

/* The item's current state: a named level, On/Off, n/max, or ">" for actions. */
static const char *VitaPerfMenu_GetStateStr(const VitaPerfMenuItem *it)
{
    static char buf[64];
    if (it->cmd) return ">";
    if (it->choices) {
        const int idx = VitaPerfMenu_ChoiceIndex(it);
        return VT(idx >= 0 ? it->choices[idx].name : "Custom");
    }
    int cur = VitaPerfMenu_GetValue(it);
    if (it->cycleMax > 0) {
        Com_sprintf(buf, sizeof(buf), "%d / %d", cur, it->cycleMax);
        return buf;
    }
    qboolean on = it->inverted ? (cur == 0) : (cur != 0);
    return VT(on ? "On" : "Off");
}

/* Runs "a; b; c" right now. Cbuf_ExecuteText(EXEC_NOW) runs its text as ONE command,
 * so presets like "seta r_picmip 2; seta r_lodscale 0.35; ..." used to set r_picmip to
 * the whole string ("must be numeric") and never reach the other cvars. */
static void VitaPerfMenu_RunNow(const char *cmds)
{
    char        one[256];
    const char *p = cmds;

    while (*p) {
        const char *end = strchr(p, ';');
        int         len = end ? (int)(end - p) : (int)strlen(p);

        if (len >= (int)sizeof(one)) {
            len = sizeof(one) - 1;
        }
        memcpy(one, p, len);
        one[len] = 0;
        Cbuf_ExecuteText(EXEC_NOW, va("%s\n", one));
        p += end ? len + 1 : len;
    }
}

static void VitaPerfMenu_ToggleItem(VitaPerfMenuItem *it)
{
    if (it->cmd || !it->cvarName) return;   /* action items have no cvar to toggle */
    if (it->choices) {
        const int      next = (VitaPerfMenu_ChoiceIndex(it) + 1) % it->numChoices;
        const cvar_t *cv;

        VitaPerfMenu_RunNow(it->choices[next].cmd);
        cv = Cvar_FindVar(it->cvarName);
        if (it->restart || (cv && (cv->flags & CVAR_LATCH))) {
            g_pmNeedRestart = qtrue;
        }
        Com_Printf("PERF-MENU: %s = %s\n", it->label, it->choices[next].name);
        return;
    }
    int cur = VitaPerfMenu_GetValue(it);
    int next;
    if (it->cycleMax > 0) {
        next = cur + 1;
        if (next > it->cycleMax) next = 0;
    } else {
        next = cur ? 0 : 1;
    }
    char buf[16];
    Com_sprintf(buf, sizeof(buf), "%d", next);
    /* Forced: several of these are CVAR_CHEAT (r_drawbrushes, r_nocurves, r_showtris...)
     * and a plain Cvar_Set is refused with cheats off -- the menu showed the new value
     * but nothing changed. (Cheat cvars still revert on the next map load.) The cvar is
     * archived so the choice persists across launches. */
    Cvar_Set2(it->cvarName, buf, qtrue);
    cvar_t *cv = Cvar_FindVar(it->cvarName);
    if (it->restart || (cv && (cv->flags & CVAR_LATCH))) {
        g_pmNeedRestart = qtrue;
    }
    if (cv) {
        cv->flags |= CVAR_ARCHIVE;
        cvar_modifiedFlags |= CVAR_ARCHIVE;  /* trigger config save */
    }
    Com_Printf("PERF-MENU: %s = %d (archived)\n", it->cvarName, next);
}

/* Settings that only take effect at renderer/level load (texture LOD, curve detail,
 * multitexture, world VBO, GPU skinning) are applied with one vid_restart when the
 * menu closes, instead of silently doing nothing until the next launch. */
static void VitaPerfMenu_Close(void)
{
    g_pmActive = qfalse;
    Com_Printf("PERF-MENU: CLOSED\n");
    if (g_pmNeedRestart) {
        g_pmNeedRestart = qfalse;
        Com_Printf("PERF-MENU: applying restart-only settings (vid_restart)\n");
        Cbuf_AddText("vid_restart\n");
    }
}

static void VitaPerfMenu_Open(qboolean debug)
{
    VitaPerfMenu_SetScreen(debug);
    g_pmActive   = qtrue;
    g_pmOpenTime = Sys_Milliseconds();
    Com_Printf("PERF-MENU: OPEN (%s)\n", debug ? "debug" : "settings");
}

void CL_VitaPerfMenu_Toggle_f(void)
{
    if (g_pmActive) {
        VitaPerfMenu_Close();
        return;
    }
    VitaPerfMenu_Open(qfalse);
}

/* "vitasettings" / "vitadebug": open a given screen (menus, LiveArea launch param). */
static void CL_VitaSettings_f(void)
{
    VitaPerfMenu_Open(qfalse);
}

static void CL_VitaDebug_f(void)
{
    VitaPerfMenu_Open(qtrue);
}

/* Notice shown instead of the multiplayer menus, which the console ports do not
 * support. Any button closes it. */
static qboolean g_vnActive;
static int      g_vnOpenTime;

void CL_VitaNotice_Multiplayer(void)
{
    g_vnActive   = qtrue;
    g_vnOpenTime = Sys_Milliseconds();
}

qboolean CL_VitaPerfMenu_IsActive(void)
{
    return g_pmActive || g_vnActive;
}

/* Menu actions (SYSTEM tab, "Back to settings"). */
static void VitaPerfMenu_Action(const char *action)
{
    if (!strcmp(action, VPM_ACTION_DEFAULTS)) {
        VitaPerfMenu_RunNow(VPM_DEFAULTS);
        g_pmNeedRestart = qtrue;
        VitaPerfMenu_Close();
    } else if (!strcmp(action, VPM_ACTION_DEBUG)) {
        VitaPerfMenu_SetScreen(qtrue);
    } else if (!strcmp(action, VPM_ACTION_SETTINGS)) {
        VitaPerfMenu_SetScreen(qfalse);
    } else if (!strcmp(action, VPM_ACTION_CLOSE)) {
        VitaPerfMenu_Close();
    }
}

/* Called from CL_KeyEvent. Returns true if the key was consumed. */
qboolean CL_VitaPerfMenu_HandleKey(int key, qboolean down)
{
    if (g_vnActive) {
        if (down && Sys_Milliseconds() - g_vnOpenTime >= 400) {
            g_vnActive = qfalse;
        }
        return qtrue;
    }
    if (!g_pmActive) {
        /* In the 2D menus the +vitaselect bind never fires (the UI eats the key),
         * so the double tap on Select is detected here instead. */
        static int s_uiSelectLastDown = -100000;
        if (down && key == K_PAD0_BACK && (Key_GetCatcher() & KEYCATCH_UI) && !UI_BindActive()) {
            const int now = Sys_Milliseconds();
            if (now - s_uiSelectLastDown < 350) {
                s_uiSelectLastDown = -100000;
                VitaPerfMenu_Open(qfalse);
                return qtrue;
            }
            s_uiSelectLastDown = now;
        }
        return qfalse;
    }
    if (!down) return qtrue;
    /* A press still held from whatever opened the menu (the LiveArea's Start button,
     * a double-tapped Select, the Options menu's Cross) must not act on the first item. */
    if (Sys_Milliseconds() - g_pmOpenTime < 400) return qtrue;

    static qboolean s_resolved = qfalse;
    static int      k_select   = -1;
    static int      k_start    = -1;
    static int      k_circle   = -1;
    static int      k_cross    = -1;
    static int      k_up       = -1;
    static int      k_down     = -1;
    static int      k_left     = -1;
    static int      k_right    = -1;
    static int      k_lshoulder = -1;
    static int      k_rshoulder = -1;
    if (!s_resolved) {
        s_resolved  = qtrue;
        k_select    = Key_StringToKeynum("PAD0_BACK");
        k_start     = Key_StringToKeynum("PAD0_START");
        k_circle    = Key_StringToKeynum("PAD0_B");
        k_cross     = Key_StringToKeynum("PAD0_A");
        k_up        = Key_StringToKeynum("PAD0_DPAD_UP");
        k_down      = Key_StringToKeynum("PAD0_DPAD_DOWN");
        k_left      = Key_StringToKeynum("PAD0_DPAD_LEFT");
        k_right     = Key_StringToKeynum("PAD0_DPAD_RIGHT");
        k_lshoulder = Key_StringToKeynum("PAD0_LEFTSHOULDER");
        k_rshoulder = Key_StringToKeynum("PAD0_RIGHTSHOULDER");
    }

    if (key == k_select || key == k_circle || key == k_start || key == K_ESCAPE) {
        VitaPerfMenu_Close();
        return qtrue;
    }
    if (key == k_left || key == k_lshoulder) {
        g_pmCatIdx--;
        if (g_pmCatIdx < 0) g_pmCatIdx = g_pmCatCount - 1;
        g_pmItemIdx = 0; g_pmScroll = 0;
        return qtrue;
    }
    if (key == k_right || key == k_rshoulder) {
        g_pmCatIdx++;
        if (g_pmCatIdx >= g_pmCatCount) g_pmCatIdx = 0;
        g_pmItemIdx = 0; g_pmScroll = 0;
        return qtrue;
    }
    if (key == k_up) {
        g_pmItemIdx--;
        if (g_pmItemIdx < 0) g_pmItemIdx = g_pmCats[g_pmCatIdx].itemCount - 1;
        VitaPerfMenu_ClampScroll();
        return qtrue;
    }
    if (key == k_down) {
        g_pmItemIdx++;
        if (g_pmItemIdx >= g_pmCats[g_pmCatIdx].itemCount) g_pmItemIdx = 0;
        VitaPerfMenu_ClampScroll();
        return qtrue;
    }
    if (key == k_cross) {
        VitaPerfMenuItem *it = &g_pmCats[g_pmCatIdx].items[g_pmItemIdx];
        if (it->cmd && it->cmd[0] == '@') {
            VitaPerfMenu_Action(it->cmd);
        } else if (it->cmd) {
            if (it->restart) {
                /* Quality preset: plain cvar sets, applied now so the vid_restart
                 * queued by Close sees them. */
                VitaPerfMenu_RunNow(it->cmd);
                g_pmNeedRestart = qtrue;
                VitaPerfMenu_Close();
            } else {
                /* Level load / cheat: close first so input returns to the game. */
                VitaPerfMenu_Close();
                Cbuf_AddText(va("%s\n", it->cmd));
            }
            Com_Printf("PERF-MENU: run '%s'\n", it->cmd);
        } else {
            VitaPerfMenu_ToggleItem(it);
        }
        return qtrue;
    }
    /* Swallow all other keys so gameplay binds don't fire. */
    return qtrue;
}

/* Wraps text into at most two lines of maxChars. */
static void VitaPerfMenu_Wrap(const char *text, int maxChars, char *line1, char *line2, int size)
{
    int len = (int)strlen(text);
    int cut;

    line1[0] = line2[0] = 0;
    if (len <= maxChars) {
        Q_strncpyz(line1, text, size);
        return;
    }
    for (cut = maxChars; cut > 0 && text[cut] != ' '; cut--) {
    }
    if (cut == 0) {
        cut = maxChars;
    }
    Q_strncpyz(line1, text, cut + 1 < size ? cut + 1 : size);
    Q_strncpyz(line2, text + cut + (text[cut] == ' ' ? 1 : 0), size);
}

/* Draw the menu overlay: in game from View3D::Draw2D, elsewhere from UI_Update. */
static void VitaNotice_Draw(class UIFont *menuFont, float screenW, float screenH)
{
    static const char *const s_en[] = {
        "Multiplayer is not available on this version.",
        "",
        "The Spearhead and Breakthrough expansions are not",
        "available yet either: this version plays the",
        "Allied Assault campaign.",
    };
    static const char *const s_pt[] = {
        "O multiplayer não está disponível nesta versão.",
        "",
        "As expansões Spearhead e Breakthrough também ainda",
        "não estão disponíveis: esta versão roda a campanha",
        "de Allied Assault.",
    };
    const vec4_t       bg   = {0.02f, 0.03f, 0.02f, 0.92f};
    const vec4_t       band = {0.30f, 0.26f, 0.12f, 0.95f};
    const qboolean     pt   = VitaMenu_IsPortuguese();
    const char *const *text = pt ? s_pt : s_en;
    const float        boxW = screenW < 540.0f ? screenW - 20.0f : 520.0f;
    const float        boxH = 200.0f;
    const float        boxX = (screenW - boxW) * 0.5f;
    const float        boxY = (screenH - boxH) * 0.5f;
    float              y;

    re.SetColor(bg);
    re.DrawBox(boxX, boxY, boxW, boxH);
    re.SetColor(band);
    re.DrawBox(boxX, boxY, boxW, 30.0f);
    re.SetColor(NULL);
    if (!menuFont) {
        return;
    }

    menuFont->setColor(UWhite);
    menuFont->Print(boxX + 12.0f, boxY + 7.0f, "MULTIPLAYER", -1, NULL);
    y = boxY + 44.0f;
    for (size_t i = 0; i < ARRAY_LEN(s_en); i++) {
        menuFont->Print(boxX + 16.0f, y, VitaMenu_Latin1(text[i]), -1, NULL);
        y += 20.0f;
    }
    menuFont->setColor(UYellow);
    menuFont->Print(boxX + 16.0f, boxY + boxH - 28.0f,
                    VitaMenu_Latin1(pt ? "Aperte qualquer botão para voltar." : "Press any button to go back."), -1, NULL);
    menuFont->setColor(UWhite);
}

void CL_VitaPerfMenu_Draw(class UIFont *menuFont, float screenW, float screenH)
{
    if (g_vnActive) {
        VitaNotice_Draw(menuFont, screenW, screenH);
        return;
    }
    if (!g_pmActive) return;

    const vec4_t bg     = {0.02f, 0.03f, 0.02f, 0.92f};
    const vec4_t band   = {0.30f, 0.26f, 0.12f, 0.95f};
    const vec4_t selBar = {0.45f, 0.38f, 0.12f, 0.60f};
    const float  boxW   = screenW < 620.0f ? screenW - 20.0f : 600.0f;
    const float  boxH   = screenH < 460.0f ? screenH - 20.0f : 440.0f;
    const float  boxX   = (screenW - boxW) * 0.5f;
    const float  boxY   = (screenH - boxH) * 0.5f;
    const float  lineH  = 20.0f;
    float        y;

    re.SetColor(bg);
    re.DrawBox(boxX, boxY, boxW, boxH);
    re.SetColor(band);
    re.DrawBox(boxX, boxY, boxW, 30.0f);

    if (!menuFont) {
        re.SetColor(NULL);
        return;
    }

    /* Title */
    menuFont->setColor(UWhite);
    menuFont->Print(boxX + 12.0f, boxY + 7.0f, VitaMenu_Latin1(VT(g_pmDebug ? "DEBUG" : "VITA SETTINGS")), -1, NULL);

    /* Tabs */
    y = boxY + 40.0f;
    float catX = boxX + 12.0f;
    for (int i = 0; i < g_pmCatCount; i++) {
        const char *label = VitaMenu_Latin1(VT(g_pmCats[i].label));
        menuFont->setColor(i == g_pmCatIdx ? UYellow : UWhite);
        menuFont->Print(catX, y, label, -1, NULL);
        catX += (float)strlen(label) * 9.0f + 18.0f;
    }
    y += 30.0f;

    /* Items, windowed [g_pmScroll, +VPM_VISIBLE) so long lists (LEVELS) scroll. */
    VitaPerfMenuCategory *cat = &g_pmCats[g_pmCatIdx];
    int last = g_pmScroll + VPM_VISIBLE;
    if (last > cat->itemCount) last = cat->itemCount;
    for (int i = g_pmScroll; i < last; i++) {
        const VitaPerfMenuItem *it = &cat->items[i];
        const cvar_t  *icv          = it->cvarName ? Cvar_FindVar(it->cvarName) : NULL;
        const qboolean needsRestart = it->restart || (icv && (icv->flags & CVAR_LATCH));
        char           value[96];

        if (i == g_pmItemIdx) {
            re.SetColor(selBar);
            re.DrawBox(boxX + 6.0f, y - 2.0f, boxW - 12.0f, lineH);
        }
        menuFont->setColor(i == g_pmItemIdx ? UYellow : UWhite);
        menuFont->Print(boxX + 16.0f, y, VitaMenu_Latin1(VT(it->label)), -1, NULL);
        Com_sprintf(value, sizeof(value), "%s%s", VitaPerfMenu_GetStateStr(it), needsRestart && !it->cmd ? " *" : "");
        menuFont->Print(boxX + boxW * 0.62f, y, VitaMenu_Latin1(value), -1, NULL);
        y += lineH;
    }
    if (cat->itemCount > VPM_VISIBLE) {
        char more[32];
        Com_sprintf(more, sizeof(more), "%d / %d", g_pmItemIdx + 1, cat->itemCount);
        menuFont->setColor(UYellow);
        menuFont->Print(boxX + boxW - 70.0f, boxY + 40.0f, more, -1, NULL);
    }

    /* Description of the selected item */
    {
        const char *desc = VD(cat->items[g_pmItemIdx].label);
        char        l1[160], l2[160];
        VitaPerfMenu_Wrap(desc, 62, l1, l2, sizeof(l1));
        menuFont->setColor(UWhite);
        menuFont->Print(boxX + 12.0f, boxY + boxH - 88.0f, VitaMenu_Latin1(l1), -1, NULL);
        menuFont->Print(boxX + 12.0f, boxY + boxH - 70.0f, VitaMenu_Latin1(l2), -1, NULL);
    }

    /* Footer: restart note and controls */
    menuFont->setColor(g_pmNeedRestart ? UYellow : UWhite);
    menuFont->Print(boxX + 12.0f, boxY + boxH - 46.0f,
        VitaMenu_Latin1(VT(g_pmNeedRestart ? "* changed: applied when the menu closes"
                                           : "* applied when the menu closes (video restart)")), -1, NULL);
    menuFont->setColor(UWhite);
    menuFont->Print(boxX + 12.0f, boxY + boxH - 24.0f,
        VitaMenu_Latin1(VT("L/R: tab   D-pad: select   X: change   O: close")), -1, NULL);

    re.SetColor(NULL);
}

/*
Select (bound to +vitaselect): held = objectives/scores as usual, double tap = perf
menu. The perf menu used to own Select outright, so holding it for the objectives
opened the menu instead.
*/
#define VITA_SELECT_DOUBLE_TAP_MS 350

static int s_vitaSelectLastDown = -100000;

static void CL_VitaSelectDown_f(void)
{
    const int now = Sys_Milliseconds();

    if (now - s_vitaSelectLastDown < VITA_SELECT_DOUBLE_TAP_MS) {
        s_vitaSelectLastDown = -100000;
        Cbuf_ExecuteText(EXEC_NOW, "-scores\n");
        VitaPerfMenu_Open(qfalse);
        return;
    }
    s_vitaSelectLastDown = now;
    Cbuf_ExecuteText(EXEC_NOW, "+scores\n");
}

static void CL_VitaSelectUp_f(void)
{
    Cbuf_ExecuteText(EXEC_NOW, "-scores\n");
}

void CL_VitaPerfMenu_Init(void)
{
    Cmd_AddCommand("perfmenu", CL_VitaPerfMenu_Toggle_f);
    Cmd_AddCommand("vitasettings", CL_VitaSettings_f);
    Cmd_AddCommand("vitadebug", CL_VitaDebug_f);
    Cmd_AddCommand("+vitaselect", CL_VitaSelectDown_f);
    Cmd_AddCommand("-vitaselect", CL_VitaSelectUp_f);
}
#endif

View3D::View3D()
{
    // set as transparent
    setBackgroundColor(UClear, true);
    // no border
    setBorderStyle(border_none);
    AllowActivate(true);

    m_printfadetime = 0.0;
    m_print_mat     = NULL;
    m_locationprint = qfalse;
}

void View3D::UpdateCenterPrint(const char *s, float alpha)
{
    m_printstring = s;

    if (s[0] == '@') {
        m_print_mat = uWinMan.RegisterShader(s + 1);
    } else {
        m_print_mat = NULL;
    }

    m_printalpha    = alpha;
    m_printfadetime = 4000.0;
    m_locationprint = qfalse;
}

void View3D::UpdateLocationPrint(int x, int y, const char *s, float alpha)
{
    m_printstring   = s;
    m_printalpha    = alpha;
    m_printfadetime = 4000.0;
    m_x_coord       = x;
    m_y_coord       = y;
    m_locationprint = qtrue;
}

void View3D::FrameInitialized(void)
{
    Connect(this, W_Activated, W_Activated);
    Connect(this, W_Deactivated, W_Deactivated);
}

void View3D::Pressed(Event *ev)
{
    IN_MouseOff();
    OnActivate(ev);
}

void View3D::OnActivate(Event *ev)
{
    UIWidget         *wid;
    UList<UIWidget *> widgets;

    UI_CloseInventory();
    Key_SetCatcher(Key_GetCatcher() & ~KEYCATCH_UI);

    for (wid = getParent()->getFirstChild(); wid; wid = getParent()->getNextChild(wid)) {
        if (wid->getAlwaysOnBottom() && wid != this) {
            widgets.AddTail(wid);
        }
    }

    widgets.IterateFromHead();
    while (widgets.IsCurrentValid()) {
        widgets.getCurrent()->BringToFrontPropogated();
        widgets.IterateNext();
    }
}

void View3D::OnDeactivate(Event *ev)
{
    Key_SetCatcher(Key_GetCatcher() | KEYCATCH_UI);
}

void View3D::DrawFPS(void)
{
    char string[128];

    setFont("verdana-14");
    // Changed in OPM
    //  1 just shows simple FPS
    //  2 displays the number of tris
    //  3 displays a black box at the bottom to correctly see the FPS counter
    if (fps->integer == 3) {
        re.SetColor(UBlack);
        re.DrawBox(
            0.0,
            m_frame.pos.y + m_frame.size.height - m_font->getHeight() * 4.0,
            m_frame.pos.x + m_frame.size.width,
            m_font->getHeight() * 4.0
        );
    }

    Com_sprintf(string, sizeof(string), "FPS %4.1f", currentfps);
    if (currentfps > 23.94) {
        if (cl_greenfps->integer) {
            m_font->setColor(UGreen);
        } else {
            m_font->setColor(UWhite);
        }
    } else if (currentfps > 18.0) {
        m_font->setColor(UYellow);
    } else {
        // low fps
        m_font->setColor(URed);
    }

    // Added in OPM (fps_location)
    //  0 = default (bottom left)
    //  1 = bottom right
    //  2 = top right under the time limit
    switch(fps_location->integer) {
    case 0:
    default:
        m_font->Print(
            m_font->getHeight(getHighResScale()) * 10.0 / getHighResScale()[0],
            (m_frame.pos.y + m_frame.size.height - m_font->getHeight(getHighResScale()) * 3.0) / getHighResScale()[1],
            string,
            -1,
            getHighResScale()
        );
        break;
    case 1:
        m_font->Print(
            (m_frame.pos.x + m_frame.size.width - m_font->getWidth(string, -1) * getHighResScale()[0] - m_font->getHeight(getHighResScale())) / getHighResScale()[0],
            (m_frame.pos.y + m_frame.size.height - m_font->getHeight(getHighResScale()) * 3.0) / getHighResScale()[1],
            string,
            -1,
            getHighResScale()
        );
        break;
    case 2:
        m_font->Print(
            (m_frame.pos.x + m_frame.size.width - m_font->getWidth(string, -1) * getHighResScale()[0] - m_font->getHeight(getHighResScale())) / getHighResScale()[0],
            (m_frame.pos.y + 40.0 * getHighResScale()[0]) / getHighResScale()[1],
            string,
            -1,
            getHighResScale()
        );
        break;
    }

    // Draw elements count
    if (cl_greenfps->integer) {
        m_font->setColor(UGreen);
    } else {
        m_font->setColor(UWhite);
    }

    if (fps->integer >= 2) {
        Com_sprintf(string, sizeof(string), "wt%5d wv%5d cl%d", cls.world_tris, cls.world_verts, cls.character_lights);

        // Added in OPM (fps_location)
        switch(fps_location->integer) {
        case 0:
        default:
            m_font->Print(
                (m_font->getHeight(getHighResScale()) * 10.0) / getHighResScale()[0],
                (m_frame.pos.y + m_frame.size.height - m_font->getHeight(getHighResScale()) * 2.0) / getHighResScale()[1],
                string,
                -1,
                getHighResScale()
            );
            break;
        case 1:
            m_font->Print(
                (m_frame.pos.x + m_frame.size.width - m_font->getWidth(string, -1) * getHighResScale()[0] - m_font->getHeight(getHighResScale())) / getHighResScale()[0],
                (m_frame.pos.y + m_frame.size.height - m_font->getHeight(getHighResScale()) * 2.0) / getHighResScale()[1],
                string,
                -1,
                getHighResScale()
            );
            break;
        case 2:
            m_font->Print(
                (m_frame.pos.x + m_frame.size.width - m_font->getWidth(string, -1) * getHighResScale()[0] - m_font->getHeight(getHighResScale())) / getHighResScale()[0],
                (m_frame.pos.y + 40 * getHighResScale()[0] + m_font->getHeight(getHighResScale())) / getHighResScale()[1],
                string,
                -1,
                getHighResScale()
            );
            break;
        }

        Com_sprintf(
            string,
            sizeof(string),
            "t%5d v%5d Mtex%5.2f",
            cls.total_tris,
            cls.total_verts,
            (float)cls.total_texels * 0.00000095367432
        );

        // Added in OPM (fps_location)
        switch(fps_location->integer) {
        case 0:
        default:
            m_font->Print(
                (m_font->getHeight(getHighResScale()) * 10.0) / getHighResScale()[0],
                (m_frame.pos.y + m_frame.size.height - m_font->getHeight(getHighResScale())) / getHighResScale()[1],
                string,
                -1,
                getHighResScale()
            );
            break;
        case 1:
            m_font->Print(
                (m_frame.pos.x + m_frame.size.width - m_font->getWidth(string, -1) * getHighResScale()[0] - m_font->getHeight(getHighResScale())) / getHighResScale()[0],
                (m_frame.pos.y + m_frame.size.height - m_font->getHeight(getHighResScale())) / getHighResScale()[1],
                string,
                -1,
                getHighResScale()
            );
            break;
        case 2:
            m_font->Print(
                (m_frame.pos.x + m_frame.size.width - m_font->getWidth(string, -1) * getHighResScale()[0] - m_font->getHeight(getHighResScale())) / getHighResScale()[0],
                (m_frame.pos.y + 40.0 * getHighResScale()[0] + m_font->getHeight(getHighResScale()) * 2.0) / getHighResScale()[1],
                string,
                -1,
                getHighResScale()
            );
            break;
        }
    }

    m_font->setColor(UBlack);
}

/*
void ProfPrint(UIFont* m_font, float minY, int line, char* label, prof_var_t* var, int level)
{

}
*/

void View3D::DrawProf(void)
{
    // FIXME: unimplemented
}

void View3D::PrintSound(int channel, const char *name, float vol, int rvol, float pitch, float base, int& line)
{
    char  buf[255];
    float x;
    float xStep;
    float height;

    height = m_font->getHeight(getHighResScale());
    xStep  = height;

    x = 0;
    Com_sprintf(buf, sizeof(buf), "%d", channel);
    m_font->Print(x, height * line + m_frame.pos.y, buf, -1, getHighResScale());

    x += xStep + xStep;
    Com_sprintf(buf, sizeof(buf), "%s", name);
    m_font->Print(x, height * line + m_frame.pos.y, buf, -1, getHighResScale());

    x += xStep * 30.0;
    Com_sprintf(buf, sizeof(buf), "vol:%.2f", vol);
    m_font->Print(x, height * line + m_frame.pos.y, buf, -1, getHighResScale());

    x += xStep * 8;
    Com_sprintf(buf, sizeof(buf), "rvol:%.2f", (float)(rvol / 128.f));
    m_font->Print(x, height * line + m_frame.pos.y, buf, -1, getHighResScale());

    x += xStep * 5;
    Com_sprintf(buf, sizeof(buf), "pit:%.2f", pitch);
    m_font->Print(x, height * line + m_frame.pos.y, buf, -1, getHighResScale());

    x += xStep * 5;
    Com_sprintf(buf, sizeof(buf), "base:%d", (int)base);
    m_font->Print(x, height * line + m_frame.pos.y, buf, -1, getHighResScale());

    line++;
}

void View3D::DrawSoundOverlay(void)
{
    setFont("verdana-14");
    m_font->setColor(UWhite);

    // FIXME: Unimplemented
    if (sound_overlay->integer) {
        Com_Printf("sound_overlay isn't supported with OpenAL/SDL right now.\n");
        Cvar_Set("sound_overlay", "0");
    }
}

void DisplayServerNetProfileInfo(UIFont *font, float y, netprofclient_t *netprofile)
{
    font->Print(104, y, va("%i", netprofile->upstream.packetsPerSec));
    font->Print(144, y, va("%i", netprofile->downstream.packetsPerSec));
    font->Print(184, y, va("%i", netprofile->upstream.packetsPerSec + netprofile->downstream.packetsPerSec));
    font->Print(234, y, va("%i", netprofile->upstream.percentFragmented));
    font->Print(264, y, va("%i", netprofile->downstream.percentFragmented));
    font->Print(
        294,
        y,
        va("%i",
           (unsigned int)((float)(netprofile->downstream.numFragmented + netprofile->upstream.numFragmented)
                          / (float)(netprofile->upstream.totalPackets + netprofile->downstream.totalPackets))),
        -1
    );
    font->Print(334, y, va("%i", netprofile->upstream.percentDropped));
    font->Print(364, y, va("%i", netprofile->downstream.percentDropped));
    font->Print(
        394,
        y,
        va("%i",
           (unsigned int)((float)(netprofile->downstream.numDropped + netprofile->upstream.numDropped)
                          / (float)(netprofile->upstream.totalPackets + netprofile->downstream.totalPackets))),
        -1
    );
    font->Print(434, y, va("%i", netprofile->upstream.percentDropped));
    font->Print(464, y, va("%i", netprofile->downstream.percentDropped));
    font->Print(
        494,
        y,
        va("%i",
           (unsigned int)((float)(netprofile->downstream.totalBytesConnectionLess
                                  + netprofile->upstream.totalBytesConnectionLess)
                          / (float)(netprofile->downstream.totalSize + netprofile->upstream.totalSize))),
        -1
    );
    font->Print(534, y, va("%i", netprofile->upstream.bytesPerSec));
    font->Print(594, y, va("%i", netprofile->downstream.bytesPerSec));
    font->Print(654, y, va("%i", netprofile->downstream.bytesPerSec + netprofile->upstream.bytesPerSec));
    font->Print(714, y, va("%i", netprofile->rate));
}

void DisplayClientNetProfile(UIFont *font, float x, float y, netprofclient_t *netprofile)
{
    float columns[5];
    float fontHeight;
    float columnHeight;

    fontHeight   = font->getHeight();
    columns[0]   = x + 120;
    columns[1]   = x + 230;
    columns[2]   = x + 330;
    columns[3]   = x + 430;
    columns[4]   = x + 530;
    columnHeight = y;

    font->Print(x, y, va("Rate: %i", netprofile->rate));

    columnHeight += fontHeight * 1.5;
    font->Print(x, columnHeight, "Data Type");
    font->Print(columns[0], columnHeight, "Packets per Sec");
    font->Print(columns[1], columnHeight, "% Fragmented");
    font->Print(columns[2], columnHeight, "% Dropped");
    font->Print(columns[3], columnHeight, "% OOB data");
    font->Print(columns[4], columnHeight, "Data per Sec");

    columnHeight += fontHeight * 0.5;
    font->Print(x, columnHeight, "----------");
    font->Print(columns[0], columnHeight, "----------");
    font->Print(columns[1], columnHeight, "----------");
    font->Print(columns[2], columnHeight, "----------");
    font->Print(columns[3], columnHeight, "----------");
    font->Print(columns[4], columnHeight, "----------");

    columnHeight += fontHeight;
    font->Print(x, columnHeight, "Data In");
    font->Print(columns[0], columnHeight, va("%i", netprofile->downstream.packetsPerSec));
    font->Print(columns[1], columnHeight, va("%i%%", netprofile->downstream.percentFragmented));
    font->Print(columns[2], columnHeight, va("%i%%", netprofile->downstream.percentDropped));
    font->Print(columns[3], columnHeight, va("%i%%", netprofile->downstream.percentConnectionLess));
    font->Print(columns[4], columnHeight, va("%i", netprofile->downstream.bytesPerSec));

    columnHeight += fontHeight;
    font->Print(x, columnHeight, "Data Out");
    font->Print(columns[0], columnHeight, va("%i", netprofile->upstream.packetsPerSec));
    font->Print(columns[1], columnHeight, va("%i%%", netprofile->upstream.percentFragmented));
    font->Print(columns[2], columnHeight, va("%i%%", netprofile->upstream.percentDropped));
    font->Print(columns[3], columnHeight, va("%i%%", netprofile->upstream.percentConnectionLess));
    font->Print(columns[4], columnHeight, va("%i", netprofile->upstream.bytesPerSec));

    columnHeight += fontHeight;

    font->Print(x, columnHeight, "Total Data");

    font->Print(
        columns[0], columnHeight, va("%i", netprofile->upstream.packetsPerSec + netprofile->downstream.packetsPerSec)
    );
    font->Print(
        columns[1],
        columnHeight,
        va("%i%%",
           (unsigned int)((float)(netprofile->downstream.numFragmented + netprofile->upstream.numFragmented)
                          / (float)(netprofile->upstream.totalPackets + netprofile->downstream.totalPackets)))
    );
    font->Print(
        columns[2],
        columnHeight,
        va("%i%%",
           (unsigned int)((float)(netprofile->downstream.numDropped + netprofile->upstream.numDropped)
                          / (double)(netprofile->upstream.totalPackets + netprofile->downstream.totalPackets)))
    );
    font->Print(
        columns[3],
        columnHeight,
        va("%i%%",
           (unsigned int)((float)(netprofile->downstream.totalBytesConnectionLess
                                  + netprofile->upstream.totalBytesConnectionLess)
                          / (float)(netprofile->downstream.totalSize + netprofile->upstream.totalSize)))
    );
    font->Print(
        columns[4], columnHeight, va("%i", netprofile->upstream.bytesPerSec + netprofile->downstream.bytesPerSec)
    );
}

void View3D::DrawNetProfile(void)
{
    float fontHeight;
    float yOffset;
    int   i;

    if (sv_netprofileoverlay->integer && sv_netprofile->integer && com_sv_running->integer) {
        float           columnHeight;
        float           valueHeight;
        float           separatorHeight;
        float           categoryHeight;
        float           currentHeight;
        netprofclient_t netproftotal;

        setFont("verdana-14");
        m_font->setColor(UWhite);

        fontHeight = m_font->getHeight();
        yOffset    = sv_netprofileoverlay->integer + 8;

        if (svs.netprofile.rate) {
            m_font->Print(8, yOffset, va("Server Net Profile          Max Rate: %i", svs.netprofile.rate), -1);
        } else {
            m_font->Print(8, yOffset, "Server Net Profile          Max Rate: none", -1);
        }

        columnHeight    = fontHeight + fontHeight + yOffset;
        valueHeight     = columnHeight + fontHeight;
        separatorHeight = fontHeight * 1.5 + columnHeight;
        categoryHeight  = fontHeight * 0.5 + columnHeight;

        m_font->Print(8, categoryHeight, "Data Source");
        m_font->Print(8, separatorHeight, "---------------");
        m_font->Print(104, columnHeight, "Packets per Sec");
        m_font->Print(104, valueHeight, "In");
        m_font->Print(144, valueHeight, "Out");
        m_font->Print(184, valueHeight, "Total");
        m_font->Print(104, separatorHeight, "---");
        m_font->Print(144, separatorHeight, "-----");
        m_font->Print(184, separatorHeight, "------");
        m_font->Print(234, columnHeight, "% Fragmented");
        m_font->Print(234, valueHeight, "In");
        m_font->Print(264, valueHeight, "Out");
        m_font->Print(294, valueHeight, "Total");
        m_font->Print(234, separatorHeight, "---");
        m_font->Print(264, separatorHeight, "-----");
        m_font->Print(294, separatorHeight, "------");
        m_font->Print(334, columnHeight, "% Dropped");
        m_font->Print(334, valueHeight, "In");
        m_font->Print(364, valueHeight, "Out");
        m_font->Print(394, valueHeight, "Total");
        m_font->Print(334, separatorHeight, "---");
        m_font->Print(364, separatorHeight, "-----");
        m_font->Print(394, separatorHeight, "------");
        m_font->Print(434, columnHeight, "% OOB Data");
        m_font->Print(434, valueHeight, "In");
        m_font->Print(464, valueHeight, "Out");
        m_font->Print(494, valueHeight, "Total");
        m_font->Print(434, separatorHeight, "---");
        m_font->Print(464, separatorHeight, "-----");
        m_font->Print(494, separatorHeight, "------");
        m_font->Print(534, columnHeight, "Data per Sec");
        m_font->Print(534, valueHeight, "In");
        m_font->Print(594, valueHeight, "Out");
        m_font->Print(654, valueHeight, "Total");
        m_font->Print(534, separatorHeight, "---");
        m_font->Print(594, separatorHeight, "-----");
        m_font->Print(654, separatorHeight, "------");
        m_font->Print(714, categoryHeight, "Rate");
        m_font->Print(714, separatorHeight, "------");

        currentHeight = fontHeight * 2.5 + columnHeight;
        SV_NET_CalcTotalNetProfile(&netproftotal, qfalse);
        m_font->Print(8, columnHeight + fontHeight * 2.5, "Total");
        DisplayServerNetProfileInfo(m_font, currentHeight, &netproftotal);

        currentHeight += fontHeight * 1.5;
        m_font->Print(8, currentHeight, "Clientless");
        DisplayServerNetProfileInfo(m_font, currentHeight, &svs.netprofile);

        currentHeight += fontHeight;

        for (i = 0; i < svs.iNumClients; i++) {
            client_t *client = &svs.clients[i];
            if (client->state != CS_ACTIVE || !client->gentity) {
                continue;
            }

            if (client->netchan.remoteAddress.type == NA_LOOPBACK) {
                m_font->Print(8.0, currentHeight, va("#%i-Loopback", i), -1, 0);

            } else {
                m_font->Print(8.0, currentHeight, va("Client #%i", i), -1, 0);
            }

            DisplayServerNetProfileInfo(m_font, currentHeight, &client->netprofile);
            currentHeight = currentHeight + fontHeight;
        }
    } else if (cl_netprofileoverlay->integer && cl_netprofile->integer && com_cl_running->integer) {
        setFont("verdana-14");
        m_font->setColor(UWhite);

        fontHeight = m_font->getHeight();
        yOffset    = cl_netprofileoverlay->integer + 16;

        m_font->Print(16, yOffset, "Client Net Profile", -1);

        NetProfileCalcStats(&cls.netprofile.downstream, 500);
        NetProfileCalcStats(&cls.netprofile.upstream, 500);

        DisplayClientNetProfile(m_font, 16, yOffset + fontHeight * 2, &cls.netprofile);
    }
}

void View3D::Draw2D(void)
{
#ifdef __vita__
    /* Toggle for testing if Draw2D's cost (CG_Draw2D from cgame + various
     * overlays) is the bottleneck. Set vita_skip_draw2d 1 in autoexec to
     * skip it entirely. Loses HUD via cgame, subtitles, fades, letterbox.
     * Engine UI widgets (compass etc) still draw via uWinMan path. */
    {
        static cvar_t *vita_skip_d2d = NULL;
        if (!vita_skip_d2d) vita_skip_d2d = Cvar_Get("vita_skip_draw2d", "0", CVAR_ARCHIVE);
        if (vita_skip_d2d->integer) return;
    }
#endif
    if (!cls.no_menus) {
        DrawFades();
    }

    DrawLetterbox();

    if ((cl_debuggraph->integer || cl_timegraph->integer) && !cls.no_menus) {
        SCR_DrawDebugGraph();
    } else if (!cls.no_menus) {
        if (cge) {
            cge->CG_Draw2D();
        }

        if (m_locationprint) {
            LocationPrint();
        } else {
            CenterPrint();
        }

        if (!cls.no_menus) {
            DrawSoundOverlay();
            DrawNetProfile();
            DrawSubtitleOverlay();
        }
    }

    if (fps->integer && !cls.no_menus) {
        DrawFPS();
        DrawProf();
    }

#if defined(__vita__) || defined(__SWITCH__)
    /* Perf menu overlay — drawn last so it sits on top of everything. */
    if (CL_VitaPerfMenu_IsActive()) {
#ifdef __vita__
        setFont("vita-14"); // has the accented letters (misc/vita/make_font.py)
#else
        setFont("verdana-14");
#endif
        CL_VitaPerfMenu_Draw(m_font, m_frame.size.width, m_frame.size.height);
    }
#endif
}

void View3D::CenterPrint(void)
{
    float       alpha;
    const char *p;
    qhandle_t   mat;
    float       x, y;
    float       w, h;

    if (!m_printfadetime) {
        return;
    }

    p = Sys_LV_CL_ConvertString(m_printstring);
    if (m_printfadetime > 3250) {
        alpha = 1.f - (m_printfadetime - 3250.f) / 750.f * m_printalpha;
    } else if (m_printfadetime >= 750) {
        alpha = 1.f;
    } else {
        alpha = m_printfadetime / 750.f * m_printalpha;
    }

    alpha = Q_clamp_float(alpha, 0, 1);

    if (!m_print_mat) {
        UIRect2D frame;
        m_font->setColor(UColor(0, 0, 0, alpha));

        frame = getClientFrame();

        m_font->PrintJustified(
            UIRect2D(frame.pos.x + 1, frame.pos.y + 1, frame.size.width, frame.size.height),
            m_iFontAlignmentHorizontal,
            m_iFontAlignmentVertical,
            p,
            getVirtualScale()
        );

        m_font->setColor(UColor(1, 1, 1, alpha));

        frame = getClientFrame();

        m_font->PrintJustified(frame, m_iFontAlignmentHorizontal, m_iFontAlignmentVertical, p, getVirtualScale());

        m_font->setColor(UBlack);
    } else if ((mat = m_print_mat->GetMaterial())) {
        vec4_t col {alpha, alpha, alpha, alpha};

        re.SetColor(col);

        w = re.GetShaderWidth(mat);
        h = re.GetShaderHeight(mat);
        x = (m_frame.pos.x + m_frame.size.width - w) * 0.5f;
        y = (m_frame.pos.y + m_frame.size.height - h) * 0.5f;

        re.DrawStretchPic(x, y, w, h, 0, 0, 1, 1, mat);
    }

    m_printfadetime -= cls.frametime;

    if (m_printfadetime < 0) {
        m_printfadetime = 0;
    }
}

void View3D::LocationPrint(void)
{
    fonthorzjustify_t horiz;
    fontvertjustify_t vert;
    int               x, y;
    const char       *p;
    float             alpha;
    UIRect2D          frame;

    if (!m_printfadetime) {
        m_locationprint = false;
        return;
    }

    horiz = FONT_JUSTHORZ_LEFT;
    vert  = FONT_JUSTVERT_TOP;

    p = Sys_LV_CL_ConvertString(m_printstring);
    if (m_printfadetime > 3250) {
        alpha = 1.f - (m_printfadetime - 3250.f) / 750.f * m_printalpha;
    } else if (m_printfadetime >= 750) {
        alpha = 1.f;
    } else {
        alpha = m_printfadetime / 750.f * m_printalpha;
    }

    alpha = Q_clamp_float(alpha, 0, 1);

    x = m_x_coord / 640.f * m_screenframe.size.width;
    y = (480 - m_font->getHeight(getHighResScale()) - m_y_coord) / 480.f * m_screenframe.size.height;

    if (m_x_coord == -1) {
        horiz = FONT_JUSTHORZ_CENTER;
        x     = 0;
    }
    if (m_y_coord == -1) {
        vert = FONT_JUSTVERT_CENTER;
        y    = 0;
    }

    m_font->setColor(UColor(0, 0, 0, alpha));
    frame = getClientFrame();

    m_font->PrintJustified(
        UIRect2D(frame.pos.x + x + 1, frame.pos.y + y + 1, frame.size.width, frame.size.height),
        horiz,
        vert,
        p,
        getVirtualScale()
    );

    m_font->setColor(UColor(1, 1, 1, alpha));
    frame = getClientFrame();

    m_font->PrintJustified(
        UIRect2D(frame.pos.x + x, frame.pos.y + y, frame.size.width, frame.size.height),
        horiz,
        vert,
        p,
        getVirtualScale()
    );

    m_font->setColor(UBlack);
    m_printfadetime -= cls.frametime;

    if (m_printfadetime < 0) {
        m_printfadetime = 0;
    }
}

void View3D::DrawLetterbox(void)
{
    float  frac;
    vec4_t col;

    col[0] = col[1] = col[2] = 0;
    col[3]                   = 1;

    frac = (float)cl.snap.ps.stats[STAT_LETTERBOX] / MAX_LETTERBOX_SIZE;
    if (frac <= 0) {
        m_letterbox_active = false;
        return;
    }

    m_letterbox_active = true;
    re.SetColor(col);

    re.DrawBox(0.0, 0.0, m_screenframe.size.width, m_screenframe.size.height * frac);
    re.DrawBox(
        0.0,
        m_screenframe.size.height - m_screenframe.size.height * frac,
        m_screenframe.size.width,
        m_screenframe.size.height
    );
}

void View3D::DrawFades(void)
{
    if (cl.snap.ps.blend[3] > 0) {
        re.SetColor(cl.snap.ps.blend);
        if (cl.snap.ps.stats[STAT_ADDFADE]) {
            re.AddBox(0.0, 0.0, m_screenframe.size.width, m_screenframe.size.height);
        } else {
            re.DrawBox(0.0, 0.0, m_screenframe.size.width, m_screenframe.size.height);
        }
    }
}

void View3D::Draw(void)
{
#ifdef __vita__
    /* Per-frame FPS instrumentation (V3-PROF). MASTER SWITCH: gated entirely
     * behind r_vita_perflog so a clean FPS-test run does ZERO timing calls and
     * ZERO logfile writes. Toggle it live in the perf menu (DEBUG -> "VITA-PERF
     * log") or `set r_vita_perflog 0/1`. When 0 none of the Sys_Milliseconds
     * probes below run -- this is the one knob to silence Vita logging. */
    extern int Sys_Milliseconds(void);
    static cvar_t *_v3_perflog = NULL;
    if (!_v3_perflog) _v3_perflog = Cvar_Get("r_vita_perflog", "0", CVAR_ARCHIVE);
    qboolean   _v3_prof = (_v3_perflog->integer != 0);
    int        _v3_t0 = 0, _v3_t1 = 0, _v3_t2 = 0, _v3_t3 = 0, _v3_t4 = 0;
    static int _v3_lastPrint = 0;
    qboolean   _v3_doPrint   = qfalse;
    if (_v3_prof) {
        _v3_t0      = Sys_Milliseconds();
        _v3_doPrint = (_v3_t0 - _v3_lastPrint) >= 1000;
    }
#endif
    if (clc.state != CA_DISCONNECTED) {
        SCR_DrawScreenField();
    }
#ifdef __vita__
    if (_v3_prof) _v3_t1 = Sys_Milliseconds();
#endif

    set2D();
#ifdef __vita__
    if (_v3_prof) _v3_t2 = Sys_Milliseconds();
#endif

    re.SavePerformanceCounters();
#ifdef __vita__
    if (_v3_prof) _v3_t3 = Sys_Milliseconds();
#endif

    Draw2D();

#ifdef __SWITCH__
    /* Draw the dev-menu overlay LAST -- after set2D() + Draw2D(), so it's in the
     * 2D state and sits on top of the HUD during 3D gameplay. Drawing it inside
     * SCR_DrawScreenField (before set2D) left it in the 3D state, hidden behind
     * the scene + HUD -> it only ever showed on flat 2D menu screens. */
    {
        extern void CL_DevMenu_Draw(void);
        CL_DevMenu_Draw();
    }
#endif

#ifdef __vita__
    if (_v3_prof) {
        _v3_t4 = Sys_Milliseconds();
        if (_v3_doPrint) {
            _v3_lastPrint = _v3_t0;
            Com_Printf("V3-PROF: scenefield=%d set2d=%d savepc=%d draw2d=%d total=%d\n",
                _v3_t1 - _v3_t0, _v3_t2 - _v3_t1, _v3_t3 - _v3_t2, _v3_t4 - _v3_t3,
                _v3_t4 - _v3_t0);
        }
    }
#endif
}

float avWidth = 0.0;

void View3D::InitSubtitle(void)
{
    float totalWidth;

    for (int i = 0; i < 4; i++) {
        subs[i]  = Cvar_Get(va("subtitle%d", i), "", 0);
        teams[i] = Cvar_Get(va("subteam%d", i), "0", 0);
        Q_strncpyz(oldStrings[i], subs[i]->string, sizeof(oldStrings[i]));
        fadeTime[i] = 4000.0;
        subLife[i]  = 4000.0;
    }

    totalWidth = 0.0;
    for (char j = 'A'; j <= 'Z'; j++) {
        totalWidth += m_font->getCharWidth(j);
    }

    avWidth = totalWidth / 26.0;
}

void View3D::DrawSubtitleLine(const char *text, int line, float minX, float a)
{
    const float *scale = getHighResScale();
    const float  y     = (m_font->getHeight(scale) * line + minX) / scale[1];

#if defined(__vita__) || defined(__SWITCH__)
    {
        // Dark band behind the line, then an outline, so the text reads on any scene.
        const float  pad  = 4.0f;
        const float  w    = m_font->getWidth(text, -1);
        const float  h    = m_font->getHeight(scale) / scale[1];
        const vec4_t band = {0.0f, 0.0f, 0.0f, 0.55f * a};
        static const float offs[4][2] = {
            {-1, 0},
            {1,  0},
            {0,  -1},
            {0,  1}
        };

        re.SetColor(band);
        re.DrawBox((20.0f - pad) * scale[0], (y - 1.0f) * scale[1], (w + pad * 2.0f) * scale[0], (h + 2.0f) * scale[1]);
        re.SetColor(NULL);

        m_font->setColor(UColor(0, 0, 0, a));
        for (int k = 0; k < 4; k++) {
            m_font->Print(20 + offs[k][0], y + offs[k][1], text, -1, scale);
        }
    }
#else
    m_font->setColor(UColor(0, 0, 0, a));
    m_font->Print(18, (m_font->getHeight(scale) * line + minX + 1.f) / scale[1], text, -1, scale);
#endif

    m_font->setColor(UColor(1, 1, 1, a));
    m_font->Print(20, y, text, -1, scale);
}

void View3D::DrawSubtitleOverlay(void)
{
    cvar_t *subAlpha;
    int     i;
    float   minX, maxX;
    int     line;

#if defined(__vita__) || defined(__SWITCH__)
    // Half-transparent text was hard to read on the small screen: full opacity, with a
    // dark band behind each line (DrawSubtitleLine).
    subAlpha = Cvar_Get("subAlpha", "1", 0);
#else
    subAlpha = Cvar_Get("subAlpha", "0.5", 0);
#endif

    setFont("facfont-20");
    m_font->setColor(URed);

    for (i = 0; i < MAX_SUBTITLES; i++) {
        if (strcmp(oldStrings[i], subs[i]->string)) {
            fadeTime[i] = 2500 * ((strlen(subs[i]->string) / 68) + 1.f) + 1500;
            subLife[i]  = fadeTime[i];
            Q_strncpyz(oldStrings[i], subs[i]->string, sizeof(oldStrings[i]));
        }

        if (fadeTime[i] > subLife[i] - 750.f) {
            alpha[i] = 1.f - (fadeTime[i] - (subLife[i] - 750.f)) / 750.f;
        } else if (fadeTime[i] < 750) {
            alpha[i] = fadeTime[i] / 750.f;
        } else {
            alpha[i] = 1.f;
        }

        fadeTime[i] -= cls.frametime;
        if (fadeTime[i] < 0) {
            // Clear the subtitle
            fadeTime[i]      = 0;
            oldStrings[i][0] = 0;

            if (subs[i]->string && subs[i]->string[0]) {
                Cvar_Set(va("subtitle%d", i), "");
            }
        }
    }

    minX = m_screenframe.size.height - m_font->getHeight(getHighResScale()) * 10;
    maxX = ((m_frame.pos.x + m_frame.size.width) - (m_frame.pos.x + m_frame.size.width) * 0.2f) / getHighResScale()[0];
    line = 0;

    for (i = 0; i < MAX_SUBTITLES; i++) {
        const char *subText;
        char        subBuf[2048];

        if (fadeTime[i] <= 0) {
            continue;
        }

#if defined(__vita__) || defined(__SWITCH__)
        // Subtitles come from the sound aliases in English; show them in the
        // Vita's text language (misc/vita/lang).
        Q_strncpyz(subBuf, Sys_LV_CL_ConvertString(subs[i]->string), sizeof(subBuf));
        subText = subBuf;
#else
        subText = subs[i]->string;
#endif

        if (m_font->getWidth(subText, sizeof(oldStrings[i])) > maxX) {
            char  buf[2048];
            char *c;
            char *end;
            char *start;
            float total;
            float width;
            int   blockcount;

            c = (char *)subText;

            total  = 0;
            end    = NULL;
            start  = buf;
            buf[0] = 0;

            while (*c) {
                blockcount = m_font->DBCSGetWordBlockCount(c, -1);
                if (!blockcount) {
                    break;
                }

                width = m_font->getWidth(c, blockcount);

                if (total + width > maxX) {
                    DrawSubtitleLine(buf, line, minX, alpha[i] * subAlpha->value);

                    line++;

                    total = 0;
                    start = buf;
                }

                end = start + blockcount + 1;
                if (end > buf + MAX_STRING_CHARS) {
                    Com_DPrintf("ERROR - word longer than possible line\n");
                    break;
                }

                memcpy(start, c, blockcount);
                start += blockcount;
                total += width;
                *start = 0;

                c += blockcount;
            }

            DrawSubtitleLine(buf, line, minX, alpha[i] * subAlpha->value);
            line++;
        } else {
            DrawSubtitleLine(subText, line, minX, alpha[i] * subAlpha->value);

            line++;
        }
    }
}

void View3D::ClearCenterPrint(void)
{
    m_printfadetime = 0.0;
}

qboolean View3D::LetterboxActive(void)
{
    return m_letterbox_active;
}

CLASS_DECLARATION(UIWidget, ConsoleView, NULL) {
    {NULL, NULL}
};

void ConsoleView::Draw(void) {}
