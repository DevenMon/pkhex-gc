#include "uinput.h"

uint32_t ui_stick_direction(int x, int y)
{
    const int ax = x < 0 ? -x : x;
    const int ay = y < 0 ? -y : y;

    /* Neither axis far enough out to mean anything. */
    if (ax < UI_STICK_THRESHOLD && ay < UI_STICK_THRESHOLD) return 0u;

    /* A dead heat is a diagonal held exactly on the gate corner. Reporting
     * both is what caused the damage; reporting either would be a guess. */
    if (ax == ay) return 0u;

    if (ay > ax) return y > 0 ? UI_STICK_UP : UI_STICK_DOWN;
    return x > 0 ? UI_STICK_RIGHT : UI_STICK_LEFT;
}

uint32_t ui_input_step(UiInput *in, uint32_t edges, uint32_t held)
{
    uint32_t down = edges;

    /* The stick-derived directions have no hardware press edge, so derive one:
     * a repeatable button that was not held last frame is a fresh press. */
    const uint32_t nav = held & in->repeatable;
    down |= nav & ~in->previous_nav;

    if (nav != 0u && nav == in->previous_nav) {
        ++in->repeat_frames;
        if (in->repeat_frames == UI_REPEAT_DELAY ||
            (in->repeat_frames > UI_REPEAT_DELAY &&
             ((in->repeat_frames - UI_REPEAT_DELAY) % UI_REPEAT_PERIOD) == 0u))
            down |= nav;
    } else {
        in->repeat_frames = 0u;
    }

    /* Suppress anything that was already down when the screen changed, until
     * it is released. Releasing clears its bit, so the next press acts. */
    in->carry_block &= held;
    down &= ~in->carry_block;

    in->previous_nav = nav;
    in->last_held = held;

    /*
     * Fold the stick onto the D-pad's bits. The two are kept apart all the way
     * to here so each repeats on its own clock - holding the stick and tapping
     * the D-pad are separate presses - and so the D-pad can be recognised.
     */
    const uint32_t dpad = in->dir_up | in->dir_down | in->dir_left | in->dir_right;
    if (down & dpad) down |= UI_COARSE;
    if (down & UI_STICK_UP)    down |= in->dir_up;
    if (down & UI_STICK_DOWN)  down |= in->dir_down;
    if (down & UI_STICK_LEFT)  down |= in->dir_left;
    if (down & UI_STICK_RIGHT) down |= in->dir_right;
    return down & ~UI_STICK_ANY;
}

void ui_input_screen_changed(UiInput *in)
{
    in->carry_block = in->last_held;
    in->repeat_frames = 0u;
}
