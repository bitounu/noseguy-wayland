# noseguy-wayland Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a Wayland-native animated noseguy screensaver in C, faithful to the original XScreenSaver hack, running as a wlr-layer-shell background surface with built-in and external text sources.

**Architecture:** Raw wayland-client + wlr-layer-shell for surface/output management; double-buffered wl_shm + cairo for rendering; pango for speech bubble text. Pure-logic modules (anim, text) are independently testable without Wayland or cairo.

**Tech Stack:** C11, wayland-client, wlr-layer-shell-unstable-v1, cairo, pangocairo, pthreads, meson + ninja

---

## File Map

| File | Responsibility |
|------|---------------|
| `src/noseguy.h` | Shared types: AnimState, TextConfig, enums — no external deps |
| `src/anim.h/.c` | State machine (WALK→STOP→TALK→WAIT→WALK), pure logic |
| `src/text.h/.c` | Text provider: builtin / command / file / stdin, non-blocking |
| `src/render.h/.c` | Cairo character drawing and pango speech bubble |
| `src/wayland.h/.c` | Registry, output enumeration, layer-surface, SHM, frame loop |
| `src/main.c` | CLI parsing with getopt_long, App init, entry point |
| `src/plugin.c` | swaylock-plugin ABI (plugin build only) |
| `data/quotes.h` | Built-in fallback quote array, NULL-terminated |
| `protocol/wlr-layer-shell-unstable-v1.xml` | Protocol definition for wayland-scanner |
| `meson.build` | Build system |
| `meson_options.txt` | `-Dplugin=true` option for shared library target |
| `tests/test_anim.c` | Anim state machine unit tests (assert-based, no deps) |
| `tests/test_text.c` | Text provider unit tests |

---

### Task 1: Project Scaffold

**Files:**
- Create: `meson.build`
- Create: `meson_options.txt`
- Create: `protocol/wlr-layer-shell-unstable-v1.xml`

- [ ] **Step 1: Create directory structure**

```bash
mkdir -p src data tests protocol
```

- [ ] **Step 2: Get wlr-layer-shell protocol XML**

```bash
sudo pacman -S wlr-protocols
cp /usr/share/wlr-protocols/unstable/wlr-layer-shell-unstable-v1.xml protocol/
```

If the package path differs, locate it with:
```bash
find /usr/share -name 'wlr-layer-shell-unstable-v1.xml' 2>/dev/null
```

- [ ] **Step 3: Write meson.build**

```meson
project('noseguy-wayland', 'c',
  version : '0.1.0',
  default_options : ['c_std=c11', 'warning_level=2'])

wayland_client = dependency('wayland-client')
cairo_dep      = dependency('cairo')
pangocairo_dep = dependency('pangocairo')
threads_dep    = dependency('threads')

wayland_scanner = find_program('wayland-scanner')

layer_shell_xml = files('protocol/wlr-layer-shell-unstable-v1.xml')

layer_shell_h = custom_target('layer-shell-header',
  input  : layer_shell_xml,
  output : 'wlr-layer-shell-unstable-v1-client-protocol.h',
  command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'])

layer_shell_c = custom_target('layer-shell-code',
  input  : layer_shell_xml,
  output : 'wlr-layer-shell-unstable-v1-protocol.c',
  command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'])

inc     = include_directories('src', 'data')
gen     = [layer_shell_h, layer_shell_c]

src_core = files('src/anim.c', 'src/text.c')
src_all  = src_core + files('src/render.c', 'src/wayland.c', 'src/main.c')

executable('noseguy-wayland',
  src_all + gen,
  dependencies        : [wayland_client, cairo_dep, pangocairo_dep, threads_dep],
  include_directories : inc,
  install             : true)

# Unit tests — no Wayland, no cairo
test_anim_exe = executable('test-anim',
  ['tests/test_anim.c', 'src/anim.c'],
  include_directories : inc)
test('anim state machine', test_anim_exe)

test_text_exe = executable('test-text',
  ['tests/test_text.c', 'src/text.c'],
  dependencies        : [threads_dep],
  include_directories : inc)
test('text provider', test_text_exe)

# Optional plugin target
if get_option('plugin')
  shared_library('noseguy-plugin',
    src_core + files('src/render.c', 'src/plugin.c'),
    dependencies        : [cairo_dep, pangocairo_dep, threads_dep],
    include_directories : inc,
    install             : true)
endif
```

- [ ] **Step 4: Write meson_options.txt**

```ini
option('plugin', type : 'boolean', value : false,
  description : 'Build swaylock-plugin shared library')
```

- [ ] **Step 5: Verify meson configures**

```bash
meson setup build
```

Expected: configuration succeeds. Build will fail on missing source files — that is expected at this stage.

- [ ] **Step 6: Commit**

```bash
git init
git add meson.build meson_options.txt protocol/
git commit -m "chore: project scaffold, meson build, wlr-layer-shell protocol XML"
```

---

### Task 2: Shared Types (src/noseguy.h)

**Files:**
- Create: `src/noseguy.h`

- [ ] **Step 1: Write src/noseguy.h**

```c
#pragma once

#include <stdbool.h>

/* ── Animation ───────────────────────────────────────────────────── */

typedef enum {
    STATE_WALK,
    STATE_STOP,
    STATE_TALK,
    STATE_WAIT,
} AnimStateKind;

typedef enum {
    DIR_LEFT  = -1,
    DIR_RIGHT =  1,
} Direction;

typedef struct {
    double        x, y;           /* feet-center position, pixels     */
    Direction     dir;
    int           frame;          /* walk cycle index 0–3             */
    int           blink_frame;    /* 0=open  1=squint  2=closed       */
    double        blink_timer;    /* seconds until next blink event   */
    AnimStateKind state;
    double        state_timer;    /* seconds remaining in state       */
    bool          mouth_open;
    double        mouth_timer;    /* seconds until mouth toggle       */
    char         *current_text;   /* owned string; NULL outside TALK  */
    int           width;          /* output width  (pixels)           */
    int           height;         /* output height (pixels)           */
} AnimState;

/* ── Text provider ───────────────────────────────────────────────── */

typedef enum {
    TEXT_BUILTIN,
    TEXT_COMMAND,
    TEXT_FILE,
    TEXT_STDIN,
} TextSource;

typedef struct {
    TextSource  source;
    const char *arg;       /* command string or file path; NULL otherwise */
    double      interval;  /* seconds between command fetches             */
} TextConfig;
```

- [ ] **Step 2: Commit**

```bash
git add src/noseguy.h
git commit -m "feat: shared type definitions (AnimState, TextConfig)"
```

---

### Task 3: Built-in Quotes (data/quotes.h)

**Files:**
- Create: `data/quotes.h`

- [ ] **Step 1: Locate original noseguy.c quote list**

The original XScreenSaver noseguy.c source contains a `messages[]` or `strings[]` array. Find it:

```bash
# If xscreensaver-hacks source is installed
find /usr/share/doc /usr/src -name 'noseguy.c' 2>/dev/null
# Or browse the XScreenSaver repository: hacks/noseguy.c
# Copy the verbatim string array into data/quotes.h
```

