# noseguy-wayland — Design Specification

Date: 2026-04-12

## Overview

Wayland-native reimplementation of the classic `noseguy` XScreenSaver hack.
Runs as an animated background via wlr-layer-shell. Integrates with
swaylock-plugin as a lockscreen background. Uses the original character art
ported from `noseguy.c` X11 primitives to cairo.

## Decisions

| Question | Decision |
|----------|----------|
| Behavior fidelity | Faithful — exact state machine, timing, and art from original |
| Language | C |
| Integration | Standalone executable + swaylock-plugin shared library |
| Multi-monitor | One independent noseguy per output |
| Text fallback | Built-in quote list extracted verbatim from original noseguy.c |
| Rendering | Approach A: raw wayland-client + wlr-layer-shell + cairo + pango |

---

## File Structure

```
noseguy-wayland/
├── meson.build
├── src/
│   ├── main.c          — CLI parsing, init, per-output spawn
│   ├── wayland.c/.h    — registry, outputs, layer-surface lifecycle
│   ├── render.c/.h     — cairo frame drawing (character + bubble)
│   ├── anim.c/.h       — state machine (WALK→STOP→TALK→WAIT→WALK)
│   ├── text.c/.h       — text provider (command / file / stdin / built-in)
│   └── noseguy.h       — shared structs (App, Output, AnimState)
├── protocol/
│   └── wlr-layer-shell-unstable-v1.xml
└── data/
    └── quotes.h        — built-in fallback strings (static const char*[])
```

---

## Architecture

### Data Flow

```
main() → init Wayland globals → enumerate outputs
  └─ per output: create layer-surface → start frame callback loop
       └─ each frame: anim_tick() → render_frame() → commit surface
text_provider → feeds anim module when TALK state needs a new string
```

Each output is an independent `struct Output` with its own cairo surface,
shm buffer, and `AnimState`. They share only the global `App` context
(Wayland connection, text provider).

---

## Section 1: Wayland Layer Surface & Output Management

`wayland.c` binds these globals from the Wayland registry:

- `wl_compositor` — create surfaces
- `wl_shm` — shared memory buffers
- `zwlr_layer_shell_v1` — background layer placement
- `wl_output` — one `struct Output` created per output event

Each `struct Output` creates a `zwlr_layer_surface_v1`:

```
layer                  = ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND
anchor                 = none (floating)
size                   = full output dimensions
keyboard_interactivity = none
exclusive_zone         = 0
```

**Buffer strategy:** double `wl_shm_pool` buffers per output. While the
compositor displays buffer A, we paint into buffer B. Swapped on
`wl_buffer.release`.

**Frame pacing:** `wl_surface.frame` callback drives the loop. Compositor
paces us. Target 30 FPS (matches original); on higher-refresh displays we
skip frames via a frame counter.

**swaylock-plugin mode:** detected via `SWAYLOCK_PLUGIN` env var. `wayland.c`
is bypassed entirely; only `anim_tick()` and `render_frame()` are used.

---

## Section 2: Rendering Engine

`render.c` owns a cairo context per output. Full repaint every frame.

### Porting Original Art

Original `noseguy.c` uses X11 primitives. Direct cairo equivalents:

| X11 | cairo |
|-----|-------|
| `XFillArc` | `cairo_arc` + `cairo_fill` |
| `XDrawArc` | `cairo_arc` + `cairo_stroke` |
| `XFillPolygon` | `cairo_move_to` / `cairo_line_to` + `cairo_fill` |
| `XDrawLine` | `cairo_move_to` / `cairo_line_to` + `cairo_stroke` |
| `XSetForeground` | `cairo_set_source_rgb` |

Character is drawn at fixed logical size from original, scaled by a factor
derived from output height for resolution independence.

### Per-Frame Draw Order

1. Clear background (`cairo_paint`)
2. Draw character at `(x, y)` with current animation frame
3. If state == TALK: draw speech bubble + pango text
4. `cairo_surface_flush` → `wl_surface.attach` + `commit`

### Character Sub-Functions

```c
draw_head(cr, x, y, frame);   // head oval, hair
draw_eyes(cr, x, y, frame);   // blink state
draw_nose(cr, x, y, frame);   // the star of the show
draw_mouth(cr, x, y, frame);  // open/closed for talking
draw_body(cr, x, y, dir);     // torso, arms
draw_legs(cr, x, y, frame);   // walk cycle frames
```

### Speech Bubble

