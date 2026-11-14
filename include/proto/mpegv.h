/* Written in the form sfdc 1.13 makes from mpegv_lib.sfd. MIT, (c) 2026 Brian King. */

#ifndef PROTO_MPEGV_H
#define PROTO_MPEGV_H

#include <clib/mpegv_protos.h>

#if defined(_CONST_BASES)
# ifndef __CONSTLIBBASEDECL__
# define __CONSTLIBBASEDECL__ const
# endif /* __CONSTLIBBASEDECL__ */
# ifndef __SEGMENTLIBBASEDECL__
# define __SEGMENTLIBBASEDECL__  __attribute__((__section__(".data")))
# endif /* __SEGMENTLIBBASEDECL__ */
#endif /* _CONST_BASES */
#ifndef __NOLIBBASE__
  extern struct Library *
# ifdef __CONSTLIBBASEDECL__
   __CONSTLIBBASEDECL__
# endif /* __CONSTLIBBASEDECL__ */
  MpegvBase
# ifdef __SEGMENTLIBBASEDECL__
 __SEGMENTLIBBASEDECL__
# endif /* __SEGMENTLIBBASEDECL__ */
;
#endif /* !__NOLIBBASE__ */

#ifndef _NO_INLINE
# if defined(__GNUC__)
#  include <inline/mpegv.h>
# else
#  include <pragmas/mpegv_pragmas.h>
# endif
#endif /* _NO_INLINE */

#endif /* !PROTO_MPEGV_H */
