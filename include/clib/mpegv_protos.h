/* Written in the form sfdc 1.13 makes from mpegv_lib.sfd. MIT, (c) 2026 Brian King. */

#ifndef CLIB_MPEGV_PROTOS_H
#define CLIB_MPEGV_PROTOS_H

#include <exec/types.h>
#include <utility/tagitem.h>
#include <libraries/mpegv.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

APTR MpvOpen(struct TagItem *tags);
APTR MpvOpenTags(Tag tag1, ...);
VOID MpvClose(APTR mpv);
LONG MpvWrite(APTR mpv, CONST UBYTE *buf, LONG len);
LONG MpvPump(APTR mpv);
struct MpvPicture *MpvDecode(APTR mpv, LONG *error);
LONG MpvGetInfo(APTR mpv, struct MpvInfo *info);
LONG MpvControl(APTR mpv, ULONG cmd, ULONG value);
VOID MpvToYUV422(struct MpvPicture *pic, UBYTE *dst, ULONG modulo, ULONG order);
VOID MpvToRGB(struct MpvPicture *pic, UBYTE *dst, ULONG modulo, ULONG format);
VOID MpvToGrey(struct MpvPicture *pic, UBYTE *dst, ULONG modulo, ULONG levels);
CONST_STRPTR MpvVersion(void);
/* V2 */
LONG MpvExamine(APTR mpv, struct MpvInfo *info, ULONG size);
/* V2.3 */
VOID MpvToRGBScaled(struct MpvPicture *pic, UBYTE *dst, ULONG modulo, ULONG format, ULONG width,
                    ULONG height);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* CLIB_MPEGV_PROTOS_H */
