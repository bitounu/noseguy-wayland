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

    out->next    = app->outputs;
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
