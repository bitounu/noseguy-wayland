#define _POSIX_C_SOURCE 200809L
/*
 * noseguy-plugin.so — swaylock-plugin shared library
 *
 * ABI exported:
 *   void screensaver_init(int width, int height)
 *   void screensaver_render(cairo_t *cr, double dt)
 *   void screensaver_destroy(void)
 *
 * Configuration via environment variables:
 *   NOSEGUY_TEXT_COMMAND   shell command supplying text lines (default: built-in)
 *   NOSEGUY_TEXT_FILE      path to a text file (one quote per line)
 *   NOSEGUY_SPRITES_DIR    directory containing nose-*.png sprite images
 *   NOSEGUY_READING_WPM    reading speed in words/min (default: 200)
 *   NOSEGUY_FONT           pango font name (default: Sans)
 *   NOSEGUY_FONT_SIZE      font size in pixels (default: 18)
 *   NOSEGUY_FONT_COLOR     font color #rrggbb (default: #0d0d0d)
 *   NOSEGUY_BG_COLOR       background color #rrggbb (default: #000000)
 *   NOSEGUY_BUBBLE_COLOR   bubble fill #rrggbbaa (default: #ffffffee)
 *   NOSEGUY_BUBBLE_BORDER  bubble border #rrggbbaa (default: #2e2e2ed9)
 *   NOSEGUY_BUBBLE_WIDTH   bubble border line width (default: 1.5)
 */

#include "anim.h"
#include "render.h"
#include "text.h"
#include "noseguy.h"

#include <cairo/cairo.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ── State ────────────────────────────────────────────────────────── */

static AnimState         g_anim;
static TextProvider     *g_text;
static BubbleConfig      g_bubble;
static cairo_surface_t  *g_sprites[SPR_COUNT];
static char             *g_font;
static int               g_font_size;
static double            g_bg_r, g_bg_g, g_bg_b;
static bool              g_ready;

/* ── Helpers ──────────────────────────────────────────────────────── */

static bool parse_rgb(const char *s, double *r, double *g, double *b) {
    if (!s) return false;
    if (*s == '#') s++;
    unsigned ri, gi, bi;
    if (sscanf(s, "%02x%02x%02x", &ri, &gi, &bi) != 3) return false;
    *r = ri / 255.0; *g = gi / 255.0; *b = bi / 255.0;
    return true;
}

static bool parse_rgba(const char *s,
                       double *r, double *g, double *b, double *a) {
    if (!s) return false;
    if (*s == '#') s++;
    unsigned ri, gi, bi, ai = 255;
    int n = sscanf(s, "%02x%02x%02x%02x", &ri, &gi, &bi, &ai);
    if (n < 3) return false;
    *r = ri / 255.0; *g = gi / 255.0; *b = bi / 255.0; *a = ai / 255.0;
    return true;
}

static void load_sprites(const char *dir) {
    if (!dir) return;
    static const char *names[SPR_COUNT] = {
        "nose-f1.png", "nose-f2.png", "nose-f3.png", "nose-f4.png",
        "nose-l1.png", "nose-l2.png", "nose-r1.png", "nose-r2.png",
    };
    char path[4096];
    for (int i = 0; i < SPR_COUNT; i++) {
        snprintf(path, sizeof(path), "%s/%s", dir, names[i]);
        g_sprites[i] = cairo_image_surface_create_from_png(path);
        if (cairo_surface_status(g_sprites[i]) != CAIRO_STATUS_SUCCESS) {
            fprintf(stderr, "noseguy-plugin: cannot load %s\n", path);
            cairo_surface_destroy(g_sprites[i]);
            g_sprites[i] = NULL;
        }
    }
    /* If any sprite is missing, disable sprites entirely */
    for (int i = 0; i < SPR_COUNT; i++) {
        if (!g_sprites[i]) {
            for (int j = 0; j < SPR_COUNT; j++) {
                if (g_sprites[j]) { cairo_surface_destroy(g_sprites[j]); g_sprites[j] = NULL; }
            }
            fprintf(stderr, "noseguy-plugin: sprites incomplete, using vector fallback\n");
            return;
        }
    }
}

