#define _POSIX_C_SOURCE 200809L
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
#include <signal.h>
#include <linux/input-event-codes.h>

static volatile bool *g_running = NULL;

static void sigterm_handler(int sig) {
    (void)sig;
    if (g_running) *g_running = false;
}

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

/* Render into the back buffer, attach it, damage the surface, then
 * request the next frame callback and commit.  Every commit has a
 * buffer attached, which is required for the compositor to fire the
 * frame callback. */
static void render_and_commit(Output *out) {
    int idx = out->buf_idx ^ 1;

    cairo_t *cr = cairo_create(out->cs[idx]);
    render_frame(cr, out->width, out->height, &out->anim,
                 out->app->font_name, out->app->font_size,
                 out->app->bg_r, out->app->bg_g, out->app->bg_b,
                 &out->app->bubble, out->app->sprites);
    cairo_destroy(cr);
    cairo_surface_flush(out->cs[idx]);

    wl_surface_attach(out->surface, out->bufs[idx], 0, 0);
    wl_surface_damage_buffer(out->surface, 0, 0, out->width, out->height);

    struct wl_callback *cb = wl_surface_frame(out->surface);
    wl_callback_add_listener(cb, &frame_listener, out);

    wl_surface_commit(out->surface);
    out->buf_idx = idx;
}

static void frame_done(void *data, struct wl_callback *cb, uint32_t ms) {
    Output *out = data;
    wl_callback_destroy(cb);
    if (!out->configured || !out->app->running) return;

    /* Gate rendering at the configured FPS.  When it is not yet time to
     * render, re-arm the frame callback via an empty commit (no new buffer,
     * no Cairo work, no GPU draw) so the chain stays alive. */
    uint32_t interval_ms = 1000u / (uint32_t)out->app->fps;
    bool should_render = (out->last_frame_ms == 0) ||
                         ((ms - out->last_frame_ms) >= interval_ms);

    if (!should_render) {
        struct wl_callback *next_cb = wl_surface_frame(out->surface);
        wl_callback_add_listener(next_cb, &frame_listener, out);
        wl_surface_commit(out->surface);
        return;
    }

    /* Use the actual elapsed wall-clock time so animation speed is
     * independent of whether the compositor fires callbacks at 30 Hz,
     * 60 Hz, or any other rate. */
    double dt = (out->last_frame_ms == 0)
                ? (1.0 / out->app->fps)
                : ((ms - out->last_frame_ms) / 1000.0);
    out->last_frame_ms = ms;

    anim_tick(&out->anim, dt);

    if (anim_wants_text(&out->anim)) {
        const char *t = text_get_next(out->app->text);
        anim_set_text(&out->anim, t ? strdup(t) : strdup("..."));
    }

    render_and_commit(out);
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
        int wpm = out->app->reading_wpm > 0 ? out->app->reading_wpm : 200;
        out->anim.reading_cps = wpm * 5.0 / 60.0;  /* avg 5 chars/word */
    }
    if (!out->configured) {
        out->configured = true;
        render_and_commit(out);
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
        ZWLR_LAYER_SHELL_V1_LAYER_TOP, "noseguy");

    /* Anchor to all edges + size 0,0 → compositor fills the full output */
    zwlr_layer_surface_v1_set_anchor(out->layer_surface,
        ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP    |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT   |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
    zwlr_layer_surface_v1_set_size(out->layer_surface, 0, 0);
    zwlr_layer_surface_v1_set_exclusive_zone(out->layer_surface, -1);
    zwlr_layer_surface_v1_set_keyboard_interactivity(out->layer_surface,
        ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE);
    zwlr_layer_surface_v1_add_listener(out->layer_surface,
                                        &layer_surface_listener, out);
    wl_surface_commit(out->surface);

    out->next    = app->outputs;
    app->outputs = out;
}

/* ── Keyboard input ───────────────────────────────────────────────── */

static void kbd_keymap(void *d, struct wl_keyboard *k,
    uint32_t fmt, int32_t fd, uint32_t sz)
    { (void)d; (void)k; (void)fmt; (void)sz; close(fd); }

static void kbd_enter(void *d, struct wl_keyboard *k,
    uint32_t s, struct wl_surface *surf, struct wl_array *a)
    { (void)d; (void)k; (void)s; (void)surf; (void)a; }

static void kbd_leave(void *d, struct wl_keyboard *k,
    uint32_t s, struct wl_surface *surf)
    { (void)d; (void)k; (void)s; (void)surf; }

static void kbd_key(void *data, struct wl_keyboard *k,
    uint32_t serial, uint32_t time, uint32_t key, uint32_t state)
{
    (void)k; (void)serial; (void)time;
    App *app = data;
    if (state != WL_KEYBOARD_KEY_STATE_PRESSED) return;
    /* idle-mode: any key dismisses (Esc not special — avoids accidental close on resume) */
    /* normal mode: only Esc quits */
    if (app->idle_mode || key == KEY_ESC)
        app->running = false;
}

static void kbd_modifiers(void *d, struct wl_keyboard *k,
    uint32_t s, uint32_t md, uint32_t ml, uint32_t mk, uint32_t g)
    { (void)d; (void)k; (void)s; (void)md; (void)ml; (void)mk; (void)g; }

static void kbd_repeat_info(void *d, struct wl_keyboard *k,
    int32_t rate, int32_t delay)
    { (void)d; (void)k; (void)rate; (void)delay; }

static const struct wl_keyboard_listener kbd_listener = {
    .keymap      = kbd_keymap,
    .enter       = kbd_enter,
    .leave       = kbd_leave,
    .key         = kbd_key,
    .modifiers   = kbd_modifiers,
    .repeat_info = kbd_repeat_info,
};

/* ── Pointer input ────────────────────────────────────────────────── */