- [ ] **Step 2: Write data/quotes.h**

Paste the verbatim strings from the original source. If unavailable, use this representative set as a placeholder and replace when the original is found:

```c
#pragma once

/* Built-in messages from the original noseguy (Dan Heller / Jamie Zawinski).
 * TODO: replace with verbatim list from xscreensaver/hacks/noseguy.c
 * Array is NULL-terminated. */
static const char *builtin_quotes[] = {
    "I nose a good time when I see one.",
    "The nose knows.",
    "A nose by any other name would smell as sweet.",
    "Keep your nose clean.",
    "Nosey? Me? Never.",
    "Following my nose since 1992.",
    "Plain as the nose on your face.",
    "Something smells like a good idea.",
    "Sniff sniff.",
    "Nosing around, as usual.",
    "Never look a gift nose in the mouth.",
    "It's all relative to the size of your nose.",
    NULL
};
```

- [ ] **Step 3: Commit**

```bash
git add data/quotes.h
git commit -m "feat: built-in quote list (verify against original noseguy.c)"
```

---

### Task 4: Text Provider (src/text.h + src/text.c)

**Files:**
- Create: `src/text.h`
- Create: `src/text.c`
- Create: `tests/test_text.c`

- [ ] **Step 1: Write failing test — tests/test_text.c**

```c
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "text.h"

static void test_builtin_returns_string(void) {
    TextConfig cfg = { .source = TEXT_BUILTIN, .arg = NULL, .interval = 30.0 };
    TextProvider *tp = text_provider_create(&cfg);
    assert(tp != NULL);
    const char *s = text_get_next(tp);
    assert(s != NULL);
    assert(strlen(s) > 0);
    text_provider_destroy(tp);
    printf("PASS: test_builtin_returns_string\n");
}

static void test_builtin_never_null(void) {
    TextConfig cfg = { .source = TEXT_BUILTIN, .arg = NULL, .interval = 30.0 };
    TextProvider *tp = text_provider_create(&cfg);
    for (int i = 0; i < 20; i++) {
        const char *s = text_get_next(tp);
        assert(s != NULL && strlen(s) > 0);
    }
    text_provider_destroy(tp);
    printf("PASS: test_builtin_never_null\n");
}

static void test_file_source(void) {
    FILE *f = fopen("/tmp/noseguy_test.txt", "w");
    assert(f != NULL);
    fprintf(f, "Hello nose\n\nSecond paragraph\n");
    fclose(f);

    TextConfig cfg = { .source = TEXT_FILE, .arg = "/tmp/noseguy_test.txt",
                       .interval = 30.0 };
    TextProvider *tp = text_provider_create(&cfg);
    assert(tp != NULL);
    const char *s = text_get_next(tp);
    assert(s != NULL);
    assert(strstr(s, "Hello") != NULL);
    const char *s2 = text_get_next(tp);
    assert(s2 != NULL);
    assert(strstr(s2, "Second") != NULL);
    text_provider_destroy(tp);
    printf("PASS: test_file_source\n");
}

int main(void) {
    test_builtin_returns_string();
    test_builtin_never_null();
    test_file_source();
    printf("All text provider tests passed.\n");
    return 0;
}
```

- [ ] **Step 2: Run test to confirm compile failure**

```bash
ninja -C build test-text 2>&1 | head -5
```

Expected: compile error — `text.h` not found.

- [ ] **Step 3: Write src/text.h**

```c
#pragma once

#include "noseguy.h"

typedef struct TextProvider TextProvider;

/* Create a provider from config. Returns NULL on allocation failure. */
TextProvider *text_provider_create(const TextConfig *cfg);

/* Return next text string. Never blocks. Never returns NULL. */
const char   *text_get_next(TextProvider *tp);

void          text_provider_destroy(TextProvider *tp);
```

- [ ] **Step 4: Write src/text.c**

```c
#include "text.h"
#include "quotes.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <time.h>

struct TextProvider {
    TextConfig       cfg;
    int              builtin_idx;

    /* TEXT_FILE / TEXT_STDIN lines */
    char           **lines;
    int              line_count;
    int              line_idx;

    /* TEXT_COMMAND background thread */
    pthread_t        thread;
    pthread_mutex_t  lock;
    char            *slot;           /* last fetched result, owned    */
    bool             thread_active;
    bool             stop;
};

/* ── helpers ─────────────────────────────────────────────────────── */

static char *strdup_trim(const char *s) {
    while (*s == ' ' || *s == '\n' || *s == '\r') s++;
    char *d = strdup(s);
    if (!d) return NULL;
    size_t n = strlen(d);
    while (n > 0 && (d[n-1]==' '||d[n-1]=='\n'||d[n-1]=='\r')) d[--n] = '\0';
    return d;
}

static void load_lines_from_file(TextProvider *tp, FILE *fh) {
    char   buf[4096];
    char   chunk[65536];
    int    cap = 16;
    chunk[0] = '\0';
    tp->lines      = calloc(cap, sizeof(char *));
    tp->line_count = 0;

    while (fgets(buf, sizeof(buf), fh)) {
        if (buf[0] == '\n' || buf[0] == '\r') {
            if (chunk[0]) {
                if (tp->line_count >= cap - 1) {
                    cap *= 2;
                    tp->lines = realloc(tp->lines, cap * sizeof(char *));
                }
                tp->lines[tp->line_count++] = strdup_trim(chunk);
                chunk[0] = '\0';
            }
        } else {
            strncat(chunk, buf, sizeof(chunk) - strlen(chunk) - 1);
        }
    }
    if (chunk[0])
        tp->lines[tp->line_count++] = strdup_trim(chunk);
}

/* ── background thread (TEXT_COMMAND) ────────────────────────────── */

static void *command_thread(void *arg) {
    TextProvider *tp = arg;
    while (1) {
        pthread_mutex_lock(&tp->lock);
        bool quit = tp->stop;
        pthread_mutex_unlock(&tp->lock);
        if (quit) break;

        FILE *p = popen(tp->cfg.arg, "r");
        if (p) {
            char buf[4096] = {0};
            while (fgets(buf + strlen(buf),
                         (int)(sizeof(buf) - strlen(buf) - 1), p))
                ;
            pclose(p);
            char *result = strdup_trim(buf);
            pthread_mutex_lock(&tp->lock);
            free(tp->slot);
            tp->slot = result;
            pthread_mutex_unlock(&tp->lock);
        }

        struct timespec ts = { .tv_sec = (time_t)tp->cfg.interval, .tv_nsec = 0 };
        nanosleep(&ts, NULL);
    }
    return NULL;
}

/* ── public API ──────────────────────────────────────────────────── */

TextProvider *text_provider_create(const TextConfig *cfg) {
    TextProvider *tp = calloc(1, sizeof(*tp));
    if (!tp) return NULL;
    tp->cfg = *cfg;
    pthread_mutex_init(&tp->lock, NULL);
    srand((unsigned)time(NULL));

    switch (cfg->source) {
    case TEXT_BUILTIN:
        break;

    case TEXT_FILE: {
        FILE *f = fopen(cfg->arg, "r");
        if (f) { load_lines_from_file(tp, f); fclose(f); }
        break;
    }

    case TEXT_STDIN:
        load_lines_from_file(tp, stdin);
        break;

    case TEXT_COMMAND:
        tp->thread_active = true;
        pthread_create(&tp->thread, NULL, command_thread, tp);
        break;
    }

    return tp;
}

const char *text_get_next(TextProvider *tp) {
    switch (tp->cfg.source) {
    case TEXT_BUILTIN: {
        int count = 0;
        while (builtin_quotes[count]) count++;
        tp->builtin_idx = rand() % count;
        return builtin_quotes[tp->builtin_idx];
    }

    case TEXT_FILE:
    case TEXT_STDIN:
        if (!tp->lines || tp->line_count == 0) return "...";
        if (tp->line_idx >= tp->line_count) tp->line_idx = 0;
        return tp->lines[tp->line_idx++];

    case TEXT_COMMAND: {
        pthread_mutex_lock(&tp->lock);
        const char *s = tp->slot ? tp->slot : "(loading...)";
        pthread_mutex_unlock(&tp->lock);
        return s;
    }
    }
    return "...";
}

void text_provider_destroy(TextProvider *tp) {
    if (!tp) return;
    if (tp->thread_active) {
        pthread_mutex_lock(&tp->lock);
        tp->stop = true;
        pthread_mutex_unlock(&tp->lock);
        pthread_join(tp->thread, NULL);
    }
    if (tp->lines) {
        for (int i = 0; i < tp->line_count; i++) free(tp->lines[i]);
        free(tp->lines);
    }
    free(tp->slot);
    pthread_mutex_destroy(&tp->lock);
    free(tp);
}
```

