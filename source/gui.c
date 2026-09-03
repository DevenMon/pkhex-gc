#include <gccore.h>
#include <malloc.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gui.h"
#include "png_writer.h"
#include "ui_font_metrics.h"

#define FIFO_SIZE (256 * 1024)
#define SPRITE_TEX_W 1024
#define SPRITE_TEX_H 512
#define SPRITE_CELL   32
#define SPRITE_COLS   (SPRITE_TEX_W / SPRITE_CELL)

/* Produced by bin2o from data/gen3_sprites.bin. */
extern const unsigned char gen3_sprites_bin[];
extern const unsigned char ui_font_bin[];

static GuiContext *active_gui;

static void color_pipeline(void) {
    GuiContext *g = active_gui;
    if (!g || g->pipeline == GUI_PIPE_COLOR) return;
    GX_ClearVtxDesc();
    GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GX_SetNumChans(1);
    GX_SetChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_VTX, GX_SRC_VTX, 0, GX_DF_NONE, GX_AF_NONE);
    GX_SetNumTexGens(0);
    GX_SetNumTevStages(1);
    GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0);
    GX_SetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
    g->pipeline = GUI_PIPE_COLOR;
}

static void texture_pipeline(void) {
    GuiContext *g = active_gui;
    if (!g || !g->pokemon_tex_ready) return;
    if (g->pipeline != GUI_PIPE_TEXTURE) {
        GX_ClearVtxDesc();
        GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
        GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
        GX_SetNumChans(0);
        GX_SetNumTexGens(1);
        GX_SetNumTevStages(1);
        GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
        GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLORNULL);
        GX_SetTevOp(GX_TEVSTAGE0, GX_REPLACE);
        g->pipeline = GUI_PIPE_TEXTURE;
    }
    GX_LoadTexObj(&g->pokemon_tex, GX_TEXMAP0);
}


static void font_pipeline(void) {
    GuiContext *g = active_gui;
    if (!g || !g->font_tex_ready) return;
    if (g->pipeline != GUI_PIPE_FONT) {
        GX_ClearVtxDesc();
        GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
        GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
        GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
        GX_SetNumChans(1);
        GX_SetChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_VTX, GX_SRC_VTX, 0, GX_DF_NONE, GX_AF_NONE);
        GX_SetNumTexGens(1);
        GX_SetNumTevStages(1);
        GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
        GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
        GX_SetTevOp(GX_TEVSTAGE0, GX_MODULATE);
        g->pipeline = GUI_PIPE_FONT;
    }
    GX_LoadTexObj(&g->font_tex, GX_TEXMAP0);
}

static inline void color_vertex(float x, float y, GXColor c) {
    GX_Position2f32(x, y);
    GX_Color4u8(c.r, c.g, c.b, c.a);
}

