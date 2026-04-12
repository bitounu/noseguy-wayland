#define _POSIX_C_SOURCE 200809L
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
        s->blink_frame = 0;
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
