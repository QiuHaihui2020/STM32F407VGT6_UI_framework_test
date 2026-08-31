#ifndef __UI_SDFS_H__
#define __UI_SDFS_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "jl_typedef.h"
#include "jl_fs.h"

#define  SD_SEEK_SET    0x00
#define  SD_SEEK_CUR 	0x01

RESFILE *font_sd_fopen(const char *filename, void *arg);
int font_sd_fread(RESFILE *fp, void *buf, u32 len);
int font_sd_fseek(RESFILE *fp, u8 seek_mode, u32 offset);
int font_sd_fclose(RESFILE *fp);

#ifdef __cplusplus
}
#endif

#endif
