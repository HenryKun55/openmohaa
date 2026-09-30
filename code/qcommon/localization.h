#pragma once

#ifdef __cplusplus
extern "C" {
#endif

    void        Sys_InitLocalization();
    void        Sys_ShutLocalization();
    const char *Sys_LV_ConvertString(const char *var);
    const char *Sys_LV_CL_ConvertString(const char *var);
#if defined(__vita__) || defined(__SWITCH__)
    // The text in the language of that code (vita/lang/<code>.txt), or var itself when that
    // language or entry is missing. Latin-1, like the game's own table.
    const char *Sys_LV_ConvertStringFor(const char *code, const char *var);
#endif

#ifdef __cplusplus
}
#endif