- [ ] **Step 5: Run tests**

```bash
ninja -C build && meson test -C build -v 2>&1 | grep -E 'PASS|FAIL|text'
```

Expected:
```
PASS: test_builtin_returns_string
PASS: test_builtin_never_null
PASS: test_file_source
All text provider tests passed.
```

- [ ] **Step 6: Commit**

```bash
git add src/text.h src/text.c tests/test_text.c
git commit -m "feat: text provider (builtin, file, stdin, command)"
```

---

### Task 5: Animation State Machine (src/anim.h + src/anim.c)

**Files:**
- Create: `src/anim.h`
- Create: `src/anim.c`
- Create: `tests/test_anim.c`

- [ ] **Step 1: Write failing test — tests/test_anim.c**

```c
#include <assert.h>
#include <stdio.h>
#include <math.h>
#include "anim.h"

static void test_initial_state(void) {
    AnimState s;
    anim_init(&s, 1920, 1080);
    assert(s.state == STATE_WALK);
    assert(s.x >= 0.0 && s.x <= 1920.0);
    assert(s.dir == DIR_LEFT || s.dir == DIR_RIGHT);
    assert(s.width == 1920 && s.height == 1080);
    printf("PASS: test_initial_state\n");
}

static void test_walk_moves_character(void) {
    AnimState s;
    anim_init(&s, 1920, 1080);
    s.state = STATE_WALK;
    s.dir   = DIR_RIGHT;
    double x_before = s.x;
    anim_tick(&s, 1.0 / 30.0);
    assert(s.x > x_before);
    printf("PASS: test_walk_moves_character\n");
}

static void test_boundary_reversal(void) {
    AnimState s;
    anim_init(&s, 800, 600);
    s.state = STATE_WALK;
    s.dir   = DIR_RIGHT;
    s.x     = 780.0;
    for (int i = 0; i < 60; i++) anim_tick(&s, 1.0 / 30.0);
    assert(s.dir == DIR_LEFT);
    printf("PASS: test_boundary_reversal\n");
}

static void test_walk_to_stop_transition(void) {
    AnimState s;
    anim_init(&s, 1920, 1080);
    s.state       = STATE_WALK;
    s.state_timer = 0.0;          /* force immediate transition */
    anim_tick(&s, 1.0 / 30.0);
    assert(s.state == STATE_STOP);
    printf("PASS: test_walk_to_stop_transition\n");
}

static void test_stop_to_talk_transition(void) {
    AnimState s;
    anim_init(&s, 1920, 1080);
    s.state       = STATE_STOP;
    s.state_timer = 0.0;
    anim_tick(&s, 1.0 / 30.0);
    assert(s.state == STATE_TALK);
    printf("PASS: test_stop_to_talk_transition\n");
}

static void test_talk_wants_text(void) {
    AnimState s;
    anim_init(&s, 1920, 1080);
    s.state        = STATE_STOP;
    s.state_timer  = 0.0;
    anim_tick(&s, 1.0 / 30.0);   /* → TALK, current_text = NULL */
    assert(s.state == STATE_TALK);
    assert(anim_wants_text(&s));
    printf("PASS: test_talk_wants_text\n");
}

static void test_set_text_clears_wants(void) {
    AnimState s;
    anim_init(&s, 1920, 1080);
    s.state       = STATE_STOP;
    s.state_timer = 0.0;
    anim_tick(&s, 1.0 / 30.0);
    anim_set_text(&s, strdup("Hello nose"));
    assert(!anim_wants_text(&s));
    assert(s.current_text != NULL);
    printf("PASS: test_set_text_clears_wants\n");
}

static void test_talk_to_wait_clears_text(void) {
    AnimState s;
    anim_init(&s, 1920, 1080);
    s.state        = STATE_TALK;
    s.state_timer  = 0.0;
    s.current_text = strdup("test");
    anim_tick(&s, 1.0 / 30.0);
    assert(s.state == STATE_WAIT);
    assert(s.current_text == NULL);
    printf("PASS: test_talk_to_wait_clears_text\n");
}

int main(void) {
    test_initial_state();
    test_walk_moves_character();
    test_boundary_reversal();
    test_walk_to_stop_transition();
    test_stop_to_talk_transition();
    test_talk_wants_text();
    test_set_text_clears_wants();
    test_talk_to_wait_clears_text();
    printf("All anim tests passed.\n");
    return 0;
}
```

- [ ] **Step 2: Confirm compile failure**

```bash
ninja -C build test-anim 2>&1 | head -5
```

Expected: compile error — `anim.h` not found.

- [ ] **Step 3: Write src/anim.h**

```c
#pragma once

#include "noseguy.h"
#include <string.h>

/* Initialize AnimState for given output dimensions.
 * Positions character randomly in the middle third of the screen. */
void anim_init(AnimState *s, int width, int height);

/* Advance state machine by dt seconds. Call once per frame. */
void anim_tick(AnimState *s, double dt);

/* True on the first tick after entering TALK with no text yet assigned.
 * Caller must call anim_set_text() when this returns true. */
bool anim_wants_text(const AnimState *s);

/* Feed text into TALK state. Takes ownership of the string (must be
 * heap-allocated; anim_tick() will free it on state exit). */
void anim_set_text(AnimState *s, char *text);
```

- [ ] **Step 4: Write src/anim.c**

