#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "anim.h"

static void test_initial_state(void) {
    AnimState s;
    anim_init(&s, 1920, 1080);
    assert(s.state == STATE_WALK);
    assert(s.x >= 0.0 && s.x <= 1920.0);
    assert(s.dir == DIR_LEFT || s.dir == DIR_RIGHT);
    assert(s.width == 1920 && s.height == 1080);
    printf("PASS: test_initial_state\n");
}

static void test_walk_moves_character(void) {
    AnimState s;
    anim_init(&s, 1920, 1080);
    s.state = STATE_WALK;
    s.dir   = DIR_RIGHT;
    double x_before = s.x;
    anim_tick(&s, 1.0 / 30.0);
    assert(s.x > x_before);
    printf("PASS: test_walk_moves_character\n");
}

static void test_boundary_reversal(void) {
    AnimState s;
    anim_init(&s, 800, 600);
    s.state = STATE_WALK;
    s.dir   = DIR_RIGHT;
    s.x     = 780.0;
    for (int i = 0; i < 60; i++) anim_tick(&s, 1.0 / 30.0);
    assert(s.dir == DIR_LEFT);
    printf("PASS: test_boundary_reversal\n");
}

static void test_walk_to_stop_transition(void) {
    AnimState s;
    anim_init(&s, 1920, 1080);
    s.state       = STATE_WALK;
    s.state_timer = 0.0;
    anim_tick(&s, 1.0 / 30.0);
    assert(s.state == STATE_STOP);
    printf("PASS: test_walk_to_stop_transition\n");
}

static void test_stop_to_talk_transition(void) {
    AnimState s;
    anim_init(&s, 1920, 1080);
    s.state       = STATE_STOP;
    s.state_timer = 0.0;
    anim_tick(&s, 1.0 / 30.0);
    assert(s.state == STATE_TALK);
    printf("PASS: test_stop_to_talk_transition\n");
}

static void test_talk_wants_text(void) {
    AnimState s;
    anim_init(&s, 1920, 1080);
    s.state       = STATE_STOP;
    s.state_timer = 0.0;
    anim_tick(&s, 1.0 / 30.0);
    assert(s.state == STATE_TALK);
    assert(anim_wants_text(&s));
    printf("PASS: test_talk_wants_text\n");
}

static void test_set_text_clears_wants(void) {
    AnimState s;
    anim_init(&s, 1920, 1080);
    s.state       = STATE_STOP;
    s.state_timer = 0.0;
    anim_tick(&s, 1.0 / 30.0);
    anim_set_text(&s, strdup("Hello nose"));
    assert(!anim_wants_text(&s));
    assert(s.current_text != NULL);
    printf("PASS: test_set_text_clears_wants\n");
}

static void test_talk_to_wait_clears_text(void) {
    AnimState s;
    anim_init(&s, 1920, 1080);
    s.state        = STATE_TALK;
    s.state_timer  = 0.0;
    s.current_text = strdup("test");
    anim_tick(&s, 1.0 / 30.0);
    assert(s.state == STATE_WAIT);
    assert(s.current_text == NULL);
    printf("PASS: test_talk_to_wait_clears_text\n");
}

int main(void) {
    test_initial_state();
    test_walk_moves_character();
    test_boundary_reversal();
    test_walk_to_stop_transition();
    test_stop_to_talk_transition();
    test_talk_wants_text();
    test_set_text_clears_wants();
    test_talk_to_wait_clears_text();
    printf("All anim tests passed.\n");
    return 0;
}
