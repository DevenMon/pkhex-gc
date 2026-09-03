#ifndef OGC_CARD_H
#define OGC_CARD_H
#include <gccore.h>
#include <stdbool.h>
#define CARD_SLOTA 0
#define CARD_SLOTB 1
#define CARD_WORKAREA_SIZE (5*8*1024)
#define CARD_ERROR_READY 0
#define CARD_FILENAMELEN 32
typedef struct _card_file { s32 chn; s32 filenum; s32 offset; s32 len; u16 iblock; } card_file;
typedef struct _card_dir { s32 chn; u32 fileno; u32 filelen; u8 permissions; char filename[CARD_FILENAMELEN]; u8 gamecode[4]; u8 company[2]; bool showall; } card_dir;
s32 CARD_Init(const char*,const char*);
s32 CARD_Mount(s32,void*,void*);
s32 CARD_Unmount(s32);
s32 CARD_FindFirst(s32,card_dir*,bool);
s32 CARD_FindNext(card_dir*);
s32 CARD_OpenEntry(s32,card_dir*,card_file*);
s32 CARD_Close(card_file*);
s32 CARD_Read(card_file*,void*,u32,u32);
s32 CARD_Write(card_file*,void*,u32,u32);
s32 CARD_GetSectorSize(s32,u32*);
#endif
