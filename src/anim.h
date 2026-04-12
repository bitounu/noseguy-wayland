#pragma once

#include "noseguy.h"

/* Initialize AnimState for given output dimensions.
 * Positions character randomly in the middle third of the screen. */
void anim_init(AnimState *s, int width, int height);

/* Advance state machine by dt seconds. Call once per frame. */
void anim_tick(AnimState *s, double dt);

/* True on the first tick after entering TALK with no text yet assigned.
 * Caller must call anim_set_text() when this returns true. */
bool anim_wants_text(const AnimState *s);

/* Feed text into TALK state. Takes ownership of the string (must be
 * heap-allocated; enter_state(WAIT) will free it on state exit). */
void anim_set_text(AnimState *s, char *text);
