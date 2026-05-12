/*
 * Vita .suprx C++ allocator override.
 *
 * Each PRX module on Vita gets its own newlib heap (sized via the weak
 * symbol `_newlib_heap_size_user` in psp2_dll_hacks.c — defaults to
 * 2 MiB). libstdc++ is linked into the module statically, so its
 * `operator new` / `operator delete` call newlib's `malloc` / `free`
 * directly and live entirely within that 2 MiB heap.
 *
 * fgame's C++ code (tiki cache, animations, ScriptVariable lists, the
 * Container<T> templates) routinely consumes more than 2 MiB during a
 * single map load, throws std::bad_alloc and terminates the process.
 *
 * Reroute `operator new`/`operator delete` to the host engine's malloc
 * (300 MiB heap, see code/sys/sys_vita.c) via the `g_engsysfuncs`
 * imports table the dlopen wrapper hands to module_start.
 *
 * Bootstrap problem
 * -----------------
 * `__libc_init_array` runs C++ static initialisers before module_start
 * has had a chance to copy the imports table — and some of those
 * static initialisers themselves call `new`. To avoid a NULL function
 * pointer call, we hand out memory from a tiny internal bump-allocator
 * until `psp2_cpp_alloc_ready()` is called, then switch to the engine
 * heap for everything else. The bootstrap region is intentionally
 * leaked (it backs C++ singletons that live for the whole process).
 *
 * Why a separate `.cpp`?
 * ----------------------
 * Defining the `operator new` symbols from a C file that also defines
 * `module_start` confused the linker enough that some libsupc++
 * relocations resolved to NULL. Keeping this in its own translation
 * unit (and building it as C++ so the toolchain sees `operator new` as
 * a real C++ overload rather than a name-collision target) restores
 * the expected static-link layout.
 */

#include <new>
#include <stddef.h>
#include <stdint.h>

extern "C" {

#include "psp2_dll_imports.h"

/* Defined in psp2_dll_hacks.c, populated by module_start once dlopen
 * hands us the imports table from the host. Read-only after that. */
extern sysfuncs_t g_engsysfuncs;

/* 16 KiB bootstrap heap. C++ static initialisers in libstdc++ + corepp
 * use a few KiB each (init of std::locale facets, lazy mutexes, etc.).
 * 16 KiB has plenty of headroom for that without bloating the module. */
static char     s_bootstrap[16 * 1024] __attribute__((aligned(16)));
static size_t   s_bootstrap_used = 0;
static int      s_engine_ready   = 0;

static inline bool from_bootstrap(const void *p)
{
    return (const char *)p >= s_bootstrap &&
           (const char *)p < s_bootstrap + sizeof(s_bootstrap);
}

static void *do_alloc(size_t size)
{
    if (size == 0) size = 1;
    if (s_engine_ready && g_engsysfuncs.pfnSysMalloc) {
        return (*g_engsysfuncs.pfnSysMalloc)(size);
    }
    /* Bootstrap: bump-allocate, 16-byte aligned. Leaks the region but
     * that's fine — it backs lifetime-of-process singletons. */
    size_t aligned = (size + 15) & ~size_t(15);
    if (s_bootstrap_used + aligned > sizeof(s_bootstrap)) {
        return nullptr;
    }
    void *p = &s_bootstrap[s_bootstrap_used];
    s_bootstrap_used += aligned;
    return p;
}

static void do_free(void *p)
{
    if (!p) return;
    if (from_bootstrap(p)) return;  /* never free bootstrap blocks */
    if (g_engsysfuncs.pfnSysFree) {
        (*g_engsysfuncs.pfnSysFree)(p);
    }
}

/* Tell the allocator the imports table is wired. Called from
 * module_start right after `g_engsysfuncs = arg->imports;`. */
void psp2_cpp_alloc_ready(void)
{
    s_engine_ready = 1;
}

} /* extern "C" */

/* C++ allocator overrides. These shadow libstdc++ thanks to static
 * link order (.cpp object is pulled in before libsupc++.a). */
void *operator new(size_t size)
{
    void *p = do_alloc(size);
    if (!p) throw std::bad_alloc();
    return p;
}

void *operator new[](size_t size)
{
    void *p = do_alloc(size);
    if (!p) throw std::bad_alloc();
    return p;
}

void *operator new(size_t size, const std::nothrow_t &) noexcept
{
    return do_alloc(size);
}

void *operator new[](size_t size, const std::nothrow_t &) noexcept
{
    return do_alloc(size);
}

void operator delete(void *p) noexcept                   { do_free(p); }
void operator delete[](void *p) noexcept                 { do_free(p); }
void operator delete(void *p, size_t) noexcept           { do_free(p); }
void operator delete[](void *p, size_t) noexcept         { do_free(p); }
void operator delete(void *p, const std::nothrow_t&) noexcept   { do_free(p); }
void operator delete[](void *p, const std::nothrow_t&) noexcept { do_free(p); }