bool gui_init(GuiContext *g) {
    if (!g) return false;
    memset(g, 0, sizeof(*g));
    active_gui = g;

    VIDEO_Init();
    PAD_Init();

    /* Fixed 480p by design. This avoids inheriting a 240p/480i EFB size from
     * a launcher and then drawing a 640x480 UI into a smaller framebuffer. */
    g->rmode = &TVNtsc480Prog;

    g->xfb[0] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(g->rmode));
    g->xfb[1] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(g->rmode));
    g->fifo = memalign(32, FIFO_SIZE);
    if (!g->xfb[0] || !g->xfb[1] || !g->fifo) return false;

    memset(g->fifo, 0, FIFO_SIZE);
    VIDEO_ClearFrameBuffer(g->rmode, g->xfb[0], COLOR_BLACK);
    VIDEO_ClearFrameBuffer(g->rmode, g->xfb[1], COLOR_BLACK);

    VIDEO_Configure(g->rmode);
    VIDEO_SetNextFramebuffer(g->xfb[0]);
    VIDEO_SetBlack(TRUE);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    if (g->rmode->viTVMode & VI_NON_INTERLACE) VIDEO_WaitVSync();

    GX_Init(g->fifo, FIFO_SIZE);
    GX_SetCopyClear((GXColor){216, 232, 246, 255}, 0x00ffffff);
    GX_SetViewport(0.0f, 0.0f, 640.0f, 480.0f, 0.0f, 1.0f);
    GX_SetScissor(0, 0, 640, 480);
    GX_SetDispCopySrc(0, 0, 640, 480);
    GX_SetDispCopyDst(640, 480);
    GX_SetCopyFilter(GX_FALSE, g->rmode->sample_pattern, GX_FALSE, g->rmode->vfilter);
    GX_SetFieldMode(GX_FALSE, GX_DISABLE);
    GX_SetPixelFmt(GX_PF_RGB8_Z24, GX_ZC_LINEAR);
    GX_SetCullMode(GX_CULL_NONE);
    GX_SetDispCopyGamma(GX_GM_1_0);
    GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
    GX_SetZMode(GX_FALSE, GX_LEQUAL, GX_FALSE);
    GX_SetColorUpdate(GX_TRUE);
    GX_SetAlphaUpdate(GX_TRUE);

    /* One vertex format supports both pipelines; descriptors select which
     * attributes are actually supplied for a draw. */
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XY, GX_F32, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);

    Mtx44 projection;
    guOrtho(projection, 0.0f, 479.0f, 0.0f, 639.0f, 0.0f, 300.0f);
    GX_LoadProjectionMtx(projection, GX_ORTHOGRAPHIC);
    Mtx model;
    guMtxIdentity(model);
    guMtxTransApply(model, model, 0.0f, 0.0f, -5.0f);
    GX_LoadPosMtxImm(model, GX_PNMTX0);
    GX_SetCurrentMtx(GX_PNMTX0);

    const unsigned expected = SPRITE_TEX_W * SPRITE_TEX_H * 2u;
    /* tools/build_gen3_sprites.py always emits exactly this many bytes.
     * Current devkitPPC/bin2s exposes the data symbol reliably, but the
     * historical <name>_size symbol is not emitted by every version.  Do not
     * depend on that optional linker symbol. */
    if (expected == 1048576u) {
        DCFlushRange((void *)gen3_sprites_bin, expected);
        GX_InitTexObj(&g->pokemon_tex, (void *)gen3_sprites_bin,
                      SPRITE_TEX_W, SPRITE_TEX_H, GX_TF_RGB5A3,
                      GX_CLAMP, GX_CLAMP, GX_FALSE);
        GX_InitTexObjLOD(&g->pokemon_tex, GX_NEAR, GX_NEAR,
                         0.0f, 0.0f, 0.0f, GX_FALSE, GX_FALSE, GX_ANISO_1);
        g->pokemon_tex_ready = true;
    }

    const unsigned font_expected = UI_FONT_TEX_W * UI_FONT_TEX_H * 2u;
    if (font_expected == 262144u) {
        DCFlushRange((void *)ui_font_bin, font_expected);
        GX_InitTexObj(&g->font_tex, (void *)ui_font_bin,
                      UI_FONT_TEX_W, UI_FONT_TEX_H, GX_TF_RGB5A3,
                      GX_CLAMP, GX_CLAMP, GX_FALSE);
        /* Linear filtering preserves the anti-aliased coverage generated into
         * the atlas, while the half-texel inset below prevents cell bleeding. */
        GX_InitTexObjLOD(&g->font_tex, GX_LINEAR, GX_LINEAR,
                         0.0f, 0.0f, 0.0f, GX_FALSE, GX_FALSE, GX_ANISO_1);
        g->font_tex_ready = true;
    }

    /* XFB 0 is the boot/blank buffer already handed to VI; draw the first
     * real frame into XFB 1 exactly like devkitPro's double-buffer example. */
    g->fb = 1;
    g->first_frame = true;
    g->pipeline = GUI_PIPE_NONE;

    /* Clear the EFB once before the first real frame. */
    GX_CopyDisp(g->xfb[0], GX_TRUE);
    GX_DrawDone();
    return true;
}

