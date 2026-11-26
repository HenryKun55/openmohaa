/*
 * The Vita's on-screen keyboard (SceImeDialog) for the game's text fields: server
 * address, player name, chat.
 *
 * The dialog draws itself over the game: vitaGL updates common dialogs at every
 * vglSwapBuffers(1) (sdl_glimp.c, render thread), so the game keeps rendering while
 * it is open. One dialog at a time; whoever opened it polls Sys_VitaIme_Poll each
 * frame until it returns the text or a cancel. The game's own input is dropped while
 * it is open (sdl_input.c).
 *
 * Text goes in and out as UTF-8 on the dialog side (UTF-16) and Latin-1 on the
 * game's side: the game's fonts are 8-bit, so accented letters (á, ç, ñ...) survive
 * and anything beyond Latin-1 becomes '?'.
 */

#include <string.h>
#include <psp2/ime_dialog.h>
#include <psp2/common_dialog.h>
#include <psp2/types.h>

#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"

#define VITA_IME_MAX 128

static SceWChar16 s_title[64];
static SceWChar16 s_initial[VITA_IME_MAX + 1];
static SceWChar16 s_input[VITA_IME_MAX + 1];
static qboolean   s_running;
static qboolean   s_orphan;   /* its owner went away: closed and dropped by Sys_VitaIme_Active */

static void Latin1ToUtf16(SceWChar16 *dst, int dstLen, const char *src)
{
    int i = 0;
    while (src && *src && i < dstLen - 1) {
        dst[i++] = (unsigned char)*src++;
    }
    dst[i] = 0;
}

static void Utf16ToLatin1(char *dst, int dstLen, const SceWChar16 *src)
{
    int i = 0;
    while (*src && i < dstLen - 1) {
        unsigned int c = *src++;
        if (c < 0x20) {
            continue; /* line breaks and controls: the game's fields are one line */
        }
        dst[i++] = c <= 0xFF ? (char)c : '?';
    }
    dst[i] = 0;
}

int Sys_VitaIme_Poll(char *out, int outLen);

/* Test runs (Vita3K): vita_ime_test "<text>" opens the keyboard with that text and
 * prints what comes back to the console ([VITA-IME] lines). Not saved. */
static void Sys_VitaIme_TestFrame(void)
{
    static cvar_t  *test;
    static qboolean testing;
    char            text[VITA_IME_MAX + 1];
    int             res;

    if (!test) {
        test = Cvar_Get("vita_ime_test", "", CVAR_TEMP);
    }
    if (testing) {
        res = Sys_VitaIme_Poll(text, sizeof(text));
        if (res != 0) {
            testing = qfalse;
            Com_Printf("[VITA-IME] test %s: '%s'\n", res > 0 ? "confirmed" : "cancelled", res > 0 ? text : "");
        }
        return;
    }
    if (test->string[0] && !s_running) {
        testing = Sys_VitaIme_Open("Test", test->string, VITA_IME_MAX);
        Com_Printf("[VITA-IME] test opened: %d\n", testing);
        Cvar_Set("vita_ime_test", "");
    }
}

/* Called every frame by the input code. */
qboolean Sys_VitaIme_Active(void)
{
    Sys_VitaIme_TestFrame();
    if (s_running && s_orphan) {
        char drop[4];
        Sys_VitaIme_Poll(drop, sizeof(drop));
    }
    return s_running;
}

/* The widget that opened the keyboard is gone: its text is thrown away. */
void Sys_VitaIme_Release(void)
{
    if (s_running) {
        s_orphan = qtrue;
    }
}

/*
 * Opens the keyboard with `initial` already typed. maxLen: longest text accepted.
 * qfalse when a dialog is already open or the system refused it.
 */
qboolean Sys_VitaIme_Open(const char *title, const char *initial, int maxLen)
{
    SceImeDialogParam param;
    int               res;

    if (s_running) {
        return qfalse;
    }
    if (maxLen <= 0 || maxLen > VITA_IME_MAX) {
        maxLen = VITA_IME_MAX;
    }

    Latin1ToUtf16(s_title, ARRAY_LEN(s_title), title ? title : "");
    Latin1ToUtf16(s_initial, maxLen + 1, initial ? initial : "");
    memset(s_input, 0, sizeof(s_input));

    sceImeDialogParamInit(&param);
    param.supportedLanguages = 0; /* all of the system's */
    param.languagesForced    = SCE_FALSE;
    param.type               = SCE_IME_TYPE_DEFAULT;
    param.option             = 0;
    param.textBoxMode        = SCE_IME_DIALOG_TEXTBOX_MODE_WITH_CLEAR;
    param.dialogMode         = SCE_IME_DIALOG_DIALOG_MODE_WITH_CANCEL; /* Circle backs out */
    param.title              = s_title;
    param.maxTextLength      = maxLen;
    param.initialText        = s_initial;
    param.inputTextBuffer    = s_input;

    res = sceImeDialogInit(&param);
    if (res < 0) {
        Com_Printf("[VITA-IME] sceImeDialogInit failed: 0x%08X\n", res);
        return qfalse;
    }
    s_running = qtrue;
    s_orphan  = qfalse;
    return qtrue;
}

/*
 * 0: still open. 1: confirmed, the text is in `out`. -1: cancelled (or failed).
 */
int Sys_VitaIme_Poll(char *out, int outLen)
{
    SceImeDialogResult result;
    int                status;

    if (!s_running) {
        return -1;
    }
    status = sceImeDialogGetStatus();
    if (status != SCE_COMMON_DIALOG_STATUS_FINISHED) {
        if (status == SCE_COMMON_DIALOG_STATUS_NONE) {
            /* closed underneath us (the system took it away) */
            s_running = qfalse;
            return -1;
        }
        return 0;
    }

    memset(&result, 0, sizeof(result));
    sceImeDialogGetResult(&result);
    sceImeDialogTerm();
    s_running = qfalse;

    if (result.button != SCE_IME_DIALOG_BUTTON_ENTER) {
        return -1;
    }
    Utf16ToLatin1(out, outLen, s_input);
    return 1;
}
