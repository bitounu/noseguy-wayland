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