void gui_begin(GuiContext *g) {
    active_gui = g;
    g->pipeline = GUI_PIPE_NONE;
    GX_SetViewport(0.0f, 0.0f, 640.0f, 480.0f, 0.0f, 1.0f);
    GX_SetScissor(0, 0, 640, 480);
    GX_SetCopyClear((GXColor){216, 232, 246, 255}, 0x00ffffff);
    GX_InvVtxCache();
    GX_InvalidateTexAll();
}

void gui_end(GuiContext *g) {
    /* Finish all UI draws, then queue the EFB -> current XFB copy and hand
     * that buffer to VI. This matches devkitPro's current gxSprites loop. */
    GX_CopyDisp(g->xfb[g->fb], GX_TRUE);
    GX_DrawDone();
    VIDEO_SetNextFramebuffer(g->xfb[g->fb]);
    if (g->first_frame) {
        VIDEO_SetBlack(FALSE);
        g->first_frame = false;
    }
    VIDEO_Flush();
    VIDEO_WaitVSync();
    g->fb ^= 1u;
}

void gui_shutdown(GuiContext *g) {
    if (!g) return;
    free(g->fifo);
    g->fifo = NULL;
    if (active_gui == g) active_gui = NULL;
}

void gui_rect(float x, float y, float w, float h, GXColor c) {
    color_pipeline();
    GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
    color_vertex(x, y, c); color_vertex(x + w, y, c);
    color_vertex(x + w, y + h, c); color_vertex(x, y + h, c);
    GX_End();
}

void gui_outline(float x, float y, float w, float h, float t, GXColor c) {
    gui_rect(x, y, w, t, c);
    gui_rect(x, y + h - t, w, t, c);
    gui_rect(x, y + t, t, h - 2 * t, c);
    gui_rect(x + w - t, y + t, t, h - 2 * t, c);
}

void gui_panel(float x, float y, float w, float h, GXColor fill, GXColor border) {
    gui_rect(x, y, w, h, fill);
    gui_outline(x, y, w, h, 1.5f, border);
}

static unsigned font_glyph(unsigned char c) {
    if (c < UI_FONT_FIRST || c > UI_FONT_LAST) c = '?';
    return (unsigned)c - UI_FONT_FIRST;
}

static float glyph_advance(unsigned char c, float scale) {
    return (float)ui_font_advance[font_glyph(c)] * scale;
}

void gui_text(float x, float y, float scale, GXColor c, const char *text) {
    if (!text || !*text || scale <= 0.0f) return;
    GuiContext *g = active_gui;
    if (!g || !g->font_tex_ready) return;
    font_pipeline();

    const float ox = x;
    const float quad = (float)UI_FONT_CELL * scale;
    const float inset_u = 0.5f / (float)UI_FONT_TEX_W;
    const float inset_v = 0.5f / (float)UI_FONT_TEX_H;
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
        if (*p == '\n') {
            x = ox;
            y += UI_FONT_LINE_HEIGHT * scale;
            continue;
        }
        unsigned idx = font_glyph(*p);
        unsigned col = idx % UI_FONT_COLS;
        unsigned row = idx / UI_FONT_COLS;
        float u0 = (float)(col * UI_FONT_CELL) / (float)UI_FONT_TEX_W + inset_u;
        float v0 = (float)(row * UI_FONT_CELL) / (float)UI_FONT_TEX_H + inset_v;
        float u1 = (float)((col + 1u) * UI_FONT_CELL) / (float)UI_FONT_TEX_W - inset_u;
        float v1 = (float)((row + 1u) * UI_FONT_CELL) / (float)UI_FONT_TEX_H - inset_v;

        GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
        GX_Position2f32(x, y);               GX_Color4u8(c.r,c.g,c.b,c.a); GX_TexCoord2f32(u0,v0);
        GX_Position2f32(x + quad, y);        GX_Color4u8(c.r,c.g,c.b,c.a); GX_TexCoord2f32(u1,v0);
        GX_Position2f32(x + quad, y + quad); GX_Color4u8(c.r,c.g,c.b,c.a); GX_TexCoord2f32(u1,v1);
        GX_Position2f32(x, y + quad);        GX_Color4u8(c.r,c.g,c.b,c.a); GX_TexCoord2f32(u0,v1);
        GX_End();
        x += glyph_advance(*p, scale);
    }
}

