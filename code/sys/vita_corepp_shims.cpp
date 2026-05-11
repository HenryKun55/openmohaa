/*
 * PS Vita C linkage shims for corepp symbols.
 *
 * cgame's cg_main.c (a C source file) calls L_InitEvents() and
 * L_ShutdownEvents(). The definitions live in code/corepp/listener.cpp
 * where they have C++ linkage (mangled to _Z12L_InitEventsv etc.). On
 * the upstream Linux/Windows SHARED build that mismatch is tolerated
 * by the dynamic linker; in our static Vita link both names need to
 * resolve concretely, so reference the mangled implementations via asm
 * labels and expose extern "C" trampolines under the un-mangled names
 * that cg_main.c's call sites expect.
 */

#ifdef __vita__

extern void cxx_L_InitEvents()     asm("_Z12L_InitEventsv");
extern void cxx_L_ShutdownEvents() asm("_Z16L_ShutdownEventsv");

extern "C" {

void L_InitEvents(void)
{
    cxx_L_InitEvents();
}

void L_ShutdownEvents(void)
{
    cxx_L_ShutdownEvents();
}

}

#endif
