#define _GNU_SOURCE
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

    /* Feet */
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

    /* Hair arc */
    cairo_set_source_rgb(cr, 0.22, 0.13, 0.04);
    cairo_set_line_width(cr, u * 0.6);
    cairo_arc(cr, hx, hy, hr * 0.96, M_PI + 0.25, 2*M_PI - 0.25);
    cairo_stroke(cr);
}

static void draw_nose(cairo_t *cr, double cx, double cy, double u,
                      const AnimState *a)
{
    double nx = cx + a->dir * u * 1.1;
    double ny = cy - 9.5 * u;

    cairo_save(cr);
    cairo_translate(cr, nx, ny);
    cairo_scale(cr, u * 1.5, u * 1.0);
    cairo_set_source_rgb(cr, 0.88, 0.48, 0.43);
    cairo_arc(cr, 0, 0, 1.0, 0, 2*M_PI);
    cairo_fill(cr);
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

    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_arc(cr, elx, ey, er, 0, 2*M_PI); cairo_fill(cr);
    cairo_arc(cr, erx, ey, er, 0, 2*M_PI); cairo_fill(cr);

    if (a->blink_frame == 2) {
        cairo_set_source_rgb(cr, 0.1, 0.1, 0.1);
        cairo_set_line_width(cr, u * 0.18);
        cairo_move_to(cr, elx - er, ey); cairo_line_to(cr, elx + er, ey);
        cairo_stroke(cr);
        cairo_move_to(cr, erx - er, ey); cairo_line_to(cr, erx + er, ey);
        cairo_stroke(cr);
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

    if (bx < 10)               bx = 10;
    if (bx + bw > width - 10)  bx = width - bw - 10;
    if (by < 10)               by = 10;

    double r   = pad * 0.55;
    double tcx = fmax(bx + r, fmin(bx + bw - r, a->x));

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