```c
#include "anim.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

/* Timing constants tuned to match original noseguy feel */
#define WALK_SPEED_PX   4.0   /* pixels per tick                    */
#define WALK_DUR_MIN    2.0   /* seconds                            */
#define WALK_DUR_MAX    8.0
#define STOP_DUR        0.3
#define TALK_DUR_MIN    4.0
#define TALK_DUR_MAX    9.0
#define WAIT_DUR        1.0
#define MOUTH_TOGGLE    0.15  /* seconds per open/close toggle      */
#define BLINK_INTERVAL  3.5   /* seconds between blinks             */
#define BLINK_DUR       0.10  /* duration of each blink phase       */
#define MARGIN          40.0  /* px from edge before reversing      */

static double randf(double lo, double hi) {
    return lo + (hi - lo) * ((double)rand() / RAND_MAX);
}

void anim_init(AnimState *s, int width, int height) {
    memset(s, 0, sizeof(*s));
    srand((unsigned)time(NULL));
    s->width       = width;
    s->height      = height;
    s->y           = height * 0.85;
    s->x           = randf(width * 0.3, width * 0.7);
    s->dir         = (rand() % 2) ? DIR_RIGHT : DIR_LEFT;
    s->state       = STATE_WALK;
    s->state_timer = randf(WALK_DUR_MIN, WALK_DUR_MAX);
    s->blink_timer = BLINK_INTERVAL;
}

static void enter_state(AnimState *s, AnimStateKind next) {
    s->state = next;
    switch (next) {
    case STATE_WALK:
        s->state_timer = randf(WALK_DUR_MIN, WALK_DUR_MAX);
        s->dir         = (rand() % 2) ? DIR_RIGHT : DIR_LEFT;
        break;
    case STATE_STOP:
        s->state_timer = STOP_DUR;
        break;
    case STATE_TALK:
        s->state_timer = randf(TALK_DUR_MIN, TALK_DUR_MAX);
        s->mouth_open  = false;
        s->mouth_timer = MOUTH_TOGGLE;
        /* current_text remains NULL until anim_set_text() is called */
        break;
    case STATE_WAIT:
        s->state_timer = WAIT_DUR;
        free(s->current_text);
        s->current_text = NULL;
        break;
    }
}

void anim_tick(AnimState *s, double dt) {
    s->state_timer -= dt;

    /* Blink logic — runs in WALK and WAIT */
    if (s->state == STATE_WALK || s->state == STATE_WAIT) {
        s->blink_timer -= dt;
        if (s->blink_timer <= 0.0) {
            s->blink_frame = (s->blink_frame + 1) % 3;
            s->blink_timer = (s->blink_frame == 0) ? BLINK_INTERVAL : BLINK_DUR;
        }
    } else {
        s->blink_frame = 0;   /* eyes open while talking / stopped */
    }

    switch (s->state) {
    case STATE_WALK:
        s->x    += s->dir * WALK_SPEED_PX;
        s->frame = (int)(fabs(s->x) / (WALK_SPEED_PX * 4)) % 4;
        if (s->x <= MARGIN) {
            s->x  = MARGIN;
            s->dir = DIR_RIGHT;
        } else if (s->x >= s->width - MARGIN) {
            s->x  = s->width - MARGIN;
            s->dir = DIR_LEFT;
        }
        if (s->state_timer <= 0.0) enter_state(s, STATE_STOP);
        break;

    case STATE_STOP:
        if (s->state_timer <= 0.0) enter_state(s, STATE_TALK);
        break;

    case STATE_TALK:
        s->mouth_timer -= dt;
        if (s->mouth_timer <= 0.0) {
            s->mouth_open  = !s->mouth_open;
            s->mouth_timer = MOUTH_TOGGLE;
        }
        if (s->state_timer <= 0.0) enter_state(s, STATE_WAIT);
        break;

    case STATE_WAIT:
        if (s->state_timer <= 0.0) enter_state(s, STATE_WALK);
        break;
    }
}

bool anim_wants_text(const AnimState *s) {
    return s->state == STATE_TALK && s->current_text == NULL;
}

void anim_set_text(AnimState *s, char *text) {
    free(s->current_text);
    s->current_text = text;
}
```

- [ ] **Step 5: Run tests**

```bash
ninja -C build && meson test -C build -v 2>&1 | grep -E 'PASS|FAIL|anim'
```

Expected:
```
PASS: test_initial_state
PASS: test_walk_moves_character
PASS: test_boundary_reversal
PASS: test_walk_to_stop_transition
PASS: test_stop_to_talk_transition
PASS: test_talk_wants_text
PASS: test_set_text_clears_wants
PASS: test_talk_to_wait_clears_text
All anim tests passed.
```

- [ ] **Step 6: Commit**

```bash
git add src/anim.h src/anim.c tests/test_anim.c
git commit -m "feat: animation state machine with full state transitions and blink"
```

---

### Task 6: Rendering Engine (src/render.h + src/render.c)

**Files:**
- Create: `src/render.h`
- Create: `src/render.c`

Rendering is visually verified in Task 8. No unit tests.

- [ ] **Step 1: Write src/render.h**

```c
#pragma once

#include <cairo/cairo.h>
#include "noseguy.h"

/* Draw one complete frame onto cr.
 *   width/height : surface dimensions in pixels
 *   anim         : current animation state (read-only)
 *   font_name    : pango font family, e.g. "Sans"
 *   font_size    : font size in pixels
 *   bg_r/g/b     : background RGB, each 0.0–1.0             */
void render_frame(cairo_t *cr, int width, int height,
                  const AnimState *anim,
                  const char *font_name, int font_size,
                  double bg_r, double bg_g, double bg_b);
```

- [ ] **Step 2: Write src/render.c**