void gui_textf(float x, float y, float scale, GXColor color, const char *fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    gui_text(x, y, scale, color, buf);
}

float gui_text_width(const char *text, float scale) {
    if (!text || scale <= 0.0f) return 0.0f;
    float max = 0.0f, cur = 0.0f;
    for (const unsigned char *p = (const unsigned char *)text;; ++p) {
        if (*p == '\n' || *p == '\0') {
            if (cur > max) max = cur;
            cur = 0.0f;
            if (!*p) break;
        } else {
            cur += glyph_advance(*p, scale);
        }
    }
    return max;
}


static void gui_disc(float cx, float cy, float rx, float ry, GXColor c) {
    static const float pts[12][2] = {
        {1.000f,0.000f},{0.866f,0.500f},{0.500f,0.866f},{0.000f,1.000f},
        {-0.500f,0.866f},{-0.866f,0.500f},{-1.000f,0.000f},{-0.866f,-0.500f},
        {-0.500f,-0.866f},{0.000f,-1.000f},{0.500f,-0.866f},{0.866f,-0.500f}
    };
    color_pipeline();
    GX_Begin(GX_TRIANGLEFAN, GX_VTXFMT0, 14);
    color_vertex(cx, cy, c);
    for (unsigned i = 0; i <= 12; ++i) {
        const unsigned j = i % 12u;
        color_vertex(cx + pts[j][0] * rx, cy + pts[j][1] * ry, c);
    }
    GX_End();
}

static void gui_capsule(float x, float y, float w, float h, GXColor c) {
    const float r = h * 0.5f;
    if (w <= h) { gui_disc(x + w*0.5f, y + h*0.5f, w*0.5f, h*0.5f, c); return; }
    gui_rect(x + r, y, w - 2.0f*r, h, c);
    gui_disc(x + r, y + r, r, r, c);
    gui_disc(x + w - r, y + r, r, r, c);
}

static bool control_token(const char *p, size_t *len, GuiButtonIcon *icon) {
    struct Tok { const char *s; GuiButtonIcon i; };
    static const struct Tok toks[] = {
        {"[START]",GUI_BTN_START},{"[DPAD]",GUI_BTN_DPAD},{"[CSTICK]",GUI_BTN_CSTICK},
        {"[A]",GUI_BTN_A},{"[B]",GUI_BTN_B},{"[X]",GUI_BTN_X},{"[Y]",GUI_BTN_Y},
        {"[Z]",GUI_BTN_Z},{"[L]",GUI_BTN_L},{"[R]",GUI_BTN_R},
        {"[STICK]",GUI_BTN_STICK}
    };
    for (unsigned i=0;i<sizeof(toks)/sizeof(toks[0]);++i) {
        size_t n=strlen(toks[i].s);
        if (!strncmp(p,toks[i].s,n)) { if(len)*len=n; if(icon)*icon=toks[i].i; return true; }
    }
    return false;
}