static void ptr_enter(void *d, struct wl_pointer *p,
    uint32_t s, struct wl_surface *surf, wl_fixed_t x, wl_fixed_t y)
    { (void)d; (void)p; (void)s; (void)surf; (void)x; (void)y; }

static void ptr_leave(void *d, struct wl_pointer *p,
    uint32_t s, struct wl_surface *surf)
    { (void)d; (void)p; (void)s; (void)surf; }

static void ptr_motion(void *data, struct wl_pointer *p,
    uint32_t time, wl_fixed_t x, wl_fixed_t y)
{
    (void)p; (void)time; (void)x; (void)y;
    App *app = data;
    if (app->idle_mode) app->running = false;
}

static void ptr_button(void *data, struct wl_pointer *p,
    uint32_t serial, uint32_t time, uint32_t button, uint32_t state)
{
    (void)p; (void)serial; (void)time; (void)button;
    App *app = data;
    if (app->idle_mode && state == WL_POINTER_BUTTON_STATE_PRESSED)
        app->running = false;
}

static void ptr_axis(void *d, struct wl_pointer *p,
    uint32_t t, uint32_t axis, wl_fixed_t v)
    { (void)d; (void)p; (void)t; (void)axis; (void)v; }

static const struct wl_pointer_listener ptr_listener = {
    .enter  = ptr_enter,
    .leave  = ptr_leave,
    .motion = ptr_motion,
    .button = ptr_button,
    .axis   = ptr_axis,
};

static void seat_capabilities(void *data, struct wl_seat *seat, uint32_t caps) {
    App *app = data;
    if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !app->keyboard) {
        app->keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(app->keyboard, &kbd_listener, app);
    }
    if ((caps & WL_SEAT_CAPABILITY_POINTER) && !app->pointer) {
        app->pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(app->pointer, &ptr_listener, app);
    }
}

static void seat_name(void *d, struct wl_seat *s, const char *n)
    { (void)d; (void)s; (void)n; }

static const struct wl_seat_listener seat_listener =
    { seat_capabilities, seat_name };

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
    else if (!strcmp(iface, wl_seat_interface.name)) {
        app->seat = wl_registry_bind(reg, name, &wl_seat_interface, 4);
        wl_seat_add_listener(app->seat, &seat_listener, app);
    } else if (!strcmp(iface, wl_output_interface.name)) {
        struct wl_output *wo = wl_registry_bind(reg, name, &wl_output_interface,  2);
        output_create(app, wo);
    }
}

static void registry_remove(void *d, struct wl_registry *r, uint32_t n)
    { (void)d; (void)r; (void)n; }

static const struct wl_registry_listener registry_listener =
    { registry_global, registry_remove };

/* ── Public API ───────────────────────────────────────────────────── */

/* Sprite file names in SPR_* order */
static const char *sprite_files[SPR_COUNT] = {
    "nose-f1.png", "nose-f2.png", "nose-f3.png", "nose-f4.png",
    "nose-l1.png", "nose-l2.png", "nose-r1.png", "nose-r2.png",
};

void wayland_load_sprites(App *app, const char *dir) {
    if (!dir) return;
    char path[4096];
    for (int i = 0; i < SPR_COUNT; i++) {
        snprintf(path, sizeof(path), "%s/%s", dir, sprite_files[i]);
        app->sprites[i] = cairo_image_surface_create_from_png(path);
        if (cairo_surface_status(app->sprites[i]) != CAIRO_STATUS_SUCCESS) {
            fprintf(stderr, "Warning: cannot load sprite %s: %s\n",
                    path,
                    cairo_status_to_string(
                        cairo_surface_status(app->sprites[i])));
            cairo_surface_destroy(app->sprites[i]);
            app->sprites[i] = NULL;
        }
    }
    /* If any sprite failed, disable sprites entirely (use vector fallback) */
    for (int i = 0; i < SPR_COUNT; i++) {
        if (!app->sprites[i]) {
            for (int j = 0; j < SPR_COUNT; j++) {
                if (app->sprites[j]) {
                    cairo_surface_destroy(app->sprites[j]);
                    app->sprites[j] = NULL;
                }
            }
            fprintf(stderr, "Sprites incomplete — using vector character.\n");
            return;
        }
    }
}

bool wayland_init(App *app) {
    app->display = wl_display_connect(NULL);
    if (!app->display) {
        fprintf(stderr, "Cannot connect to Wayland display\n");
        return false;
    }
    app->registry = wl_display_get_registry(app->display);
    wl_registry_add_listener(app->registry, &registry_listener, app);
    wl_display_roundtrip(app->display);
    if (!app->compositor || !app->shm || !app->layer_shell) {
        fprintf(stderr, "Missing required Wayland globals\n");
        return false;
    }
    wl_display_roundtrip(app->display);
    return true;
}

void wayland_run(App *app) {
    app->running = true;
    g_running = &app->running;
    signal(SIGTERM, sigterm_handler);
    while (app->running && wl_display_dispatch(app->display) != -1)
        ;
    g_running = NULL;
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
    for (int i = 0; i < SPR_COUNT; i++)
        if (app->sprites[i]) { cairo_surface_destroy(app->sprites[i]); app->sprites[i] = NULL; }
    if (app->pointer)     wl_pointer_destroy(app->pointer);
    if (app->keyboard)    wl_keyboard_destroy(app->keyboard);
    if (app->seat)        wl_seat_destroy(app->seat);
    if (app->layer_shell) zwlr_layer_shell_v1_destroy(app->layer_shell);
    if (app->shm)         wl_shm_destroy(app->shm);
    if (app->compositor)  wl_compositor_destroy(app->compositor);
    if (app->registry)    wl_registry_destroy(app->registry);
    if (app->display)     wl_display_disconnect(app->display);
}
