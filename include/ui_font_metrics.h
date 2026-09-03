#ifndef PKHEX_GC_UI_FONT_METRICS_H
#define PKHEX_GC_UI_FONT_METRICS_H
#include <stdint.h>
#define UI_FONT_TEX_W 512u
#define UI_FONT_TEX_H 256u
#define UI_FONT_CELL 32u
#define UI_FONT_COLS 16u
#define UI_FONT_FIRST 32u
#define UI_FONT_LAST 126u
#define UI_FONT_BASELINE 23.0f
#define UI_FONT_LINE_HEIGHT 22.0f
static const uint8_t ui_font_advance[95] = {
    5, 5, 9, 12, 12, 17, 12, 6, 7, 7, 10, 12, 5, 9, 5, 7,
    12, 8, 11, 12, 12, 12, 12, 10, 12, 12, 5, 5, 12, 12, 12, 11,
    19, 13, 13, 14, 13, 12, 11, 14, 14, 5, 11, 13, 11, 17, 14, 14,
    12, 14, 12, 12, 12, 14, 13, 18, 13, 13, 12, 7, 7, 7, 9, 9,
    6, 11, 11, 11, 11, 11, 7, 11, 11, 5, 5, 10, 5, 16, 11, 11,
    11, 11, 7, 10, 7, 11, 10, 15, 10, 10, 10, 8, 7, 8, 12,
};
#endif
