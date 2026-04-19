#include "input.h"

#include <stddef.h>

#include <keypadc.h>

static bool edge_pressed(bool now, bool was_down)
{
    return now && !was_down;
}

void input_init(input_state_t *state)
{
    if (state == NULL) {
        return;
    }
    state->right_down = false;
    state->left_down = false;
    state->up_down = false;
    state->down_down = false;
    state->second_down = false;
    state->enter_down = false;
    state->stat_down = false;
    state->vars_down = false;
    state->alpha_down = false;
    state->clear_down = false;
}

void input_poll(input_state_t *state, input_events_t *events)
{
    uint8_t g1;
    uint8_t g2;
    uint8_t g4;
    uint8_t g5;
    uint8_t g6;
    uint8_t g7;
    bool right_now;
    bool left_now;
    bool up_now;
    bool down_now;
    bool second_now;
    bool enter_now;
    bool stat_now;
    bool vars_now;
    bool alpha_now;
    bool clear_now;

    if (state == NULL || events == NULL) {
        return;
    }

    events->page_next = false;
    events->page_prev = false;
    events->nav_up = false;
    events->nav_down = false;
    events->menu = false;
    events->confirm = false;
    events->panel = false;
    events->bookmark = false;
    events->exit = false;

    kb_Scan();

    g1 = kb_ScanGroup(1u);
    g2 = kb_ScanGroup(2u);
    g4 = kb_ScanGroup(4u);
    g5 = kb_ScanGroup(5u);
    g6 = kb_ScanGroup(6u);
    g7 = kb_ScanGroup(7u);

    right_now = ((kb_Data[7] & kb_Right) != 0u) || ((g7 & kb_Right) != 0u);
    left_now = ((kb_Data[7] & kb_Left) != 0u) || ((g7 & kb_Left) != 0u);
    up_now = ((kb_Data[7] & kb_Up) != 0u) || ((g7 & kb_Up) != 0u);
    down_now = ((kb_Data[7] & kb_Down) != 0u) || ((g7 & kb_Down) != 0u);
    second_now = ((kb_Data[1] & kb_2nd) != 0u) || ((g1 & kb_2nd) != 0u) || kb_IsDown(kb_Key2nd);
    enter_now = ((kb_Data[6] & kb_Enter) != 0u) || ((g6 & kb_Enter) != 0u) || kb_IsDown(kb_KeyEnter);
    stat_now = ((kb_Data[4] & kb_Stat) != 0u) || ((g4 & kb_Stat) != 0u) || kb_IsDown(kb_KeyStat);
    vars_now = ((kb_Data[5] & kb_Vars) != 0u) || ((g5 & kb_Vars) != 0u) || kb_IsDown(kb_KeyVars);
    alpha_now = ((kb_Data[2] & kb_Alpha) != 0u) || ((g2 & kb_Alpha) != 0u) || kb_IsDown(kb_KeyAlpha);
    clear_now = ((kb_Data[6] & kb_Clear) != 0u) || ((g6 & kb_Clear) != 0u) || kb_IsDown(kb_KeyClear);

    events->page_next = edge_pressed(right_now, state->right_down);
    events->page_prev = edge_pressed(left_now, state->left_down);
    events->nav_up = edge_pressed(up_now, state->up_down);
    events->nav_down = edge_pressed(down_now, state->down_down);
    events->confirm = edge_pressed(second_now, state->second_down)
        || edge_pressed(enter_now, state->enter_down);
    events->panel = edge_pressed(stat_now, state->stat_down);
    events->bookmark = edge_pressed(vars_now, state->vars_down);
    events->menu = edge_pressed(alpha_now, state->alpha_down);
    events->exit = edge_pressed(clear_now, state->clear_down);

    state->right_down = right_now;
    state->left_down = left_now;
    state->up_down = up_now;
    state->down_down = down_now;
    state->second_down = second_now;
    state->enter_down = enter_now;
    state->stat_down = stat_now;
    state->vars_down = vars_now;
    state->alpha_down = alpha_now;
    state->clear_down = clear_now;
}
