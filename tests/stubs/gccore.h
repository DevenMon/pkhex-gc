#ifndef GCCORE_H
#define GCCORE_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef int8_t s8;
typedef int32_t s32;
typedef uint32_t u32;
typedef uint8_t u8;
typedef uint16_t u16;
typedef float f32;
typedef struct GXColor { u8 r,g,b,a; } GXColor;
typedef struct GXTexObj { unsigned long _dummy[8]; } GXTexObj;
typedef f32 Mtx[3][4];
typedef f32 Mtx44[4][4];
typedef struct GXRModeObj {
    u32 viTVMode, fbWidth, efbHeight, xfbHeight, viXOrigin, viYOrigin, viWidth, viHeight;
    u32 xfbMode; bool field_rendering, aa; u8 sample_pattern[12][2]; u8 vfilter[7];
} GXRModeObj;
#define FALSE 0
#define TRUE 1
#define VI_DISPLAY_PIX_SZ 2
#define VI_INTERLACE 0
#define VI_NON_INTERLACE 1
#define VI_PROGRESSIVE 2
#define PAD_BUTTON_UP      0x0001u
#define PAD_BUTTON_DOWN    0x0002u
#define PAD_BUTTON_LEFT    0x0004u
#define PAD_BUTTON_RIGHT   0x0008u
#define PAD_BUTTON_A       0x0010u
#define PAD_BUTTON_B       0x0020u
#define PAD_BUTTON_X       0x0040u
#define PAD_BUTTON_START   0x0080u
#define PAD_TRIGGER_Z      0x0010u
#define PAD_BUTTON_Y       0x0400u
#define PAD_TRIGGER_L      0x0100u
#define PAD_TRIGGER_R      0x0200u
#define MEM_K0_TO_K1(x) (x)
#define ATTRIBUTE_ALIGN(v) __attribute__((aligned(v)))
#define GX_TRUE 1
#define GX_FALSE 0
#define GX_ENABLE 1
#define GX_DISABLE 0
#define GX_CULL_NONE 0
#define GX_VA_POS 9
#define GX_VA_CLR0 11
#define GX_VA_TEX0 13
#define GX_DIRECT 1
#define GX_VTXFMT0 0
#define GX_POS_XYZ 1
#define GX_POS_XY 0
#define GX_F32 4
#define GX_CLR_RGBA 0
#define GX_RGBA8 5
#define GX_TEX_ST 1
#define GX_COLOR0A0 0
#define GX_COLORNULL 0xff
#define GX_SRC_VTX 1
#define GX_DF_NONE 0
#define GX_AF_NONE 0
#define GX_TEVSTAGE0 0
#define GX_TEXCOORDNULL 0xff
#define GX_TEXMAP_NULL 0xff
#define GX_TEXCOORD0 0
#define GX_TEXMAP0 0
#define GX_TG_MTX2x4 1
#define GX_TG_TEX0 4
#define GX_IDENTITY 60
#define GX_PASSCLR 2
#define GX_REPLACE 3
#define GX_MODULATE 4
#define GX_BM_BLEND 1
#define GX_BL_SRCALPHA 4
#define GX_BL_INVSRCALPHA 5
#define GX_LO_CLEAR 0
#define GX_LEQUAL 3
#define GX_ORTHOGRAPHIC 1
#define GX_PNMTX0 0
#define GX_GM_1_0 0
#define GX_QUADS 0x80
#define GX_TRIANGLEFAN 0xA0
#define GX_PF_RGB565_Z16 2
#define GX_PF_RGB8_Z24 0
#define GX_ZC_LINEAR 0
#define GX_TF_RGB5A3 5
#define GX_CLAMP 0
#define GX_NEAR 0
#define GX_LINEAR 1
#define GX_ANISO_1 0
#define COLOR_BLACK 0
#define SI_CHAN0 0
#define SI_CHAN1 1
#define SI_MAX_CHAN 4
#define SI_ERROR_NO_RESPONSE 0x0008
#define SI_GBA 0x00040000u
extern GXRModeObj TVNtsc480Prog;
extern GXRModeObj TVNtsc480IntDf;
void VIDEO_Init(void);
void PAD_Init(void);
GXRModeObj *VIDEO_GetPreferredMode(void *);
void *SYS_AllocateFramebuffer(GXRModeObj *);
void VIDEO_Configure(GXRModeObj *);
void VIDEO_ClearFrameBuffer(GXRModeObj *, void *, u32);
void VIDEO_SetNextFramebuffer(void *);
void VIDEO_SetBlack(int);
void VIDEO_Flush(void);
void VIDEO_WaitVSync(void);
u32 VIDEO_GetNextField(void);
bool SYS_MainLoop(void);
void PAD_ScanPads(void);
u32 PAD_ButtonsDown(int);
u32 PAD_ButtonsHeld(int);
s8 PAD_StickX(int);
s8 PAD_StickY(int);
s8 PAD_SubStickX(int);
s8 PAD_SubStickY(int);
void *GX_Init(void *, u32);
void GX_SetCopyClear(GXColor,u32);
void GX_SetViewport(f32,f32,f32,f32,f32,f32);
f32 GX_GetYScaleFactor(u16,u16);
u32 GX_SetDispCopyYScale(f32);
void GX_SetScissor(u32,u32,u32,u32);
void GX_SetDispCopySrc(u16,u16,u16,u16);
void GX_SetDispCopyDst(u16,u16);
void GX_SetCopyFilter(u8,u8 [12][2],u8,u8 [7]);
void GX_SetFieldMode(u8,u8);
void GX_SetPixelFmt(u8,u8);
void GX_SetCullMode(u8);
void GX_SetDispCopyGamma(u8);
void GX_ClearVtxDesc(void);
void GX_SetVtxDesc(u32,u32);
void GX_SetVtxAttrFmt(u8,u32,u32,u32,u8);
void GX_SetNumChans(u8);
void GX_SetChanCtrl(u8,u8,u8,u8,u32,u8,u8);
void GX_SetNumTexGens(u32);
void GX_SetNumTevStages(u8);
void GX_SetTevOrder(u8,u8,u32,u8);
void GX_SetTevOp(u8,u8);
void GX_SetTexCoordGen(u16,u32,u32,u32);
void GX_LoadTexObj(GXTexObj *,u8);
void GX_InitTexObj(GXTexObj *,void *,u16,u16,u32,u8,u8,u8);
void GX_InitTexObjLOD(GXTexObj *,u8,u8,f32,f32,f32,u8,u8,u8);
void GX_InvalidateTexAll(void);
void GX_SetBlendMode(u8,u8,u8,u8);
void GX_SetZMode(u8,u8,u8);
void GX_SetColorUpdate(u8);
void GX_SetAlphaUpdate(u8);
void guOrtho(Mtx44,f32,f32,f32,f32,f32,f32);
void GX_LoadProjectionMtx(Mtx44,u8);
void guMtxIdentity(Mtx);
void guMtxTransApply(Mtx,Mtx,f32,f32,f32);
void GX_LoadPosMtxImm(Mtx,u32);
void GX_SetCurrentMtx(u32);
void GX_InvVtxCache(void);
void GX_Begin(u8,u8,u16);
void GX_Position3f32(f32,f32,f32);
void GX_Position2f32(f32,f32);
void GX_TexCoord2f32(f32,f32);
void GX_Color4u8(u8,u8,u8,u8);
void GX_End(void);
void GX_DrawDone(void);
void GX_CopyDisp(void *,u8);
void DCFlushRange(void *,u32);
void DCInvalidateRange(void *,u32);
typedef void (*SICallback)(s32,u32);
typedef void (*SITypeCallback)(s32,u32);
u32 SI_Transfer(s32,void*,u32,void*,u32,SICallback,u32);
u32 SI_GetTypeAsync(s32,SITypeCallback);
u32 SI_GetType(s32);
#endif