static float button_dims(float scale, GuiButtonIcon icon, float *h) {
    const float k = scale / 0.78f;
    float w=18.0f, hh=18.0f;
    if (icon==GUI_BTN_B) { w=16.0f; hh=16.0f; }
    else if (icon==GUI_BTN_X || icon==GUI_BTN_Y) { w=20.0f; hh=15.0f; }
    else if (icon==GUI_BTN_Z) { w=22.0f; hh=13.0f; }
    else if (icon==GUI_BTN_L || icon==GUI_BTN_R) { w=24.0f; hh=13.0f; }
    else if (icon==GUI_BTN_START) { w=34.0f; hh=13.0f; }
    else if (icon==GUI_BTN_DPAD) { w=19.0f; hh=19.0f; }
    else if (icon==GUI_BTN_CSTICK || icon==GUI_BTN_STICK) { w=22.0f; hh=22.0f; }
    if(h)*h=hh*k;
    return w*k;
}

float gui_button_icon(float x, float y, float scale, GuiButtonIcon icon) {
    static const GXColor green={44,173,88,255}, red={220,65,64,255};
    static const GXColor gray={205,211,220,255}, purple={122,78,171,255};
    static const GXColor dark={20,24,34,255};
    float h=0.0f,w=button_dims(scale,icon,&h), k=scale/0.78f;
    if(icon==GUI_BTN_DPAD) {
        const float t=7.0f*k, cx=x+w*0.5f, cy=y+h*0.5f;
        gui_rect(cx-t*0.5f,y,t,h,gray); gui_rect(x,cy-t*0.5f,w,t,gray);
        return w+4.0f*k;
    }
    if(icon==GUI_BTN_CSTICK || icon==GUI_BTN_STICK) {
        /* The two sticks differ only in colour on the real pad: the C-stick is
         * yellow, the control stick light grey. Same shape, so same drawing. */
        static const GXColor yellow={238,196,54,255};
        const GXColor cap = (icon==GUI_BTN_CSTICK) ? yellow : gray;
        const float stem=4.0f*k;
        gui_rect(x+w*0.5f-stem*0.5f,y+h*0.44f,stem,h*0.50f,dark);
        gui_disc(x+w*0.5f,y+h*0.38f,w*0.34f,h*0.30f,cap);
        return w+4.0f*k;
    }
    GXColor fill=gray;
    if(icon==GUI_BTN_A)fill=green; else if(icon==GUI_BTN_B)fill=red; else if(icon==GUI_BTN_Z)fill=purple;
    /* X, Y, L and R are light grey on the real pad, and the UI behind them is
     * light too, so they get an edge rather than melting into the panel. */
    if(icon==GUI_BTN_A || icon==GUI_BTN_B) {
        gui_disc(x+w*0.5f,y+h*0.5f,w*0.5f,h*0.5f,fill);
    } else {
        static const GXColor edge={120,132,150,255};
        gui_capsule(x-1.0f*k,y-1.0f*k,w+2.0f*k,h+2.0f*k,edge);
        gui_capsule(x,y,w,h,fill);
    }
    const char *label="";
    switch(icon){case GUI_BTN_A:label="A";break;case GUI_BTN_B:label="B";break;case GUI_BTN_X:label="X";break;case GUI_BTN_Y:label="Y";break;case GUI_BTN_Z:label="Z";break;case GUI_BTN_L:label="L";break;case GUI_BTN_R:label="R";break;case GUI_BTN_START:label="START";break;default:break;}
    if(*label){
        float fs=(icon==GUI_BTN_START?0.36f:0.48f)*k;
        float tw=gui_text_width(label,fs);
        gui_text(x+(w-tw)*0.5f,y+(h-UI_FONT_CELL*fs)*0.5f-1.0f*k,fs,dark,label);
    }
    return w+4.0f*k;
}

float gui_controls_width(const char *text, float scale) {
    if(!text) return 0.0f;
    float w=0.0f;
    for(const char*p=text;*p;){size_t n=0;GuiButtonIcon i;if(control_token(p,&n,&i)){w+=button_dims(scale,i,NULL)+4.0f*(scale/0.78f);p+=n;}else{char ch[2]={*p,0};w+=gui_text_width(ch,scale);++p;}}
    return w;
}

