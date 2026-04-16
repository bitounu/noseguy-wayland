#define _GNU_SOURCE
#include "render.h"
#include <pango/pangocairo.h>
#include <math.h>

/* Character height = output_height * CHAR_SCALE.
 * All sub-functions receive `u` = CHAR_SCALE * height / 10.0
 * so that 1 character unit ≈ one tenth of the character height. */
#define CHAR_SCALE RENDER_CHAR_SCALE

/* ── Vector character (fallback when no sprites supplied) ─────────── */

static void draw_legs(cairo_t *cr, double cx, double cy, double u,
                      const AnimState *a)
{
    static const double angles[4][2] = {
        { -0.40,  0.40 },
        { -0.15,  0.15 },
        {  0.40, -0.40 },
        {  0.15, -0.15 },
    };
    double hip_y   = cy - 3.5 * u;
    double leg_len = 3.8 * u;
    int f = (a->state == STATE_WALK) ? a->frame : 1;

    cairo_set_line_width(cr, u * 0.5);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_source_rgb(cr, 0.25, 0.25, 0.55);

    double la = angles[f][0], ra = angles[f][1];
    cairo_move_to(cr, cx - u * 0.2, hip_y);
    cairo_line_to(cr, cx - u * 0.2 + sin(la) * leg_len,
                      hip_y           + cos(la) * leg_len);
    cairo_stroke(cr);
    cairo_move_to(cr, cx + u * 0.2, hip_y);
    cairo_line_to(cr, cx + u * 0.2 + sin(ra) * leg_len,
                      hip_y           + cos(ra) * leg_len);
    cairo_stroke(cr);

    cairo_set_source_rgb(cr, 0.15, 0.10, 0.05);
    cairo_arc(cr, cx - u*0.2 + sin(la)*leg_len,
                  hip_y       + cos(la)*leg_len, u*0.4, 0, 2*M_PI);
    cairo_fill(cr);
    cairo_arc(cr, cx + u*0.2 + sin(ra)*leg_len,
                  hip_y       + cos(ra)*leg_len, u*0.4, 0, 2*M_PI);
    cairo_fill(cr);
}

static void draw_body(cairo_t *cr, double cx, double cy, double u)
{
    double bx = cx - u * 0.8, by = cy - 7.2 * u;
    double bw = u * 1.6,      bh = u * 3.7;
    double r  = u * 0.35;

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
    double hx = cx, hy = cy - 9.8 * u, hr = u * 2.4;
    cairo_set_source_rgb(cr, 0.95, 0.80, 0.65);
    cairo_arc(cr, hx, hy, hr, 0, 2*M_PI);
    cairo_fill(cr);
    cairo_set_source_rgb(cr, 0.22, 0.13, 0.04);
    cairo_set_line_width(cr, u * 0.6);
    cairo_arc(cr, hx, hy, hr * 0.96, M_PI + 0.25, 2*M_PI - 0.25);
    cairo_stroke(cr);
}

static void draw_nose(cairo_t *cr, double cx, double cy, double u,
                      const AnimState *a)
{
    double nx = cx + a->dir * u * 1.1, ny = cy - 9.5 * u;
    cairo_save(cr);
    cairo_translate(cr, nx, ny);
    cairo_scale(cr, u * 1.5, u * 1.0);
    cairo_set_source_rgb(cr, 0.88, 0.48, 0.43);
    cairo_arc(cr, 0, 0, 1.0, 0, 2*M_PI); cairo_fill(cr);
    cairo_set_source_rgb(cr, 0.58, 0.28, 0.26);
    cairo_arc(cr, -0.30, 0.25, 0.22, 0, 2*M_PI); cairo_fill(cr);
    cairo_arc(cr,  0.30, 0.25, 0.22, 0, 2*M_PI); cairo_fill(cr);
    cairo_restore(cr);
}