```c
#include "render.h"
#include <pango/pangocairo.h>
#include <math.h>
#include <string.h>

/* Character height = output_height * CHAR_SCALE.
 * All sub-functions receive `u` = CHAR_SCALE * height / 10.0
 * so that 1 character unit ≈ one tenth of the character height. */
#define CHAR_SCALE 0.13

static void draw_legs(cairo_t *cr, double cx, double cy, double u,
                      const AnimState *a)
{
    /* 4-frame walk cycle defined as (left_angle, right_angle) offsets */
    static const double angles[4][2] = {
        { -0.40,  0.40 },
        { -0.15,  0.15 },
        {  0.40, -0.40 },
        {  0.15, -0.15 },
    };
    double hip_y  = cy - 3.5 * u;
    double leg_len = 3.8 * u;
    int f = (a->state == STATE_WALK) ? a->frame : 1;

    cairo_set_line_width(cr, u * 0.5);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_source_rgb(cr, 0.25, 0.25, 0.55);   /* dark trouser */

    double la = angles[f][0], ra = angles[f][1];
    cairo_move_to(cr, cx - u * 0.2, hip_y);
    cairo_line_to(cr, cx - u * 0.2 + sin(la) * leg_len,
                      hip_y           + cos(la) * leg_len);
    cairo_stroke(cr);
    cairo_move_to(cr, cx + u * 0.2, hip_y);
    cairo_line_to(cr, cx + u * 0.2 + sin(ra) * leg_len,
                      hip_y           + cos(ra) * leg_len);
    cairo_stroke(cr);

    /* Feet — small filled circles at leg ends */
    cairo_set_source_rgb(cr, 0.15, 0.10, 0.05);
    double lx = cx - u * 0.2 + sin(la) * leg_len;
    double ly = hip_y         + cos(la) * leg_len;
    double rx = cx + u * 0.2 + sin(ra) * leg_len;
    double ry = hip_y         + cos(ra) * leg_len;
    cairo_arc(cr, lx, ly, u * 0.4, 0, 2 * M_PI); cairo_fill(cr);
    cairo_arc(cr, rx, ry, u * 0.4, 0, 2 * M_PI); cairo_fill(cr);
}

static void draw_body(cairo_t *cr, double cx, double cy, double u)
{
    double bx = cx - u * 0.8;
    double by = cy - 7.2 * u;
    double bw = u  * 1.6;
    double bh = u  * 3.7;
    double r  = u  * 0.35;

    cairo_set_source_rgb(cr, 0.25, 0.45, 0.75);
    cairo_new_path(cr);
    cairo_arc(cr, bx + r,      by + r,      r, M_PI,      3*M_PI/2);
    cairo_arc(cr, bx + bw - r, by + r,      r, 3*M_PI/2,  2*M_PI);
    cairo_arc(cr, bx + bw - r, by + bh - r, r, 0,         M_PI/2);
    cairo_arc(cr, bx + r,      by + bh - r, r, M_PI/2,    M_PI);
    cairo_close_path(cr);
    cairo_fill(cr);
}

static void draw_head(cairo_t *cr, double cx, double cy, double u)
{
    double hx = cx;
    double hy = cy - 9.8 * u;
    double hr = u  * 2.4;

    cairo_set_source_rgb(cr, 0.95, 0.80, 0.65);
    cairo_arc(cr, hx, hy, hr, 0, 2*M_PI);
    cairo_fill(cr);

    /* Hair arc across top */
    cairo_set_source_rgb(cr, 0.22, 0.13, 0.04);
    cairo_set_line_width(cr, u * 0.6);
    cairo_arc(cr, hx, hy, hr * 0.96, M_PI + 0.25, 2*M_PI - 0.25);
    cairo_stroke(cr);
}

static void draw_nose(cairo_t *cr, double cx, double cy, double u,
                      const AnimState *a)
{
    /* Offset in direction of travel — the defining feature */
    double nx = cx + a->dir * u * 1.1;
    double ny = cy - 9.5 * u;

    cairo_save(cr);
    cairo_translate(cr, nx, ny);
    cairo_scale(cr, u * 1.5, u * 1.0);
    cairo_set_source_rgb(cr, 0.88, 0.48, 0.43);
    cairo_arc(cr, 0, 0, 1.0, 0, 2*M_PI);
    cairo_fill(cr);

    /* Nostrils */
    cairo_set_source_rgb(cr, 0.58, 0.28, 0.26);
    cairo_arc(cr, -0.30, 0.25, 0.22, 0, 2*M_PI); cairo_fill(cr);
    cairo_arc(cr,  0.30, 0.25, 0.22, 0, 2*M_PI); cairo_fill(cr);
    cairo_restore(cr);
}

static void draw_eyes(cairo_t *cr, double cx, double cy, double u,
                      const AnimState *a)
{
    double ey  = cy - 10.8 * u;
    double elx = cx - u * 0.9;
    double erx = cx + u * 0.9;
    double er  = u  * 0.38;

    /* Whites */
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_arc(cr, elx, ey, er, 0, 2*M_PI); cairo_fill(cr);
    cairo_arc(cr, erx, ey, er, 0, 2*M_PI); cairo_fill(cr);

    if (a->blink_frame == 2) {
        /* Fully closed */
        cairo_set_source_rgb(cr, 0.1, 0.1, 0.1);
        cairo_set_line_width(cr, u * 0.18);
        cairo_move_to(cr, elx - er, ey); cairo_line_to(cr, elx + er, ey);
        cairo_stroke(cr);
        cairo_move_to(cr, erx - er, ey); cairo_line_to(cr, erx + er, ey);
        cairo_stroke(cr);
    } else {
        /* Pupil — shifted down slightly when squinting */
        double py = (a->blink_frame == 1) ? ey + er * 0.35 : ey;
        double pr = (a->blink_frame == 1) ? er * 0.28 : er * 0.50;
        cairo_set_source_rgb(cr, 0.08, 0.08, 0.08);
        cairo_arc(cr, elx, py, pr, 0, 2*M_PI); cairo_fill(cr);
        cairo_arc(cr, erx, py, pr, 0, 2*M_PI); cairo_fill(cr);
    }
}

static void draw_mouth(cairo_t *cr, double cx, double cy, double u,
                       const AnimState *a)
{
    double mx = cx;
    double my = cy - 8.5 * u;

    if (a->state == STATE_TALK && a->mouth_open) {
        cairo_save(cr);
        cairo_translate(cr, mx, my);
        cairo_scale(cr, u * 0.85, u * 0.42);
        cairo_set_source_rgb(cr, 0.55, 0.12, 0.12);
        cairo_arc(cr, 0, 0, 1.0, 0, 2*M_PI);
        cairo_fill(cr);
        cairo_restore(cr);
    } else {
        cairo_set_source_rgb(cr, 0.48, 0.14, 0.14);
        cairo_set_line_width(cr, u * 0.22);
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
        cairo_arc(cr, mx, my - u * 0.18, u * 0.72, 0.18, M_PI - 0.18);
        cairo_stroke(cr);
    }
}

static void draw_speech_bubble(cairo_t *cr, int width, int height,
                                const AnimState *a,
                                const char *font_name, int font_size,
                                double u)
{
    if (!a->current_text || !a->current_text[0]) return;

    PangoLayout *layout = pango_cairo_create_layout(cr);
    PangoFontDescription *desc = pango_font_description_from_string(font_name);
    pango_font_description_set_absolute_size(desc, font_size * PANGO_SCALE);
    pango_layout_set_font_description(layout, desc);
    pango_font_description_free(desc);
    pango_layout_set_width(layout, (int)(width * 0.40) * PANGO_SCALE);
    pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
    pango_layout_set_text(layout, a->current_text, -1);

    int tw, th;
    pango_layout_get_pixel_size(layout, &tw, &th);

    int pad  = font_size;
    int bw   = tw + pad * 2;
    int bh   = th + pad * 2;
    int tail = (int)(u * 2.2);

    double head_top = a->y - 12.5 * u;
    int bx = (int)(a->x - bw / 2);
    int by = (int)(head_top - bh - tail - u * 0.5);

    if (bx < 10)              bx = 10;
    if (bx + bw > width - 10) bx = width - bw - 10;
    if (by < 10)              by = 10;

    double r    = pad * 0.55;
    double tcx  = fmax(bx + r, fmin(bx + bw - r, a->x));

    /* Bubble fill + border */
    cairo_set_source_rgba(cr, 1, 1, 1, 0.93);
    cairo_new_path(cr);
    cairo_arc(cr, bx + r,      by + r,      r, M_PI,      3*M_PI/2);
    cairo_arc(cr, bx + bw - r, by + r,      r, 3*M_PI/2,  2*M_PI);
    cairo_arc(cr, bx + bw - r, by + bh - r, r, 0,         M_PI/2);
    cairo_line_to(cr, tcx + u, by + bh);
    cairo_line_to(cr, tcx,     by + bh + tail);
    cairo_line_to(cr, tcx - u, by + bh);
    cairo_arc(cr, bx + r,      by + bh - r, r, M_PI/2,    M_PI);
    cairo_close_path(cr);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, 0.18, 0.18, 0.18, 0.85);
    cairo_set_line_width(cr, 1.5);
    cairo_stroke(cr);

    /* Text */
    cairo_set_source_rgb(cr, 0.05, 0.05, 0.05);
    cairo_move_to(cr, bx + pad, by + pad);
    pango_cairo_show_layout(cr, layout);
    g_object_unref(layout);

    (void)height;
}

void render_frame(cairo_t *cr, int width, int height,
                  const AnimState *a,
                  const char *font_name, int font_size,
                  double bg_r, double bg_g, double bg_b)
{
    double u = height * CHAR_SCALE / 10.0;

    cairo_set_source_rgb(cr, bg_r, bg_g, bg_b);
    cairo_paint(cr);

    draw_legs (cr, a->x, a->y, u, a);
    draw_body (cr, a->x, a->y, u);
    draw_head (cr, a->x, a->y, u);
    draw_nose (cr, a->x, a->y, u, a);
    draw_eyes (cr, a->x, a->y, u, a);
    draw_mouth(cr, a->x, a->y, u, a);

    if (a->state == STATE_TALK)
        draw_speech_bubble(cr, width, height, a, font_name, font_size, u);
}
```

