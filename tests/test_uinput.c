/*
 * Controller edge and repeat handling.
 *
 * The case that matters most here is the last one: a shoulder button held
 * across a screen change used to keep repeating on the screen it arrived at,
 * and on the summary screen that meant Save. Paging through the event flags
 * and backing out wrote to the card.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "uinput.h"

/* Stand-ins for the libogc bits; only their separateness matters. */
#define BTN_A     0x0001u
#define BTN_B     0x0002u
#define BTN_UP    0x0010u
#define BTN_DOWN  0x0020u
#define TRIG_L    0x0040u
#define TRIG_R    0x0080u

static const uint32_t REPEATABLE = BTN_UP | BTN_DOWN | TRIG_L | TRIG_R;

static void reset(UiInput *in)
{
    memset(in, 0, sizeof(*in));
    in->repeatable = REPEATABLE;
}

/* One frame with nothing newly pressed by the hardware, everything derived
 * from what is held - which is how the stick-driven directions arrive. */
static uint32_t frame(UiInput *in, uint32_t held)
{
    return ui_input_step(in, 0u, held);
}

static void test_press_and_release(void)
{
    UiInput in; reset(&in);

    assert(frame(&in, BTN_DOWN) == BTN_DOWN);   /* first frame held is a press */
    assert(frame(&in, BTN_DOWN) == 0u);         /* still held is not */
    assert(frame(&in, 0u) == 0u);
    assert(frame(&in, BTN_DOWN) == BTN_DOWN);   /* released, pressed again */
    printf("  a held direction presses once, not every frame\n");
}

static void test_repeat_timing(void)
{
    UiInput in; reset(&in);
    unsigned fires = 0;

    assert(frame(&in, BTN_DOWN) == BTN_DOWN);
    for (unsigned f = 1; f < UI_REPEAT_DELAY; ++f)
        assert(frame(&in, BTN_DOWN) == 0u);     /* quiet until the delay */
    assert(frame(&in, BTN_DOWN) == BTN_DOWN);   /* first repeat */

    for (unsigned f = 0; f < UI_REPEAT_PERIOD * 4u; ++f)
        if (frame(&in, BTN_DOWN)) ++fires;
    assert(fires == 4u);                        /* then one per period */
    printf("  a direction repeats after the delay, at the fixed period\n");
}

static void test_hardware_edges_pass_through(void)
{
    UiInput in; reset(&in);

    /* A is not repeatable, so it only ever arrives as a hardware edge. */
    assert(ui_input_step(&in, BTN_A, BTN_A) == BTN_A);
    assert(ui_input_step(&in, 0u, BTN_A) == 0u);
    printf("  a non-repeating button acts once per hardware press\n");
}

static void test_held_button_does_not_cross_a_screen_change(void)
{
    UiInput in; reset(&in);

    /* Page down a list with R held. */
    assert(frame(&in, TRIG_R) == TRIG_R);
    for (unsigned f = 1; f < UI_REPEAT_DELAY; ++f) frame(&in, TRIG_R);
    assert(frame(&in, TRIG_R) == TRIG_R);       /* repeating happily */

    /* Press B to leave while R is still down. */
    const uint32_t both = TRIG_R | BTN_B;
    assert(ui_input_step(&in, BTN_B, both) == BTN_B);
    ui_input_screen_changed(&in);

    /* R is still physically down. On the screen just entered it means Save,
     * and it must do nothing at all until it is let go. */
    for (unsigned f = 0; f < UI_REPEAT_DELAY * 3u; ++f)
        assert(frame(&in, TRIG_R) == 0u);

    /* Released, then pressed again: now it is the new screen's press. */
    assert(frame(&in, 0u) == 0u);
    assert(frame(&in, TRIG_R) == TRIG_R);
    printf("  a button held across a screen change acts only after release\n");
}

static void test_screen_change_blocks_every_held_button(void)
{
    UiInput in; reset(&in);

    /* B held down through a transition must not immediately back out again. */
    assert(ui_input_step(&in, BTN_B, BTN_B) == BTN_B);
    ui_input_screen_changed(&in);
    for (unsigned f = 0; f < 30u; ++f)
        assert(ui_input_step(&in, 0u, BTN_B) == 0u);
    assert(frame(&in, 0u) == 0u);
    assert(ui_input_step(&in, BTN_B, BTN_B) == BTN_B);
    printf("  the button that caused the change cannot act twice\n");
}

static void test_one_release_does_not_unblock_another(void)
{
    UiInput in; reset(&in);

    assert(ui_input_step(&in, 0u, TRIG_L | TRIG_R) == (TRIG_L | TRIG_R));
    ui_input_screen_changed(&in);

    /* Letting go of L must not hand R to the new screen. */
    for (unsigned f = 0; f < UI_REPEAT_DELAY * 2u; ++f)
        assert(frame(&in, TRIG_R) == 0u);
    assert(frame(&in, 0u) == 0u);
    assert(frame(&in, TRIG_R) == TRIG_R);
    printf("  releasing one held button does not unblock the others\n");
}

/* ------------------------------------- stick and D-pad are separate ------ */

/*
 * The two used to set the same four bits, so the program could not tell them
 * apart and the D-pad was four inputs it could not use. Now the stick reports
 * as UI_STICK_* and is folded onto the D-pad's bits on the way out, with
 * UI_COARSE marking the frames the D-pad produced.
 */
