# OpenMoHAA Vita — Renderer Rewrite Roadmap

A multi-week project to retrofit the renderergl1 backend with the
hardware features the PS Vita actually does well (TBDR GPU, NEON SIMD,
VBOs, vertex-shader skinning). Goal: 45-55 FPS in `m1l1` dense outdoor
scenes, up from the ~12-15 FPS baseline.

This document is the living plan. Every phase is registered before
work starts, every measurement is captured before/after, every learning
is written down so the next session (mine or anyone else's) can pick
up without re-discovering.

---

## Hardware truth

PS Vita Fat (PCH-1001, ~2011):
- **CPU**: Quad-core ARM Cortex-A9 @ ~444 MHz (one core for game)
- **GPU**: PowerVR SGX543MP4+, tile-based deferred renderer (TBDR)
- **RAM**: 512 MB system + 128 MB VRAM (CDRAM)
- **NEON**: 128-bit SIMD per core
- **vitaGL**: OpenGL ES 1.x/2.x wrapper around Sony GXM (Metal-like)

### What the Vita does WELL
- Single-pass shaders with combined texture stages
- Static vertex buffers (VBOs) — upload once, draw many times
- Front-to-back draw order (TBDR culls overdraw at tile level)
- NEON-vectorised math (matrices, particle transforms)
- Hardware skinning via uniform matrix palettes

### What hurts Vita with stock Q3 renderer
- **Client arrays recopied per frame** → vitaGL's legacy_pool burns
  CPU time copying static BSP geometry every frame
- **CPU vertex skinning** → 23% of CPU in `RB_SkelMesh`
- **Multi-pass shaders** → each stage = its own draw call, breaks TBDR
- **glBegin/glEnd immediate mode** → emulated, very slow
- **Frequent state changes** → forces TBDR tile flushes

---

## Baseline (2026-05-17, post-NEON+bonePtr)

Measured on PS Vita Fat in m1l1 outdoor, near the truck/Nazi compound.

| Metric | Value | Source |
|---|---|---|
| Frame total (gameplay) | 80-130 ms | `frame:N all:X` |
| FPS effective | 8-12 | derived |
| `cl` (CL_Frame) | ~115 ms | com_speeds |
| `gm` (cgame VM) | 22-50 ms | com_speeds |
| `rf` (renderer frontend) | 8-12 ms | com_speeds |
| `bk` (backend) | 0 ms (async) | com_speeds |
| `snd` (S_Update) | ~22 ms | CL-PROF |
| `set2d` (R_IssuePendingRenderCommands) | 50-62 ms | V3-PROF |
| `vglSwapBuffers` | 0-1 ms | GFX-SWAP |
| Draw calls / frame | 4-15 | `c_drawElems` |
| GL state changes / frame | 8-28 | `c_glStateChanges` |
| GL binds / frame | 4-22 | `c_glBinds` |

---

## Phases

Each phase is **small**, **gated by a cvar where possible**, **measured
before/after**, and **documented in this file** with the actual win
(not the predicted win).

### ✅ Phase 0 — Baseline & profiling tooling

**Done.** CL-PROF, V3-PROF, WD-PROF, GFX-SWAP, GPU draw counter
instrumented. `r_speeds 1` print throttled to 1/sec on Vita. Mac
build + `sample` profiling pipeline working. Identified RB_SkelMesh
and CL_RefTIKI_GetLocalChannel as initial hotspots.

### ✅ Phase 0.5 — NEON skinning + bonePtr cache

**Done 2026-05-17.**

- `SkelWeightGetXyz`, `SkelWeightMorphGetXyz`, `SkelVertGetNormal` →
  NEON intrinsics gated on `__ARM_NEON`.
- `bonePtr[boneIndex]` lookup table precomputed once per
  `RB_SkelMesh` call. Replaces per-weight `ri.TIKI_GetLocalChannel`
  function-pointer hops.

**Measured win on Vita:** `set2d` went 60 ms → 47 ms (-22%) right
after NEON. After bonePtr cache the gain was within measurement noise
on Mac, but the function pointer call is genuinely gone from the inner
loop.

### ✅ Phase 1a — VBO upload infrastructure (DONE 2026-05-17)

Allocate + upload world BSP triangle soup to a vitaGL VBO at level
load. Draw path unchanged (still client arrays). Just proves the
infrastructure works without risking visual regression.

**File**: `code/renderergl1/tr_vita_vbo.c` (new, `#ifdef __vita__`).
**Cvar**: `seta r_vita_vbo_world 1` in autoexec. Default 0.
**Hook**: `R_VitaWorldVBO_Build()` called from `RE_LoadWorldMap`
right after `tr.world = &s_worldData`.

**m1l1 measured outcome:**
- 3562 SF_TRIANGLES world surfaces eligible (SF_FACE was already
  routed to SF_TRIANGLES via prior `vita: route BSP brush faces
  through SF_TRIANGLES` commit).
- 16577 verts × 44 B = 712 KB
- 28854 indexes × 4 B = 112 KB
- Total VRAM cost: 824 KB. Vita Fat has 128 MB VRAM, no pressure.
- World renders identically (cvar doesn't change draw path yet).
- set2d unchanged (expected — VBO not yet bound at draw time).

**Learning:** vitaGL is statically linked so plain `glGenBuffers /
glBindBuffer / glBufferData / glDeleteBuffers` resolve directly; the
engine's `qgl*` indirection is for SDL_GL_GetProcAddress paths and
doesn't apply on Vita. SF_GRID is skipped for now — it owns a
subdivided patch mesh and needs a separate path. SF_FACE wasn't seen
because Vita routes it through SF_TRIANGLES.

### ⚠️ Phase 1b — First cut SHIPPED but VISUALLY BROKEN (rolled back to cvar=0)

Wired the world VBO into the draw path:
- New file fields: `srfTriangles_t.vitaVboSurfIdx` (stamped during
  build), `tess.useVitaWorldVBO + tess.vitaWorldVboFirstIndex +
  tess.vitaWorldVboNumIndexes`.
- `RB_SurfaceTriangles` flushes the prior tess (if non-empty),
  marks tess as VBO-mode, sets `tess.numIndexes = vboNumIndexes`
  but `tess.numVertexes = 0` so the per-vert compute loops
  short-circuit.
- `R_DrawElements` branches on `tess.useVitaWorldVBO` and calls
  `R_VitaWorldVBO_BindAndDraw(firstIndex, numIndexes)`, which binds
  VBO+IBO, sets vertex/normal/texCoord/color pointers to drawVert_t
  offsets in the VBO, draws, and unbinds.

**OUTCOME (live test on Vita m1l1, 2026-05-17):**
- World renders.
- Some walls look WRONG — lightmap pass is stretched/garbled.
- No measurable FPS gain (set2d still ~50ms).

**Root cause:** the engine renders most BSP walls as two passes:
- Pass 0: diffuse texture, sampled with `drawVert.st` UVs (offset 12).
- Pass 1: lightmap, sampled with `drawVert.lightmap` UVs (offset 20).

`R_VitaWorldVBO_BindAndDraw` hard-codes texCoord pointer to offset 12
for EVERY pass. Pass 1 (lightmap) therefore samples the lightmap
texture with diffuse UVs — wrong mapping, visible as smeared / wrong
lighting on walls.

When Q3 uses the *single-pass multitexture* path (TMU 0 = diffuse,
TMU 1 = lightmap), TWO texCoord pointers are needed
simultaneously — one per TMU. BindAndDraw sets only one.

**Decided:** roll back the cvar to 0 by default. The Phase 1a
infrastructure (VBO allocation + upload + per-surface offsets) stays
committed and works fine. The draw path is gated by the cvar so
production behaviour is unchanged.

**Learning** (worth a memory entry):
- Q3 multi-stage shaders need per-stage texCoord pointers.
  Hard-coding one offset breaks lightmap pass.
- A correct Phase 1b needs either (a) per-pass texCoord offset
  injection, knowing whether the active TMU is base or lightmap, or
  (b) restricting VBO surfaces to single-stage shaders only — which
  excludes most walls and kills the win.
- For Phase 1b proper: in `BindAndDraw`, set TMU 0 = offset 12 AND
  TMU 1 = offset 20 (via `glClientActiveTextureARB`). That covers
  the dominant multitexture case. For multi-pass shaders, hook the
  per-pass setup to override the engine's pointer.

### Phase 1b proper — TODO next session

Upload static BSP world geometry to a vitaGL VBO once at level load.
Bind & draw without re-copying client arrays each frame.

- **Scope**: SF_FACE (now SF_TRIANGLES via earlier port fix) + SF_GRID
  world surfaces only. Static models, NPCs, particles untouched.
- **Cvar gate**: `r_vita_vbo_world` default 0. Live-toggleable.
- **Fallback**: existing `R_DrawElements` path preserved exactly. When
  cvar = 0, behaviour is bit-identical to today.
- **Predicted win**: 30-40% reduction in `set2d`. Frames in dense
  outdoor cenas should drop ~80 ms → ~55 ms (~12 → ~18 FPS).

**Risk**: vertex format mismatch (lightmap UVs, vertex colours).
Mitigation: cvar lets us flip back immediately. Test scene comparison
side-by-side before committing.

### Phase 2 — GPU skinning for NPCs (3-5 days, MEDIUM risk)

Move TIKI mesh skinning from CPU (`RB_SkelMesh`) to vertex shader via
matrix palette uniforms.

- Upload bone matrices (up to TIKI_MAX_BONES=100) as uniform array
  before each NPC draw.
- Vertex shader does `pos = sum(boneMatrix[boneIdx[i]] * pos * weight[i])`.
- CPU side just sets uniforms and submits indexed draw.
- **Cvar gate**: `r_vita_gpu_skinning` default 0.

**Predicted win**: RB_SkelMesh drops from ~23% of CPU to ~3%. Frame
in cenas com NPCs visible drops another ~15 ms (~25 → ~30 FPS).

**Risk**: vertex shader has to match what Q3 backend expects (colours,
UVs, lightmap). Vita Fat shader compiler is finicky.

### Phase 3 — Single-pass combined shaders (5-7 days, MEDIUM risk)

Q3 shaders are multi-pass: stage 0 = diffuse, stage 1 = lightmap,
stage 2 = environment, etc. Each stage = its own draw call. On TBDR
GPUs this is awful — every state change flushes the tile.

Combine 2-stage `diffuse * lightmap` shaders (the dominant pattern)
into a single fragment shader that does both texture fetches and the
multiply in one pass.

- Detect at shader compile time which Q3 shaders match the pattern
  (`diffuse + tcGen base, lightmap + GL_DST_COLOR`).
- For those, emit a special "combined" stage that's drawn once.
- **Cvar gate**: `r_vita_combined_stages` default 0.

**Predicted win**: 2× draw call reduction (15 → 7 typical). Saves
~10 ms set2d. (~30 → ~35 FPS)

### Phase 4 — Front-to-back draw order + overdraw cull (2-3 days, LOW risk)

Q3 sorts back-to-front for transparent shaders, ordering by sort key.
TBDR GPUs want **opaque front-to-back** to skip occluded pixels at
tile level.

- Add a second sort pass for opaque sort keys (S_OPAQUE) by depth
  near→far.
- Transparent surfaces stay back-to-front (correct alpha blending).
- **Cvar gate**: `r_vita_front_to_back` default 0.

**Predicted win**: less GPU pixel work in dense outdoor. ~5 FPS.

### Phase 5 — vitaGL-specific extensions (1-2 days, LOW risk)

Replace generic GL calls with vitaGL fast-path APIs where they exist.

- `vglDrawArraysJ` (Just-In-Time array draw, no client array copy).
- `vglBindAttribLocation` for shader attribs.
- `vglUseLowPrecision` for shader varyings (already on).
- **Cvar gate**: `r_vita_vgl_ext` default 0.

**Predicted win**: 5-10% across the board.

---

## How we'll work

1. **One phase at a time.** Don't mix Phase 1 work with Phase 2 etc.
2. **Cvar gate every new path.** Default 0 so behaviour doesn't change.
3. **Build + push + test on Vita.** Run baseline scene 30 s. Capture
   log. Compare set2d + frame times.
4. **Record outcome in this file.** Predicted vs actual, what
   went wrong, what we learned. **Even failures get a paragraph.**
5. **Commit per phase.** Atomic git commits so we can git-bisect later.
6. **Memory entries.** When a phase ships, add what we learned to the
   `feedback_*` / `project_openmohaa_*` memory files so future-Claude
   doesn't re-discover.

## Outcomes log (filled as we go)

| Phase | Predicted set2d | Actual set2d | Predicted FPS | Actual FPS | Notes |
|---|---|---|---|---|---|
| 0.5 | -22% (60→47ms) | -22% (60→47ms) | +50% | +30% | NEON + bonePtr. cgame VM didn't shrink. |
| 1a | unchanged | unchanged | unchanged | unchanged | VBO upload only — 3562 surfaces, 824 KB VRAM. Infrastructure ready. |
| 1b first cut | -35% (50→32ms) | ~no change | +50% | ~no change | **Visually broken — walls glitched.** Single texCoord offset (12) used for ALL passes; lightmap pass needed offset 20. Rolled back to cvar=0. |
| 1b proper | -35% | TBD | +50% | TBD | Per-pass texCoord injection or multitexture TMU0/TMU1 dual bind. |
| 2 | -10% | TBD | +25% | TBD | GPU skinning |
| 3 | -15% | TBD | +15% | TBD | Combined stages |
| 4 | -5% | TBD | +10% | TBD | Front-to-back |
| 5 | -5% | TBD | +5% | TBD | vitaGL ext |

---

## What we will NOT do

- Multi-threaded renderer. Q3 architecture is single-threaded and
  splitting render/gameplay threads would touch every system.
- Rewrite cgame. Game logic optimization is a different project.
- Replace renderergl1 with renderergl2. The GL2 path is dead in
  OpenMoHAA Vita port (different code path, untested).
- Cut visible features (sky, lights, decals) for FPS. We want
  the WHOLE game to run, not a stripped version.