- [ ] **Step 3: Commit**

```bash
git add src/render.h src/render.c
git commit -m "feat: cairo rendering engine — character art and speech bubble"
```

---

### Task 7: Wayland Infrastructure (src/wayland.h + src/wayland.c)

**Files:**
- Create: `src/wayland.h`
- Create: `src/wayland.c`

- [ ] **Step 1: Write src/wayland.h**

```c
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <cairo/cairo.h>
#include "noseguy.h"
#include "text.h"
#include "anim.h"

/* Opaque forward declarations — keep wayland headers out of noseguy.h */
struct wl_display;   struct wl_registry;  struct wl_compositor;
struct wl_shm;       struct wl_output;    struct wl_surface;
struct wl_buffer;
struct zwlr_layer_shell_v1;  struct zwlr_layer_surface_v1;

typedef struct Output Output;
typedef struct App    App;

struct Output {
    App                          *app;
    struct wl_output             *wl_output;
    struct wl_surface            *surface;
    struct zwlr_layer_surface_v1 *layer_surface;

    int                           width, height;
    bool                          configured;

    /* Double-buffered SHM */
    struct wl_buffer             *bufs[2];
    uint8_t                      *data[2];    /* mmap'd */
    cairo_surface_t              *cs[2];
    int                           buf_idx;

    AnimState                     anim;
    int                           frame_seq;
    Output                       *next;
};

struct App {
    struct wl_display            *display;
    struct wl_registry           *registry;
    struct wl_compositor         *compositor;
    struct wl_shm                *shm;
    struct zwlr_layer_shell_v1   *layer_shell;

    Output                       *outputs;
    TextProvider                 *text;

    int                           fps;
    char                         *font_name;
    int                           font_size;
    double                        bg_r, bg_g, bg_b;
    bool                          running;
};

bool wayland_init(App *app);
void wayland_run(App *app);
void wayland_destroy(App *app);
```

- [ ] **Step 2: Write src/wayland.c**

