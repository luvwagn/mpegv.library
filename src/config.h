/*
 * mpegv.library: libmpeg2's config.h, written by hand (libmpeg2 is built
 * here without its configure script): its C code only, no architecture's
 * accelerations, big-endian for the 68k (and whatever the build machine
 * is for the tests).
 *
 * (c) 2026 Brian King. GPL-3.0-or-later (LICENSE).
 */
#define HAVE_STDINT_H 1
#define HAVE_INTTYPES_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRING_H 1
#define STDC_HEADERS 1
#define HAVE_BUILTIN_EXPECT 1
#define ATTRIBUTE_ALIGNED_MAX 8          /* (the Amiga's hunks: 8 at most) */
#define PACKAGE "libmpeg2"
#define VERSION "0.5.1"
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#undef WORDS_BIGENDIAN
#else
#define WORDS_BIGENDIAN 1
#endif
/* mpegv.library's own memory functions for libmpeg2 (src/mpegv.c) */
#define MPEGV 1