/* ── Plugin ABI ───────────────────────────────────────────────────── */

void screensaver_init(int width, int height) {
    /* Text provider */
    TextConfig tcfg = { .source = TEXT_BUILTIN, .arg = NULL, .interval = 30.0 };
    const char *cmd  = getenv("NOSEGUY_TEXT_COMMAND");
    const char *file = getenv("NOSEGUY_TEXT_FILE");
    if (cmd)  { tcfg.source = TEXT_COMMAND; tcfg.arg = cmd; }
    if (file) { tcfg.source = TEXT_FILE;    tcfg.arg = file; }
    g_text = text_provider_create(&tcfg);

    /* Bubble config defaults */
    g_bubble = (BubbleConfig){
        .fill_r = 1.0,  .fill_g = 1.0,  .fill_b = 1.0,  .fill_a = 0.93,
        .border_r = 0.18,.border_g = 0.18,.border_b = 0.18,.border_a = 0.85,
        .border_width = 1.5,
        .font_r = 0.05, .font_g = 0.05, .font_b = 0.05,
    };
    parse_rgba(getenv("NOSEGUY_BUBBLE_COLOR"),  &g_bubble.fill_r,   &g_bubble.fill_g,   &g_bubble.fill_b,   &g_bubble.fill_a);
    parse_rgba(getenv("NOSEGUY_BUBBLE_BORDER"), &g_bubble.border_r, &g_bubble.border_g, &g_bubble.border_b, &g_bubble.border_a);
    parse_rgb (getenv("NOSEGUY_FONT_COLOR"),    &g_bubble.font_r,   &g_bubble.font_g,   &g_bubble.font_b);
    const char *bw = getenv("NOSEGUY_BUBBLE_WIDTH");
    if (bw) g_bubble.border_width = atof(bw);

    /* Font */
    const char *font = getenv("NOSEGUY_FONT");
    g_font      = strdup(font ? font : "Sans");
    const char *fsz = getenv("NOSEGUY_FONT_SIZE");
    g_font_size = fsz ? atoi(fsz) : 18;

    /* Background color */
    g_bg_r = g_bg_g = g_bg_b = 0.0;
    parse_rgb(getenv("NOSEGUY_BG_COLOR"), &g_bg_r, &g_bg_g, &g_bg_b);

    /* Sprites */
    load_sprites(getenv("NOSEGUY_SPRITES_DIR"));

    /* Animation */
    anim_init(&g_anim, width, height);
    const char *wpm_s = getenv("NOSEGUY_READING_WPM");
    int wpm = wpm_s ? atoi(wpm_s) : 200;
    if (wpm <= 0) wpm = 200;
    g_anim.reading_cps = wpm * 5.0 / 60.0;

    g_ready = true;
}

void screensaver_render(cairo_t *cr, double dt) {
    if (!g_ready) return;

    anim_tick(&g_anim, dt);

    if (anim_wants_text(&g_anim)) {
        const char *t = text_get_next(g_text);
        anim_set_text(&g_anim, t ? strdup(t) : strdup("..."));
    }

    /* Check if the host resized (surface dimensions from the cairo target) */
    cairo_surface_t *surf = cairo_get_target(cr);
    int w = cairo_image_surface_get_width(surf);
    int h = cairo_image_surface_get_height(surf);
    if (w > 0 && h > 0 && (w != g_anim.width || h != g_anim.height))
        anim_init(&g_anim, w, h);

    render_frame(cr, g_anim.width, g_anim.height, &g_anim,
                 g_font, g_font_size,
                 g_bg_r, g_bg_g, g_bg_b,
                 &g_bubble,
                 g_sprites[0] ? g_sprites : NULL);
}

void screensaver_destroy(void) {
    if (!g_ready) return;
    text_provider_destroy(g_text);
    g_text = NULL;
    free(g_font);
    g_font = NULL;
    for (int i = 0; i < SPR_COUNT; i++) {
        if (g_sprites[i]) { cairo_surface_destroy(g_sprites[i]); g_sprites[i] = NULL; }
    }
    g_ready = false;
}
