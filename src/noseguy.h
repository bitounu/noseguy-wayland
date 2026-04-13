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
    double        vy;             /* vertical velocity, px/tick       */
    int           frame;          /* walk cycle index 0–3             */
    int           blink_frame;    /* 0=open  1=squint  2=closed       */
    double        blink_timer;    /* seconds until next blink event   */
    AnimStateKind state;
    double        state_timer;    /* seconds remaining in state       */
    bool          mouth_open;
    double        mouth_timer;    /* seconds until mouth toggle       */
    char         *current_text;   /* owned string; NULL outside TALK  */
    double        reading_cps;    /* chars/sec for talk-time calc     */
    int           width;          /* output width  (pixels)           */
    int           height;         /* output height (pixels)           */
} AnimState;

/* ── Sprite indices ──────────────────────────────────────────────── */

typedef enum {
    SPR_F1 = 0,  /* front, still / neutral   */
    SPR_F2,      /* front, legs left         */
    SPR_F3,      /* front, legs right        */
    SPR_F4,      /* front, bent head / hat   */
    SPR_L1,      /* walking left, step 1     */
    SPR_L2,      /* walking left, step 2     */
    SPR_R1,      /* walking right, step 1    */
    SPR_R2,      /* walking right, step 2    */
    SPR_COUNT,
} SpriteIdx;

/* ── Bubble / font colours ───────────────────────────────────────── */

typedef struct {
    double fill_r,   fill_g,   fill_b,   fill_a;
    double border_r, border_g, border_b, border_a;
    double border_width;
    double font_r,   font_g,   font_b;
} BubbleConfig;

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