static void draw_eyes(cairo_t *cr, double cx, double cy, double u,
                      const AnimState *a)
{
    double ey = cy - 10.8 * u, elx = cx - u * 0.9, erx = cx + u * 0.9;
    double er = u * 0.38;

    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_arc(cr, elx, ey, er, 0, 2*M_PI); cairo_fill(cr);
    cairo_arc(cr, erx, ey, er, 0, 2*M_PI); cairo_fill(cr);

    if (a->blink_frame == 2) {
        cairo_set_source_rgb(cr, 0.1, 0.1, 0.1);
        cairo_set_line_width(cr, u * 0.18);
        cairo_move_to(cr, elx - er, ey); cairo_line_to(cr, elx + er, ey); cairo_stroke(cr);
        cairo_move_to(cr, erx - er, ey); cairo_line_to(cr, erx + er, ey); cairo_stroke(cr);
    } else {
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
    double mx = cx, my = cy - 8.5 * u;
    if (a->state == STATE_TALK && a->mouth_open) {
        cairo_save(cr);
        cairo_translate(cr, mx, my);
        cairo_scale(cr, u * 0.85, u * 0.42);
        cairo_set_source_rgb(cr, 0.55, 0.12, 0.12);
        cairo_arc(cr, 0, 0, 1.0, 0, 2*M_PI); cairo_fill(cr);
        cairo_restore(cr);
    } else {
        cairo_set_source_rgb(cr, 0.48, 0.14, 0.14);
        cairo_set_line_width(cr, u * 0.22);
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
        cairo_arc(cr, mx, my - u * 0.18, u * 0.72, 0.18, M_PI - 0.18);
        cairo_stroke(cr);
    }
}

static void draw_vector_char(cairo_t *cr, double cx, double cy, double u,
                             const AnimState *a)
{
    draw_legs (cr, cx, cy, u, a);
    draw_body (cr, cx, cy, u);
    draw_head (cr, cx, cy, u);
    draw_nose (cr, cx, cy, u, a);
    draw_eyes (cr, cx, cy, u, a);
    draw_mouth(cr, cx, cy, u, a);
}

/* ── Sprite character ─────────────────────────────────────────────── */

static SpriteIdx sprite_for_state(const AnimState *a)
{
    switch (a->state) {
    case STATE_WALK:
        if (a->dir == DIR_LEFT)
            return (a->frame & 1) ? SPR_L2 : SPR_L1;
        else
            return (a->frame & 1) ? SPR_R2 : SPR_R1;
    case STATE_STOP:
        return (a->dir == DIR_LEFT) ? SPR_F2 : SPR_F3;
    case STATE_TALK:
        return SPR_F1;
    case STATE_WAIT:
        return SPR_F4;
    }
    return SPR_F1;
}

static void draw_sprite_char(cairo_t *cr, double cx, double cy,
                             double char_height,
                             cairo_surface_t *const sprites[SPR_COUNT],
                             const AnimState *a)
{
    SpriteIdx idx = sprite_for_state(a);
    cairo_surface_t *spr = sprites[idx];
    if (!spr || cairo_surface_status(spr) != CAIRO_STATUS_SUCCESS) return;

    int sw = cairo_image_surface_get_width(spr);
    int sh = cairo_image_surface_get_height(spr);
    if (sw == 0 || sh == 0) return;

    double scale = char_height / (double)sh;
    double dx = cx - (sw * scale) / 2.0;
    double dy = cy - sh * scale;   /* feet at cy */

    cairo_save(cr);
    if (fabs(scale - 1.0) < 0.02) {
        /* Pre-scaled sprite: direct blit without matrix change */
        cairo_set_source_surface(cr, spr, dx, dy);
    } else {
        cairo_translate(cr, dx, dy);
        cairo_scale(cr, scale, scale);
        cairo_set_source_surface(cr, spr, 0, 0);
    }
    cairo_paint(cr);
    cairo_restore(cr);
}

/* ── Speech bubble ────────────────────────────────────────────────── */

static void draw_speech_bubble(cairo_t *cr, int width, int height,
                                const AnimState *a,
                                const char *font_name, int font_size,
                                double u, const BubbleConfig *bc)
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

    int    pad  = font_size;
    int    bw   = tw + pad * 2;
    int    bh   = th + pad * 2;
    int    tail = (int)(u * 2.2);
    double r    = pad * 0.55;

    /* Character extents: head top ≈ y−12.5u, feet = y */
    double head_top = a->y - 12.5 * u;
    double feet_bot = a->y;

    /* Default: bubble above head */
    int bx   = (int)(a->x - bw / 2);
    int by   = (int)(head_top - bh - tail - u * 0.5);
    bool flip = false;  /* false = tail at bottom pointing down to head */

    /* Clamp horizontal */
    if (bx < 10)              bx = 10;
    if (bx + bw > width - 10) bx = width - bw - 10;

    /* If bubble would overlap the character, flip below the feet */
    if (by < 10 || by + bh + tail > head_top - u) {
        by   = (int)(feet_bot + u * 1.5);
        flip = true;
    }

    /* Clamp vertical (shouldn't be needed but safety) */
    if (!flip && by < 10)              by = 10;
    if ( flip && by + bh > height - 10) by = height - bh - 10;

    double tcx = fmax(bx + r, fmin(bx + bw - r, a->x));

    /* Draw bubble shape */
    cairo_new_path(cr);
    if (!flip) {
        /* Normal: tail at bottom */
        cairo_arc(cr, bx + r,      by + r,      r, M_PI,      3*M_PI/2);
        cairo_arc(cr, bx + bw - r, by + r,      r, 3*M_PI/2,  2*M_PI);
        cairo_arc(cr, bx + bw - r, by + bh - r, r, 0,         M_PI/2);
        cairo_line_to(cr, tcx + u, by + bh);
        cairo_line_to(cr, tcx,     by + bh + tail);
        cairo_line_to(cr, tcx - u, by + bh);
        cairo_arc(cr, bx + r,      by + bh - r, r, M_PI/2,    M_PI);
    } else {
        /* Flipped: tail at top */
        cairo_arc(cr, bx + r,      by + r,      r, M_PI,      3*M_PI/2);
        cairo_line_to(cr, tcx - u, by);
        cairo_line_to(cr, tcx,     by - tail);
        cairo_line_to(cr, tcx + u, by);
        cairo_arc(cr, bx + bw - r, by + r,      r, 3*M_PI/2,  2*M_PI);
        cairo_arc(cr, bx + bw - r, by + bh - r, r, 0,         M_PI/2);
        cairo_arc(cr, bx + r,      by + bh - r, r, M_PI/2,    M_PI);
    }
    cairo_close_path(cr);

    cairo_set_source_rgba(cr, bc->fill_r, bc->fill_g, bc->fill_b, bc->fill_a);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, bc->border_r, bc->border_g, bc->border_b, bc->border_a);
    cairo_set_line_width(cr, bc->border_width);
    cairo_stroke(cr);

    cairo_set_source_rgb(cr, bc->font_r, bc->font_g, bc->font_b);
    cairo_move_to(cr, bx + pad, by + pad);
    pango_cairo_show_layout(cr, layout);
    g_object_unref(layout);

    (void)height;
}

/* ── Public API ───────────────────────────────────────────────────── */

void render_frame(cairo_t *cr, int width, int height,
                  const AnimState *a,
                  const char *font_name, int font_size,
                  double bg_r, double bg_g, double bg_b,
                  const BubbleConfig *bubble,
                  cairo_surface_t *const sprites[SPR_COUNT])
{
    double u           = height * CHAR_SCALE / 10.0;
    double char_height = height * CHAR_SCALE;

    cairo_set_source_rgb(cr, bg_r, bg_g, bg_b);
    cairo_paint(cr);

    if (sprites && sprites[0])
        draw_sprite_char(cr, a->x, a->y, char_height, sprites, a);
    else
        draw_vector_char(cr, a->x, a->y, u, a);

    if (a->state == STATE_TALK)
        draw_speech_bubble(cr, width, height, a, font_name, font_size, u, bubble);
}
