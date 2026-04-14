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
struct wl_buffer;    struct wl_seat;      struct wl_keyboard;
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
    struct wl_seat               *seat;
    struct wl_keyboard           *keyboard;
    struct wl_pointer            *pointer;

    Output                       *outputs;
    TextProvider                 *text;

    int                           fps;
    int                           reading_wpm;  /* 0 = use default (200) */
    char                         *font_name;
    int                           font_size;
    double                        bg_r, bg_g, bg_b;
    BubbleConfig                  bubble;

    /* Sprites — NULL entries fall back to vector drawing */
    cairo_surface_t              *sprites[SPR_COUNT];

    bool                          running;
    bool                          idle_mode;  /* --idle-mode: ignore Esc, exit only on SIGTERM */
};

bool wayland_init(App *app);
void wayland_run(App *app);
void wayland_destroy(App *app);
void wayland_load_sprites(App *app, const char *dir);