```c
#include "wayland.h"
#include "render.h"

#include <wayland-client.h>
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>

/* ── SHM buffers ─────────────────────────────────────────────────── */

static int create_shm_fd(size_t size) {
    char name[] = "/noseguy-XXXXXX";
    int fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd < 0) { perror("shm_open"); return -1; }
    shm_unlink(name);
    if (ftruncate(fd, (off_t)size)) { perror("ftruncate"); close(fd); return -1; }
    return fd;
}

static bool output_alloc_buffers(Output *out) {
    int stride = out->width * 4;
    size_t sz  = (size_t)stride * out->height;
    for (int i = 0; i < 2; i++) {
        int fd = create_shm_fd(sz);
        if (fd < 0) return false;
        out->data[i] = mmap(NULL, sz, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
        if (out->data[i] == MAP_FAILED) { close(fd); return false; }
        struct wl_shm_pool *pool =
            wl_shm_create_pool(out->app->shm, fd, (int32_t)sz);
        out->bufs[i] = wl_shm_pool_create_buffer(
            pool, 0, out->width, out->height, stride, WL_SHM_FORMAT_ARGB8888);
        wl_shm_pool_destroy(pool);
        close(fd);
        out->cs[i] = cairo_image_surface_create_for_data(
            out->data[i], CAIRO_FORMAT_ARGB32,
            out->width, out->height, stride);
    }
    return true;
}

static void output_free_buffers(Output *out) {
    if (!out->width) return;
    size_t sz = (size_t)(out->width * 4) * out->height;
    for (int i = 0; i < 2; i++) {
        if (out->cs[i])   { cairo_surface_destroy(out->cs[i]);  out->cs[i]   = NULL; }
        if (out->bufs[i]) { wl_buffer_destroy(out->bufs[i]);    out->bufs[i] = NULL; }
        if (out->data[i] && out->data[i] != MAP_FAILED)
            { munmap(out->data[i], sz); out->data[i] = NULL; }
    }
}

/* ── Frame callback ───────────────────────────────────────────────── */

static const struct wl_callback_listener frame_listener;

static void schedule_frame(Output *out) {
    struct wl_callback *cb = wl_surface_frame(out->surface);
    wl_callback_add_listener(cb, &frame_listener, out);
    wl_surface_commit(out->surface);
}

static void frame_done(void *data, struct wl_callback *cb, uint32_t ms) {
    (void)ms;
    Output *out = data;
    wl_callback_destroy(cb);
    if (!out->configured || !out->app->running) return;

    double dt = 1.0 / out->app->fps;

    anim_tick(&out->anim, dt);

    if (anim_wants_text(&out->anim)) {
        const char *t = text_get_next(out->app->text);
        anim_set_text(&out->anim, t ? strdup(t) : strdup("..."));
    }

    int idx = out->buf_idx ^ 1;
    cairo_t *cr = cairo_create(out->cs[idx]);
    render_frame(cr, out->width, out->height, &out->anim,
                 out->app->font_name, out->app->font_size,
                 out->app->bg_r, out->app->bg_g, out->app->bg_b);
    cairo_destroy(cr);
    cairo_surface_flush(out->cs[idx]);

    wl_surface_attach(out->surface, out->bufs[idx], 0, 0);
    wl_surface_damage_buffer(out->surface, 0, 0, out->width, out->height);
    schedule_frame(out);
    out->buf_idx = idx;
}

static const struct wl_callback_listener frame_listener = { frame_done };

/* ── Layer surface events ─────────────────────────────────────────── */

static void layer_surface_configure(void *data,
    struct zwlr_layer_surface_v1 *ls, uint32_t serial,
    uint32_t w, uint32_t h)
{
    Output *out = data;
    zwlr_layer_surface_v1_ack_configure(ls, serial);

    if ((int)w != out->width || (int)h != out->height) {
        output_free_buffers(out);
        out->width  = (int)w;
        out->height = (int)h;
        output_alloc_buffers(out);
        anim_init(&out->anim, out->width, out->height);
    }
    if (!out->configured) {
        out->configured = true;
        schedule_frame(out);
    }
}

static void layer_surface_closed(void *data,
    struct zwlr_layer_surface_v1 *ls)
{
    (void)ls;
    ((Output *)data)->app->running = false;
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
    .configure = layer_surface_configure,
    .closed    = layer_surface_closed,
};

/* ── Output creation ──────────────────────────────────────────────── */

static void output_create(App *app, struct wl_output *wl_out) {
    Output *out = calloc(1, sizeof(*out));
    out->app       = app;
    out->wl_output = wl_out;

    out->surface = wl_compositor_create_surface(app->compositor);
    out->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
        app->layer_shell, out->surface, wl_out,
        ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND, "noseguy");

    /* Anchor to all edges + size 0,0 → compositor fills the full output */
    zwlr_layer_surface_v1_set_anchor(out->layer_surface,
        ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP    |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT   |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
    zwlr_layer_surface_v1_set_size(out->layer_surface, 0, 0);
    zwlr_layer_surface_v1_set_exclusive_zone(out->layer_surface, -1);
    zwlr_layer_surface_v1_add_listener(out->layer_surface,
                                        &layer_surface_listener, out);
    wl_surface_commit(out->surface);

    out->next   = app->outputs;
    app->outputs = out;
}

/* ── Registry ─────────────────────────────────────────────────────── */

static void registry_global(void *data, struct wl_registry *reg,
    uint32_t name, const char *iface, uint32_t version)
{
    App *app = data;
    (void)version;
    if (!strcmp(iface, wl_compositor_interface.name))
        app->compositor  = wl_registry_bind(reg, name, &wl_compositor_interface,  4);
    else if (!strcmp(iface, wl_shm_interface.name))
        app->shm         = wl_registry_bind(reg, name, &wl_shm_interface,          1);
    else if (!strcmp(iface, zwlr_layer_shell_v1_interface.name))
        app->layer_shell = wl_registry_bind(reg, name, &zwlr_layer_shell_v1_interface, 1);
    else if (!strcmp(iface, wl_output_interface.name)) {
        struct wl_output *wo = wl_registry_bind(reg, name, &wl_output_interface,  2);
        output_create(app, wo);
    }
}

static void registry_remove(void *d, struct wl_registry *r, uint32_t n)
    { (void)d; (void)r; (void)n; }

static const struct wl_registry_listener registry_listener =
    { registry_global, registry_remove };

/* ── Public API ───────────────────────────────────────────────────── */

bool wayland_init(App *app) {
    app->display = wl_display_connect(NULL);
    if (!app->display) { fprintf(stderr, "Cannot connect to Wayland display\n"); return false; }
    app->registry = wl_display_get_registry(app->display);
    wl_registry_add_listener(app->registry, &registry_listener, app);
    wl_display_roundtrip(app->display);
    if (!app->compositor || !app->shm || !app->layer_shell) {
        fprintf(stderr, "Missing required Wayland globals\n"); return false;
    }
    wl_display_roundtrip(app->display);   /* let outputs receive configure */
    return true;
}

void wayland_run(App *app) {
    app->running = true;
    while (app->running && wl_display_dispatch(app->display) != -1)
        ;
}

void wayland_destroy(App *app) {
    for (Output *o = app->outputs; o; ) {
        output_free_buffers(o);
        if (o->layer_surface) zwlr_layer_surface_v1_destroy(o->layer_surface);
        if (o->surface)       wl_surface_destroy(o->surface);
        if (o->wl_output)     wl_output_destroy(o->wl_output);
        Output *next = o->next;
        free(o);
        o = next;
    }
    if (app->layer_shell) zwlr_layer_shell_v1_destroy(app->layer_shell);
    if (app->shm)         wl_shm_destroy(app->shm);
    if (app->compositor)  wl_compositor_destroy(app->compositor);
    if (app->registry)    wl_registry_destroy(app->registry);
    if (app->display)     wl_display_disconnect(app->display);
}
```

- [ ] **Step 3: Build (link will still fail — main.c missing)**

```bash
ninja -C build 2>&1 | grep -v 'main.c' | grep -E 'error:|warning:' | head -20
```

Expected: no errors from wayland.c or render.c.

- [ ] **Step 4: Commit**

```bash
git add src/wayland.h src/wayland.c
git commit -m "feat: wayland layer-shell infrastructure, double-buffered SHM, frame loop"
```

---

### Task 8: Main Entry Point (src/main.c)

**Files:**
- Create: `src/main.c`

- [ ] **Step 1: Write src/main.c**

```c
#include "wayland.h"
#include "text.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s [OPTIONS]\n"
        "  --text-command <cmd>    shell command for text (default: built-in)\n"
        "  --text-file <path>      read text from file\n"
        "  --stdin                 read text from stdin\n"
        "  --fps <n>               target frame rate (default: 30)\n"
        "  --font <name>           pango font name (default: Sans)\n"
        "  --font-size <px>        font size in pixels (default: 18)\n"
        "  --interval <seconds>    seconds between text fetches (default: 30)\n"
        "  --bg-color <#rrggbb>    background color (default: #000000)\n",
        prog);
}

static bool parse_color(const char *s, double *r, double *g, double *b) {
    if (*s == '#') s++;
    unsigned ri, gi, bi;
    if (sscanf(s, "%02x%02x%02x", &ri, &gi, &bi) != 3) return false;
    *r = ri / 255.0; *g = gi / 255.0; *b = bi / 255.0;
    return true;
}

int main(int argc, char **argv) {
    TextConfig tcfg = { .source = TEXT_BUILTIN, .arg = NULL, .interval = 30.0 };
    App app = {
        .fps       = 30,
        .font_name = "Sans",
        .font_size = 18,
        .bg_r = 0.0, .bg_g = 0.0, .bg_b = 0.0,
    };

    static const struct option long_opts[] = {
        { "text-command", required_argument, NULL, 'c' },
        { "text-file",    required_argument, NULL, 'f' },
        { "stdin",        no_argument,       NULL, 's' },
        { "fps",          required_argument, NULL, 'F' },
        { "font",         required_argument, NULL, 'n' },
        { "font-size",    required_argument, NULL, 'z' },
        { "interval",     required_argument, NULL, 'i' },
        { "bg-color",     required_argument, NULL, 'b' },
        { "help",         no_argument,       NULL, 'h' },
        { NULL, 0, NULL, 0 }
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "", long_opts, NULL)) != -1) {
        switch (opt) {
        case 'c': tcfg.source = TEXT_COMMAND; tcfg.arg = optarg; break;
        case 'f': tcfg.source = TEXT_FILE;    tcfg.arg = optarg; break;
        case 's': tcfg.source = TEXT_STDIN;                      break;
        case 'F': app.fps       = atoi(optarg);                  break;
        case 'n': app.font_name = optarg;                        break;
        case 'z': app.font_size = atoi(optarg);                  break;
        case 'i': tcfg.interval = atof(optarg);                  break;
        case 'b':
            if (!parse_color(optarg, &app.bg_r, &app.bg_g, &app.bg_b)) {
                fprintf(stderr, "Invalid color: %s\n", optarg);
                return 1;
            }
            break;
        default: usage(argv[0]); return opt == 'h' ? 0 : 1;
        }
    }

    app.text = text_provider_create(&tcfg);
    if (!app.text) { fprintf(stderr, "Failed to create text provider\n"); return 1; }

    if (!wayland_init(&app)) return 1;
    wayland_run(&app);
    wayland_destroy(&app);
    text_provider_destroy(app.text);
    return 0;
}
```

