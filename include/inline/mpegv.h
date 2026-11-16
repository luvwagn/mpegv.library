/* Written in the form sfdc 1.13 makes from mpegv_lib.sfd. MIT, (c) 2026 Brian King. */

#ifndef _INLINE_MPEGV_H
#define _INLINE_MPEGV_H

#ifndef _SFDC_VARARG_DEFINED
#define _SFDC_VARARG_DEFINED
#ifdef __HAVE_IPTR_ATTR__
typedef APTR _sfdc_vararg __attribute__((iptr));
#else
typedef ULONG _sfdc_vararg;
#endif /* __HAVE_IPTR_ATTR__ */
#endif /* _SFDC_VARARG_DEFINED */

#ifndef __INLINE_MACROS_H
#include <inline/macros.h>
#endif /* !__INLINE_MACROS_H */

#ifndef MPEGV_BASE_NAME
#define MPEGV_BASE_NAME MpegvBase
#endif /* !MPEGV_BASE_NAME */

#define MpvOpen(___tags) \
      LP1(0x1e, APTR, MpvOpen , struct TagItem *, ___tags, a0,\
      , MPEGV_BASE_NAME)

#ifndef NO_INLINE_STDARG
#define MpvOpenTags(___tag1, ...) \
    ({_sfdc_vararg _tags[] = { ___tag1, __VA_ARGS__ }; MpvOpen((struct TagItem *) _tags); })
#endif /* !NO_INLINE_STDARG */

#define MpvClose(___mpv) \
      LP1NR(0x24, MpvClose , APTR, ___mpv, a0,\
      , MPEGV_BASE_NAME)

#define MpvWrite(___mpv, ___buf, ___len) \
      LP3(0x2a, LONG, MpvWrite , APTR, ___mpv, a0, CONST UBYTE *, ___buf, a1, LONG, ___len, d0,\
      , MPEGV_BASE_NAME)

#define MpvPump(___mpv) \
      LP1(0x30, LONG, MpvPump , APTR, ___mpv, a0,\
      , MPEGV_BASE_NAME)

#define MpvDecode(___mpv, ___error) \
      LP2(0x36, struct MpvPicture *, MpvDecode , APTR, ___mpv, a0, LONG *, ___error, a1,\
      , MPEGV_BASE_NAME)

#define MpvGetInfo(___mpv, ___info) \
      LP2(0x3c, LONG, MpvGetInfo , APTR, ___mpv, a0, struct MpvInfo *, ___info, a1,\
      , MPEGV_BASE_NAME)

#define MpvControl(___mpv, ___cmd, ___value) \
      LP3(0x42, LONG, MpvControl , APTR, ___mpv, a0, ULONG, ___cmd, d0, ULONG, ___value, d1,\
      , MPEGV_BASE_NAME)

#define MpvToYUV422(___pic, ___dst, ___modulo, ___order) \
      LP4NR(0x48, MpvToYUV422 , struct MpvPicture *, ___pic, a0, UBYTE *, ___dst, a1, ULONG, ___modulo, d0, ULONG, ___order, d1,\
      , MPEGV_BASE_NAME)

#define MpvToRGB(___pic, ___dst, ___modulo, ___format) \
      LP4NR(0x4e, MpvToRGB , struct MpvPicture *, ___pic, a0, UBYTE *, ___dst, a1, ULONG, ___modulo, d0, ULONG, ___format, d1,\
      , MPEGV_BASE_NAME)

#define MpvToGrey(___pic, ___dst, ___modulo, ___levels) \
      LP4NR(0x54, MpvToGrey , struct MpvPicture *, ___pic, a0, UBYTE *, ___dst, a1, ULONG, ___modulo, d0, ULONG, ___levels, d1,\
      , MPEGV_BASE_NAME)

#define MpvVersion() \
      LP0(0x5a, CONST_STRPTR, MpvVersion ,\
      , MPEGV_BASE_NAME)

#define MpvExamine(___mpv, ___info, ___size) \
      LP3(0x60, LONG, MpvExamine , APTR, ___mpv, a0, struct MpvInfo *, ___info, a1, ULONG, ___size, d0,\
      , MPEGV_BASE_NAME)

#define MpvToRGBScaled(___pic, ___dst, ___modulo, ___format, ___width, ___height) \
      LP6NR(0x66, MpvToRGBScaled , struct MpvPicture *, ___pic, a0, UBYTE *, ___dst, a1, ULONG, ___modulo, d0, ULONG, ___format, d1, ULONG, ___width, d2, ULONG, ___height, d3,\
      , MPEGV_BASE_NAME)

#endif /* !_INLINE_MPEGV_H */
