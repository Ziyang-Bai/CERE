#ifndef INPUT_H
#define INPUT_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool page_next;
    bool page_prev;
    bool nav_up;
    bool nav_down;
    bool menu;
    bool confirm;
    bool panel;
    bool bookmark;
    bool exit;
} input_events_t;

typedef struct {
    bool right_down;
    bool left_down;
    bool up_down;
    bool down_down;
    bool second_down;
    bool enter_down;
    bool stat_down;
    bool vars_down;
    bool alpha_down;
    bool clear_down;
} input_state_t;

void input_init(input_state_t *state);
void input_poll(input_state_t *state, input_events_t *events);

#endif
