#ifndef PKHEX_GC_GUI_H
#define PKHEX_GC_GUI_H

#include <gccore.h>
#include <stdbool.h>
#include <stdint.h>

/* The UI is authored in a fixed 640x480 coordinate system. v0.4 deliberately
 * uses the standard NTSC 480p render mode so GX, EFB and XFB are all 640x480. */
#define GUI_W 640.0f
#define GUI_H 480.0f

typedef enum GuiPipeline {
    GUI_PIPE_NONE = 0,
    GUI_PIPE_COLOR,
    GUI_PIPE_TEXTURE,
    GUI_PIPE_FONT,
} GuiPipeline;


typedef enum GuiButtonIcon {
    GUI_BTN_A = 0, GUI_BTN_B, GUI_BTN_X, GUI_BTN_Y, GUI_BTN_Z,
    GUI_BTN_L, GUI_BTN_R, GUI_BTN_START, GUI_BTN_DPAD, GUI_BTN_CSTICK,
    GUI_BTN_STICK
} GuiButtonIcon;

typedef struct GuiContext {
    void *xfb[2];
    GXRModeObj *rmode;
    void *fifo;
    unsigned fb;
    bool first_frame;
    GuiPipeline pipeline;
    GXTexObj pokemon_tex;
    bool pokemon_tex_ready;
    GXTexObj font_tex;
    bool font_tex_ready;
} GuiContext;

bool gui_init(GuiContext *g);
void gui_begin(GuiContext *g);
void gui_end(GuiContext *g);
void gui_shutdown(GuiContext *g);

void gui_rect(float x, float y, float w, float h, GXColor color);
void gui_outline(float x, float y, float w, float h, float thickness, GXColor color);
void gui_text(float x, float y, float scale, GXColor color, const char *text);
void gui_textf(float x, float y, float scale, GXColor color, const char *fmt, ...);
void gui_panel(float x, float y, float w, float h, GXColor fill, GXColor border);
void gui_badge(float x, float y, const char *text, GXColor fill, GXColor text_color);
float gui_text_width(const char *text, float scale);

/* GameCube controller prompt glyphs. Control strings may contain tokens such
 * as [A], [B], [X], [Y], [Z], [L], [R], [START], [DPAD], and [CSTICK]. */
float gui_button_icon(float x, float y, float scale, GuiButtonIcon icon);
float gui_controls_width(const char *text, float scale);
void gui_controls_text(float x, float y, float scale, GXColor color, const char *text);

/* Draw the embedded PokéSprite icon for National Dex 1..386. */
void gui_pokemon_sprite(float x, float y, float w, float h, unsigned national_species);

/* Capture the most recently presented 640x480 XFB as an RGB PNG. */
bool gui_screenshot_png(GuiContext *g, const char *path);

#endif