void gui_controls_text(float x, float y, float scale, GXColor color, const char *text) {
    if(!text)return;
    for(const char*p=text;*p;){size_t n=0;GuiButtonIcon i;if(control_token(p,&n,&i)){float h=0;button_dims(scale,i,&h);x+=gui_button_icon(x,y+(21.0f*(scale/0.78f)-h)*0.5f,scale,i);p+=n;}else{char ch[2]={*p,0};gui_text(x,y,scale,color,ch);x+=gui_text_width(ch,scale);++p;}}
}

void gui_badge(float x, float y, const char *text, GXColor fill, GXColor text_color) {
    const float scale = 0.72f;
    float w = gui_text_width(text, scale) + 12.0f;
    gui_rect(x, y, w, 21.0f, fill);
    gui_text(x + 6.0f, y, scale, text_color, text);
}

void gui_pokemon_sprite(float x, float y, float w, float h, unsigned national_species) {
    GuiContext *g = active_gui;
    if (!g || !g->pokemon_tex_ready || national_species < 1 || national_species > 386) return;
    texture_pipeline();

    unsigned idx = national_species - 1u;
    unsigned col = idx % SPRITE_COLS;
    unsigned row = idx / SPRITE_COLS;
    const float u0 = (float)(col * SPRITE_CELL) / (float)SPRITE_TEX_W;
    const float v0 = (float)(row * SPRITE_CELL) / (float)SPRITE_TEX_H;
    const float u1 = (float)(col * SPRITE_CELL + SPRITE_CELL) / (float)SPRITE_TEX_W;
    const float v1 = (float)(row * SPRITE_CELL + SPRITE_CELL) / (float)SPRITE_TEX_H;

    GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
    GX_Position2f32(x, y);         GX_TexCoord2f32(u0, v0);
    GX_Position2f32(x + w, y);     GX_TexCoord2f32(u1, v0);
    GX_Position2f32(x + w, y + h); GX_TexCoord2f32(u1, v1);
    GX_Position2f32(x, y + h);     GX_TexCoord2f32(u0, v1);
    GX_End();
}


static uint8_t clamp8(int v) {
    if (v < 0) return 0;
    if (v > 255) return 255;
    return (uint8_t)v;
}

static void yuv_to_rgb(uint8_t y, uint8_t u, uint8_t v, uint8_t *bgr) {
    int c = (int)y - 16;
    int d = (int)u - 128;
    int e = (int)v - 128;
    if (c < 0) c = 0;
    int r = (298*c + 409*e + 128) >> 8;
    int g = (298*c - 100*d - 208*e + 128) >> 8;
    int b = (298*c + 516*d + 128) >> 8;
    bgr[0] = clamp8(r);
    bgr[1] = clamp8(g);
    bgr[2] = clamp8(b);
}

bool gui_screenshot_png(GuiContext *g, const char *path) {
    if (!g || !path || !*path || !g->xfb[0] || !g->xfb[1]) return false;
    FILE *f = fopen(path, "wb");
    if (!f) return false;

    enum { W=640, H=480, ROW=W*3 };
    uint8_t *rgb = (uint8_t *)malloc((size_t)ROW * H);
    if (!rgb) { fclose(f); return false; }

    /* gui_end toggles fb after presenting, so fb^1 is the most recent XFB. */
    const uint8_t *xfb = (const uint8_t *)g->xfb[g->fb ^ 1u];
    for (int y=0; y<H; ++y) {
        const uint8_t *src = xfb + (size_t)y * W * 2u;
        uint8_t *row = rgb + (size_t)y * ROW;
        for (int x=0; x<W; x+=2) {
            uint8_t y0=src[0], u=src[1], y1=src[2], v=src[3]; src += 4;
            yuv_to_rgb(y0,u,v,row + x*3);
            yuv_to_rgb(y1,u,v,row + (x+1)*3);
        }
    }
    bool ok = png_write_rgb8(f, W, H, rgb, ROW);
    free(rgb);
    if (fclose(f) != 0) ok = false;
    return ok;
}