static void reset_split(UiInput *in)
{
    memset(in, 0, sizeof(*in));
    in->repeatable = BTN_UP | BTN_DOWN | UI_STICK_ANY | TRIG_L | TRIG_R;
    in->dir_up = BTN_UP;
    in->dir_down = BTN_DOWN;
}

static void test_stick_reads_as_a_direction_without_coarse(void)
{
    UiInput in; reset_split(&in);
    const uint32_t got = frame(&in, UI_STICK_DOWN);
    assert(got & BTN_DOWN);              /* the handler sees a direction */
    assert(!(got & UI_COARSE));          /* the stick is the fine one */
    assert(!(got & UI_STICK_ANY));       /* and the stick's own bits never escape */
    printf("  the stick reads as a plain direction, one step\n");
}

static void test_dpad_reads_as_the_same_direction_but_coarse(void)
{
    UiInput in; reset_split(&in);
    const uint32_t got = frame(&in, BTN_DOWN);
    assert(got & BTN_DOWN);
    assert(got & UI_COARSE);
    printf("  the D-pad reads as the same direction, marked coarse\n");
}

static void test_the_two_repeat_on_their_own_clocks(void)
{
    /*
     * Holding the stick and tapping the D-pad has to be two presses. If they
     * shared a bit the tap would land inside the stick's repeat and vanish.
     */
    UiInput in; reset_split(&in);
    assert(frame(&in, UI_STICK_DOWN) & BTN_DOWN);
    assert(frame(&in, UI_STICK_DOWN) == 0u);

    const uint32_t got = frame(&in, UI_STICK_DOWN | BTN_DOWN);
    assert(got & BTN_DOWN);
    assert(got & UI_COARSE);
    printf("  a D-pad tap still lands while the stick is held\n");
}

static void test_a_screen_change_blocks_a_held_stick(void)
{
    UiInput in; reset_split(&in);
    assert(frame(&in, UI_STICK_DOWN) & BTN_DOWN);
    ui_input_screen_changed(&in);
    for (unsigned f = 0; f < UI_REPEAT_DELAY * 2u; ++f)
        assert(frame(&in, UI_STICK_DOWN) == 0u);
    assert(frame(&in, 0u) == 0u);
    assert(frame(&in, UI_STICK_DOWN) & BTN_DOWN);
    printf("  a held stick does not carry across a screen change either\n");
}

static void test_coarse_never_appears_without_a_direction(void)
{
    UiInput in; reset_split(&in);
    assert((frame(&in, TRIG_L) & UI_COARSE) == 0u);
    assert((frame(&in, 0u) & UI_COARSE) == 0u);
    printf("  a shoulder press is not a coarse step\n");
}

/*
 * The bug this guards: each stick axis was thresholded on its own, so a lean
 * down-and-slightly-right reported DOWN and RIGHT together. On the inventory
 * screen up/down move the cursor and left/right edit the value under it, so
 * scrolling a list rewrote the entries it passed.
 */
static void test_a_diagonal_stick_is_one_direction(void)
{
    /* Straight pushes still read as themselves. */
    assert(ui_stick_direction(0, 90) == UI_STICK_UP);
    assert(ui_stick_direction(0, -90) == UI_STICK_DOWN);
    assert(ui_stick_direction(-90, 0) == UI_STICK_LEFT);
    assert(ui_stick_direction(90, 0) == UI_STICK_RIGHT);
    printf("  a straight push reads as its own direction\n");

    /* The grip that caused the damage: mostly down, a little sideways. */
    assert(ui_stick_direction(45, -90) == UI_STICK_DOWN);
    assert(ui_stick_direction(-45, -90) == UI_STICK_DOWN);
    assert(ui_stick_direction(45, 90) == UI_STICK_UP);
    printf("  a lean off-axis is only the axis it leans along\n");

    /* Never two at once, over the whole square. */
    for (int x = -128; x < 128; ++x) {
        for (int y = -128; y < 128; ++y) {
            const uint32_t d = ui_stick_direction(x, y);
            assert((d & ~UI_STICK_ANY) == 0u);
            assert(d == 0u || d == UI_STICK_UP || d == UI_STICK_DOWN ||
                   d == UI_STICK_LEFT || d == UI_STICK_RIGHT);
        }
    }
    printf("  no stick position produces two directions at once\n");

    /* Centre and the dead zone stay silent. */
    assert(ui_stick_direction(0, 0) == 0u);
    assert(ui_stick_direction(39, 39) == 0u);
    assert(ui_stick_direction(-39, 39) == 0u);
    printf("  centre and the dead zone report nothing\n");
}

int main(void)
{
    test_press_and_release();
    test_repeat_timing();
    test_hardware_edges_pass_through();
    test_held_button_does_not_cross_a_screen_change();
    test_screen_change_blocks_every_held_button();
    test_one_release_does_not_unblock_another();
    test_stick_reads_as_a_direction_without_coarse();
    test_dpad_reads_as_the_same_direction_but_coarse();
    test_the_two_repeat_on_their_own_clocks();
    test_a_screen_change_blocks_a_held_stick();
    test_coarse_never_appears_without_a_direction();
    test_a_diagonal_stick_is_one_direction();
    printf("controller input tests: PASS\n");
    return 0;
}