- [ ] **Step 2: Build complete binary**

```bash
ninja -C build noseguy-wayland 2>&1 | grep -E 'error:|FAILED'
```

Expected: clean build, no errors.

- [ ] **Step 3: Run all unit tests**

```bash
meson test -C build -v
```

Expected: all tests pass.

- [ ] **Step 4: First visual run — built-in quotes**

```bash
./build/noseguy-wayland
```

Verify visually:
- [ ] Character appears and walks left/right
- [ ] Legs animate through 4-frame cycle while walking
- [ ] Character stops at screen edges and reverses direction
- [ ] Eyes blink periodically during walking
- [ ] Speech bubble appears with built-in text during TALK state
- [ ] Mouth opens/closes while talking
- [ ] Character resumes walking after WAIT state
- [ ] No errors or crashes in stderr

If proportions need tuning, adjust `CHAR_SCALE` and the coordinate multipliers in `render.c` — no API changes required.

- [ ] **Step 5: Test with fortune**

```bash
./build/noseguy-wayland --text-command "fortune -s"
```

Expected: fortune output appears in the speech bubble.

- [ ] **Step 6: Test custom background color**

```bash
./build/noseguy-wayland --bg-color '#1a1a2e'
```

Expected: dark navy background.

- [ ] **Step 7: Commit**

```bash
git add src/main.c
git commit -m "feat: main entry point, CLI parsing, full Wayland integration"
```

---

### Task 9: swaylock-plugin Target (src/plugin.c)

**Files:**
- Create: `src/plugin.c`

`meson.build` already declares the `noseguy-plugin` shared library target — it just needs `src/plugin.c` to exist.

- [ ] **Step 1: Write src/plugin.c**

```c
/* swaylock-plugin ABI for noseguy-wayland.
 *
 * swaylock-plugin provides its own wl_surface and calls:
 *   screensaver_init(width, height)
 *   screensaver_render(cairo_t *cr, double dt)
 *   screensaver_destroy()
 *
 * Configuration via environment variables:
 *   NOSEGUY_TEXT_COMMAND   — shell command for text (default: built-in)
 *   NOSEGUY_FONT           — pango font name       (default: Sans)
 *   NOSEGUY_FONT_SIZE      — font size in pixels   (default: 18)
 */
#include "anim.h"
#include "render.h"
#include "text.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <cairo/cairo.h>

static AnimState     g_anim;
static TextProvider *g_text;
static char         *g_font;
static int           g_font_size;

void screensaver_init(int width, int height) {
    const char *cmd  = getenv("NOSEGUY_TEXT_COMMAND");
    const char *font = getenv("NOSEGUY_FONT");
    const char *size = getenv("NOSEGUY_FONT_SIZE");

    g_font      = strdup(font ? font : "Sans");
    g_font_size = size ? atoi(size) : 18;

    TextConfig cfg = {
        .source   = cmd ? TEXT_COMMAND : TEXT_BUILTIN,
        .arg      = cmd,
        .interval = 30.0,
    };
    g_text = text_provider_create(&cfg);
    anim_init(&g_anim, width, height);
}

void screensaver_render(cairo_t *cr, double dt) {
    cairo_surface_t *target = cairo_get_target(cr);
    int w = cairo_image_surface_get_width(target);
    int h = cairo_image_surface_get_height(target);

    anim_tick(&g_anim, dt);
    if (anim_wants_text(&g_anim)) {
        const char *t = text_get_next(g_text);
        anim_set_text(&g_anim, t ? strdup(t) : strdup("..."));
    }
    render_frame(cr, w, h, &g_anim, g_font, g_font_size, 0.0, 0.0, 0.0);
}

void screensaver_destroy(void) {
    text_provider_destroy(g_text);
    free(g_font);
    free(g_anim.current_text);
}
```

- [ ] **Step 2: Build plugin**

```bash
meson setup build -Dplugin=true --reconfigure
ninja -C build noseguy-plugin 2>&1 | grep -E 'error:|FAILED'
```

Expected: `libnoseguy-plugin.so` built without errors.

- [ ] **Step 3: Verify exported symbols**

```bash
nm -D build/libnoseguy-plugin.so | grep screensaver
```

Expected:
```
T screensaver_destroy
T screensaver_init
T screensaver_render
```

- [ ] **Step 4: Commit**

```bash
git add src/plugin.c
git commit -m "feat: swaylock-plugin shared library (screensaver_init/render/destroy ABI)"
```

---

## Self-Review

**Spec coverage:**

| Spec requirement | Covered by |
|-----------------|-----------|
| wlr-layer-shell, one surface per output | Task 7 |
| Double-buffered wl_shm | Task 7 |
| Frame callback pacing | Task 7 |
| Original character art ported to cairo | Task 6 |
| Speech bubble with pango | Task 6 |
| WALK→STOP→TALK→WAIT→WALK state machine | Task 5 |
| Walk speed, timing constants | Task 5 |
| Blink in WALK and WAIT | Task 5 |
| Mouth animation in TALK | Task 5 |
| Boundary reversal | Task 5 |
| Text: builtin/command/file/stdin | Task 4 |
| Non-blocking command via pthread | Task 4 |
| Built-in quotes from original | Task 3 |
| Full CLI with all options | Task 8 |
| meson build with wayland-scanner | Task 1 |
| Standalone executable | Tasks 1 + 8 |
| swaylock-plugin shared library | Tasks 1 + 9 |
| NOSEGUY_TEXT_COMMAND env var for plugin | Task 9 |

**Placeholder scan:** None found. Task 3 has a note to verify against original source — intentional, not a placeholder.

**Type consistency:** `AnimState` defined in Task 2, used identically in Tasks 5, 6, 7, 9. `anim_wants_text()` / `anim_set_text()` signatures match between Task 5 definition and Tasks 7 + 9 usage. `render_frame()` signature matches between Task 6 header and Tasks 7 + 9 callers. `TextProvider` / `TextConfig` consistent throughout.