Rounded rectangle (`cairo_arc` at corners) with triangular tail pointing
down toward character's head. Text rendered with `pango_cairo_show_layout`
for correct line wrapping, font metrics, and Unicode.

---

## Section 3: Animation State Machine

`anim.c` ports the original state machine faithfully.

### States

```
WALK → STOP → TALK → WAIT → WALK (loops)
```

Blinking occurs inside WALK and WAIT states.

### State Table

| State | Behavior | Duration |
|-------|----------|----------|
| `WALK` | moves `±speed` px/tick, legs cycle, occasional blink | random 2–8 sec |
| `STOP` | legs freeze, faces screen | ~0.3 sec |
| `TALK` | mouth opens/closes each N ticks, speech bubble shown | until text timer expires |
| `WAIT` | bubble gone, looks around | ~1 sec |

### AnimState Struct

```c
typedef struct {
    double   x, y;          // current position
    int      dir;           // -1 left, +1 right
    int      frame;         // walk cycle frame 0-3
    int      blink;         // blink frame 0-2
    State    state;
    int      ticks;         // ticks remaining in current state
    int      mouth_open;    // toggles in TALK
    char    *current_text;  // owned string, NULL outside TALK
} AnimState;
```

`anim_tick(AnimState*, double dt)` called once per frame with delta time.
Random values use `rand()` seeded from time, matching original behavior.
Walk boundaries: character bounces at output edges, reverses direction.
Text fetch triggered on entry to TALK: `text_get_next()` called, result
stored in `current_text`.

---

## Section 4: Text Provider

```c
TextProvider *text_provider_create(TextConfig *cfg);
const char   *text_get_next(TextProvider *tp);   // never blocks
void          text_provider_destroy(TextProvider *tp);
```

### Source Priority (first configured wins)

1. `--stdin` — read all of stdin at startup, split on blank lines, cycle
2. `--text-file path` — same, re-read each cycle (supports live updates)
3. `--text-command "fortune -s"` — `popen()` per fetch, result cached
4. Built-in fallback — `quotes.h` array from original noseguy.c, random cycle

### Non-Blocking Command Execution

`popen()` runs in a background thread (single producer). Result written to
a mutex-protected slot. `text_get_next()` reads non-blocking — returns
previous string if command not yet finished.

---

## Section 5: CLI

```
noseguy-wayland [OPTIONS]

  --text-command <cmd>     shell command for text (default: built-in)
  --text-file <path>       read text from file
  --stdin                  read text from stdin
  --fps <n>                target frame rate (default: 30)
  --font <name>            pango font name (default: "Sans")
  --font-size <px>         font size in pixels (default: 18)
  --interval <seconds>     seconds between text fetches (default: 30)
  --bg-color <#rrggbb>     background color (default: #000000)
```

Implemented with `getopt_long`. No `--output` selector — all outputs always
receive a character instance.

---

## Section 6: Build System

### meson.build Dependencies

```
wayland-client
wayland-protocols
cairo
pangocairo
threads
```

`wayland-scanner` generates protocol glue from
`protocol/wlr-layer-shell-unstable-v1.xml` as a meson custom target.

```bash
meson setup build
ninja -C build
```

### Two Build Targets

- `noseguy-wayland` — standalone executable (always built)
- `noseguy-plugin.so` — swaylock-plugin shared library (`-Dplugin=true`)

### swaylock-plugin ABI

```c
void screensaver_init(int width, int height);
void screensaver_render(cairo_t *cr, double dt);
void screensaver_destroy(void);
```

Plugin build includes `anim.c`, `render.c`, `text.c`. Excludes `wayland.c`
and `main.c`. Text command passed via `NOSEGUY_TEXT_COMMAND` env var.

### swayidle Integration Example

```bash
swayidle -w \
  timeout 300 'swaylock-plugin --command "noseguy-wayland --text-command \"fortune -s\""' \
  timeout 600 'swaymsg "output * dpms off"' \
  resume 'swaymsg "output * dpms on"'
```

---

## Dependencies Summary

| Library | Purpose |
|---------|---------|
| wayland-client | Wayland protocol connection |
| wlr-layer-shell-unstable-v1 | Background layer surface |
| cairo | 2D rendering, character drawing |
| pangocairo | Text layout in speech bubble |
| pthreads | Non-blocking text command execution |

## Non-Goals

- X11 / XWayland support
- Input handling
- Authentication (handled by swaylock)
- Pixel-perfect reproduction of original (proportions and timing faithful, not exact pixel counts)
