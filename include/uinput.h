/*
 * Controller edge and repeat handling.
 *
 * This lives apart from main.c so it can be exercised on the host. It earned
 * that: a held shoulder button used to carry its auto-repeat across a screen
 * change, and since L and R page through a list on one screen and mean Dex
 * and Save on another, paging and then backing out could save to the card
 * without anyone asking for it.
 */
#ifndef PKHEXGC_UINPUT_H
#define PKHEXGC_UINPUT_H

#include <stdint.h>

/* Frames a direction must be held before it starts repeating, and the gap
 * between repeats after that. Slow enough to select one row, fast enough to
 * cross a 400-entry list. */
#define UI_REPEAT_DELAY 18u
#define UI_REPEAT_PERIOD 5u

/*
 * The analog stick's four directions, synthesised from its position. They used
 * to be the same four bits the D-pad sets, which made the D-pad four inputs the
 * program could not see - it could not tell the two apart, so it could not give
 * them different jobs. These sit above every hardware button bit.
 */
#define UI_STICK_UP    0x00010000u
#define UI_STICK_DOWN  0x00020000u
#define UI_STICK_LEFT  0x00040000u
#define UI_STICK_RIGHT 0x00080000u
#define UI_STICK_ANY   (UI_STICK_UP | UI_STICK_DOWN | UI_STICK_LEFT | UI_STICK_RIGHT)

/*
 * Set on a frame whose direction came from the D-pad rather than the stick.
 *
 * The stick moves one step and the D-pad ten. Handlers still read one set of
 * direction bits and only consult this where a ten-step means something, so a
 * screen with no coarse step behaves exactly as it did and needs no second code
 * path. It also frees L and R, which used to carry "coarse" on the value
 * editors and now only ever step sideways through pages, boxes and pockets.
 */
#define UI_COARSE      0x00100000u

/* How many single steps one D-pad press is worth. */
#define UI_COARSE_STEP 10

typedef struct {
    uint32_t repeatable;      /* buttons that auto-repeat while held */
    uint32_t previous_nav;    /* repeatable buttons held last frame */
    uint32_t last_held;       /* everything held last frame */
    uint32_t carry_block;     /* held across a screen change, ignored until released */
    unsigned repeat_frames;

    /* The hardware D-pad bits, supplied by the caller so this file needs no
     * libogc header and stays testable on a development machine. */
    uint32_t dir_up, dir_down, dir_left, dir_right;
} UiInput;

/*
 * Turn one frame of controller state into the set of buttons that should act
 * this frame. `edges` is the hardware's own press edges; `held` is everything
 * currently down, with the analog stick's directions as UI_STICK_* rather than
 * as the D-pad's bits.
 *
 * The stick's bits are folded onto the D-pad's on the way out, so a handler
 * sees one direction whichever produced it, plus UI_COARSE when it was the
 * D-pad. UI_STICK_* never reaches a handler.
 */
uint32_t ui_input_step(UiInput *in, uint32_t edges, uint32_t held);

/* Call after a handler changes screens: whatever is still held belongs to the
 * screen being left, not the one being entered. */
void ui_input_screen_changed(UiInput *in);

#endif
