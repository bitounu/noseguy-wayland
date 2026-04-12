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
    int           frame;          /* walk cycle index 0–3             */
    int           blink_frame;    /* 0=open  1=squint  2=closed       */
    double        blink_timer;    /* seconds until next blink event   */
    AnimStateKind state;
    double        state_timer;    /* seconds remaining in state       */
    bool          mouth_open;
    double        mouth_timer;    /* seconds until mouth toggle       */
    char         *current_text;   /* owned string; NULL outside TALK  */
    int           width;          /* output width  (pixels)           */
    int           height;         /* output height (pixels)           */
} AnimState;

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
