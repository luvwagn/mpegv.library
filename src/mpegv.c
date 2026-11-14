/*
 * mpegv.library: MPEG-1 video decoded by the CPU, for any program.
 *
 * The decoder is pl_mpeg (third_party/pl_mpeg, MIT, Dominic Szablewski),
 * made faster for a 68020 without an FPU (each change marked "AmigaMCE:"
 * or "mpegv:" in pl_mpeg.h), built in here with the library's own memory
 * and no stdio. Its demuxer reads a Program Stream as the read hook gives
 * it (or MpvWrite), the video packets go to the video decoder, the others
 * to the packet hook; a bare video stream goes to the decoder as it is.
 * A picture is the decoder's own planes, not copied; the helpers make
 * them what a screen wants.
 *
 * Built for the Amiga as a library (src/libinit.c: libnix's, public
 * domain), and for the build machine as plain functions (the tests).
 *
 * (c) 2026 Brian King. MIT licence (LICENSE).
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <utility/tagitem.h>
#include <utility/hooks.h>
#include <string.h>
#include <stdint.h>
#include <libraries/mpegv.h>

#ifdef __amigaos__
#include <exec/libraries.h>
#include <proto/exec.h>
#include "stabs.h"
/* stabs' stubs call their function with a BSR (16 bits): the stubs come
 * first and pl_mpeg is more than 32 KB, so the call is a JSR (32 bits) */
#undef _ADDTABL_CALL
#define _ADDTABL_CALL(name)                 \
__asm__("\tmovel a6@(40:W),a4");             \
__asm__("\tjsr _" #name)

const char LibName[] = "mpegv.library";
const char LibIdString[] = "mpegv 1.0 (14.11.2026) pl_mpeg, MPEG-1 video";
static const char __attribute__((used)) LibVerTag[] =
    "$VER: mpegv.library 1.0 (14.11.2026) pl_mpeg, MPEG-1 video (68020+)";
const UWORD LibVersion = 1;
const UWORD LibRevision = 0;

struct ExecBase *SysBase;
#define API

/* pl_mpeg's memory: exec's, each block's size before it (for realloc) */
static void *mpv_malloc(size_t n)
{
    ULONG *b = AllocVec(n + 8, MEMF_ANY);
    if (!b)
        return NULL;
    b[0] = n;
    return b + 2;
}

static void mpv_free(void *p)
{
    if (p)
        FreeVec((ULONG *)p - 2);
}

static void *mpv_realloc(void *p, size_t n)
{
    void *q;
    size_t had;
    if (!p)
        return mpv_malloc(n);
    had = ((ULONG *)p)[-2];
    if (!(q = mpv_malloc(n)))
        return NULL;
    memcpy(q, p, had < n ? had : n);
    mpv_free(p);
    return q;
}

/* a hook called as the Amiga calls them: A0 the hook, A2 the object, A1
 * the message; D0 its answer */
static LONG call_hook(struct Hook *h, APTR obj, APTR msg)
{
    register LONG d0 __asm("d0");
    register struct Hook *a0 __asm("a0") = h;
    register APTR a1 __asm("a1") = msg;
    register APTR a2 __asm("a2") = obj;
    __asm volatile("movel %%a0@(8),%%a3\n\tjsr %%a3@"
                   : "=r"(d0), "+r"(a0), "+r"(a1)
                   : "r"(a2)
                   : "d1", "a3", "cc", "memory");
    return d0;
}
#else
#include <stdlib.h>
#define API                                 /* on the host: plain functions (tests) */
#define mpv_malloc malloc
#define mpv_free free
#define mpv_realloc realloc
static LONG call_hook(struct Hook *h, APTR obj, APTR msg)
{
    return (LONG)((ULONG(*)(struct Hook *, APTR, APTR))h->h_Entry)(h, obj, msg);
}
#endif

#define PLM_MALLOC(sz) mpv_malloc(sz)
#define PLM_FREE(p) mpv_free(p)
#define PLM_REALLOC(p, sz) mpv_realloc(p, sz)
#define PLM_NO_STDIO
#define PL_MPEG_IMPLEMENTATION
#include "pl_mpeg.h"

static const char version[] = "mpegv 1.0, pl_mpeg (MPEG-1 video)";

struct Mpv {
    struct Hook *read, *packet;
    ULONG readsize;
    UBYTE *in;                              /* the read hook's buffer */
    ULONG kind;                             /* MPVS_PROGRAM or MPVS_VIDEO; MPVS_AUTO: not yet known */
    plm_buffer_t *fbuf, *vbuf;              /* the stream as read; its video */
    plm_demux_t *demux;
    plm_video_t *vd;
    BOOL ended;                             /* the stream's end reached (or MPVC_END) */
    BOOL failed;                            /* the read hook could not read */
    BOOL pushed_end;
    BOOL skipb;
    BOOL bad;                               /* not a stream it decodes */
    int64_t first_pts;                      /* the first video packet's time, -1 none */
    struct MpvPicture pic;
};

/* ---- the stream's bytes to where they go: the first say what it is */
static void feed(struct Mpv *m, UBYTE *b, ULONG n)
{
    if (m->kind == MPVS_AUTO) {
        ULONG i = 0;
        while (i < n && !b[i])
            i++;
        if (i >= 2 && i < n && b[i] == 1 && i + 1 < n) {
            m->kind = b[i + 1] == 0xBA ? MPVS_PROGRAM : b[i + 1] == 0xB3 ? MPVS_VIDEO : MPVS_AUTO;
        }
        if (m->kind == MPVS_AUTO) {
            m->bad = TRUE;                  /* (neither a pack nor a sequence header first) */
            m->kind = MPVS_PROGRAM;
        }
    }
    if (m->kind == MPVS_PROGRAM) {
        if (!m->fbuf) {
            m->fbuf = plm_buffer_create_with_capacity(PLM_BUFFER_DEFAULT_SIZE);
            if (!m->fbuf)
                return;
        }
        plm_buffer_write(m->fbuf, b, n);
        if (!m->demux)
            m->demux = plm_demux_create(m->fbuf, FALSE);
    } else
        plm_buffer_write(m->vbuf, b, n);
}

/* The read hook's next bytes fed: FALSE at the end (or an error). */
static BOOL read_more(struct Mpv *m)
{
    struct MpvRead r;
    LONG n;
    if (!m->read || m->ended)
        return FALSE;
    r.mr_Buffer = m->in;
    r.mr_Length = (LONG)m->readsize;
    n = call_hook(m->read, m, &r);
    if (n <= 0) {
        if (n < 0)
            m->failed = TRUE;
        return FALSE;
    }
    feed(m, m->in, (ULONG)n);
    return TRUE;
}

/* the demuxer wants more of the stream */
static void fload(plm_buffer_t *b, void *user)
{
    struct Mpv *m = user;
    if (!read_more(m))
        plm_buffer_signal_end(b);
}

/* One packet from the Program Stream: video to the decoder, the rest to
 * the packet hook. FALSE at its end (or, without a read hook, when more
 * bytes are wanted). */
static BOOL pump(struct Mpv *m)
{
    plm_packet_t *p;
    if (m->ended || !m->demux)
        return FALSE;
    if (!(p = plm_demux_decode(m->demux))) {
        if (m->read || m->pushed_end) {
            m->ended = TRUE;
            plm_buffer_signal_end(m->vbuf);
        }
        return FALSE;
    }
    if (p->type == (int)PLM_DEMUX_PACKET_VIDEO_1) {
        if (m->first_pts < 0 && p->pts90 >= 0)
            m->first_pts = p->pts90;
        plm_buffer_write(m->vbuf, p->data, p->length);
    } else if (m->packet) {
        struct MpvPacket pk;
        pk.mp_Stream = (ULONG)p->type;
        pk.mp_Flags = p->pts90 >= 0 ? MPVPF_PTS : 0;
        pk.mp_PTS = p->pts90 >= 0 ? (ULONG)p->pts90 : 0;
        pk.mp_Data = p->data;
        pk.mp_Length = (ULONG)p->length;
        call_hook(m->packet, m, &pk);
    }
    return TRUE;
}

/* the video decoder wants more: packets until a video one (a bare
 * stream: the stream itself) */
static void vload(plm_buffer_t *b, void *user)
{
    struct Mpv *m = user;
    size_t had;
    if (m->kind == MPVS_AUTO && !read_more(m)) {
        if (m->read || m->pushed_end)
            plm_buffer_signal_end(b);
        return;
    }
    if (m->kind == MPVS_VIDEO) {
        if (m->read && !read_more(m)) {
            m->ended = TRUE;
            plm_buffer_signal_end(b);
        } else if (!m->read && m->pushed_end)
            plm_buffer_signal_end(b);
        return;
    }
    if (m->demux && m->fbuf && m->read)
        plm_buffer_set_load_callback(m->fbuf, fload, m);
    had = plm_buffer_get_remaining(b);
    while (plm_buffer_get_remaining(b) == had && pump(m))
        ;
}

static unsigned long tag_data(struct TagItem *t, ULONG tag, unsigned long def)
{
    while (t) {
        switch (t->ti_Tag) {
        case TAG_DONE:
            return def;
        case TAG_IGNORE:
            t++;
            continue;
        case TAG_MORE:
            t = (struct TagItem *)t->ti_Data;
            continue;
        case TAG_SKIP:
            t += 1 + t->ti_Data;
            continue;
        }
        if (t->ti_Tag == tag)
            return t->ti_Data;
        t++;
    }
    return def;
}

API VOID MpvClose(APTR mpv);

API APTR MpvOpen(struct TagItem *tags)
{
    struct Mpv *m = mpv_malloc(sizeof(*m));
    if (!m)
        return NULL;
    memset(m, 0, sizeof(*m));
    m->read = (struct Hook *)tag_data(tags, MPVA_ReadHook, 0);
    m->packet = (struct Hook *)tag_data(tags, MPVA_PacketHook, 0);
    m->kind = (ULONG)tag_data(tags, MPVA_Stream, MPVS_AUTO);
    m->readsize = (ULONG)tag_data(tags, MPVA_ReadSize, 16384);
    m->skipb = tag_data(tags, MPVA_SkipB, FALSE) != 0;
    m->first_pts = -1;
    if (m->kind > MPVS_VIDEO)
        m->kind = MPVS_AUTO;
    if (m->readsize < 2048)
        m->readsize = 2048;
    if ((m->read && !(m->in = mpv_malloc(m->readsize))) ||
        !(m->vbuf = plm_buffer_create_with_capacity(PLM_BUFFER_DEFAULT_SIZE)))
        goto fail;
    plm_buffer_set_load_callback(m->vbuf, vload, m);
    if (m->kind == MPVS_PROGRAM) {
        if (!(m->fbuf = plm_buffer_create_with_capacity(PLM_BUFFER_DEFAULT_SIZE)) ||
            !(m->demux = plm_demux_create(m->fbuf, FALSE)))
            goto fail;
        if (m->read)
            plm_buffer_set_load_callback(m->fbuf, fload, m);
    }
    if (!(m->vd = plm_video_create_with_buffer(m->vbuf, FALSE)))
        goto fail;
    plm_video_set_skip_b(m->vd, m->skipb);
    return m;
fail:
    MpvClose(m);
    return NULL;
}

API VOID MpvClose(APTR mpv)
{
    struct Mpv *m = mpv;
    if (!m)
        return;
    if (m->vd)
        plm_video_destroy(m->vd);
    if (m->demux)
        plm_demux_destroy(m->demux);
    if (m->fbuf)
        plm_buffer_destroy(m->fbuf);
    if (m->vbuf)
        plm_buffer_destroy(m->vbuf);
    if (m->in)
        mpv_free(m->in);
    mpv_free(m);
}

API LONG MpvWrite(APTR mpv, CONST UBYTE *buf, LONG len)
{
    struct Mpv *m = mpv;
    if (!m || !buf || len <= 0 || m->pushed_end)
        return 0;
    feed(m, (UBYTE *)buf, (ULONG)len);
    return len;
}

API LONG MpvPump(APTR mpv)
{
    struct Mpv *m = mpv;
    if (!m)
        return FALSE;
    if (m->kind == MPVS_AUTO)
        read_more(m);
    if (m->kind != MPVS_PROGRAM)
        return FALSE;
    if (m->fbuf && m->read)
        plm_buffer_set_load_callback(m->fbuf, fload, m);
    return pump(m);
}

/* pictures a second as num / den, from the rate's code */
static void rate_of(int code, ULONG *num, ULONG *den)
{
    static const UWORD n[9] = { 30, 24000, 24, 25, 30000, 30, 50, 60000, 60 };
    static const UWORD d[9] = { 1, 1001, 1, 1, 1001, 1, 1, 1001, 1 };
    if (code < 1 || code > 8)
        code = 0;
    *num = n[code];
    *den = d[code];
}

API struct MpvPicture *MpvDecode(APTR mpv, LONG *error)
{
    struct Mpv *m = mpv;
    plm_frame_t *f;
    LONG e = MPVE_OK;
    if (!m) {
        if (error)
            *error = MPVE_STREAM;
        return NULL;
    }
    f = plm_video_decode(m->vd);
    if (!f) {
        if (m->failed)
            e = MPVE_READ;
        else if (plm_video_has_ended(m->vd) || m->ended)
            e = MPVE_END;
        else if (m->bad && !plm_video_has_header(m->vd))
            e = MPVE_STREAM;
        else
            e = MPVE_MORE;
        if (error)
            *error = e;
        return NULL;
    }
    {
        struct MpvPicture *p = &m->pic;
        ULONG num, den;
        rate_of(m->vd->rate_code, &num, &den);
        p->mpic_Y = f->y.data;
        p->mpic_Cb = f->cb.data;
        p->mpic_Cr = f->cr.data;
        p->mpic_Width = (UWORD)f->width;
        p->mpic_Height = (UWORD)f->height;
        p->mpic_YModulo = (UWORD)f->y.width;
        p->mpic_CModulo = (UWORD)f->cb.width;
        p->mpic_YRows = (UWORD)f->y.height;
        p->mpic_Type = (UWORD)f->type;
        p->mpic_Number = f->index;
        p->mpic_Time = (ULONG)((uint64_t)f->index * 90000u * den / num);
        p->mpic_Flags = m->first_pts >= 0 ? MPVPF_PTS : 0;
        p->mpic_PTS = (ULONG)(m->first_pts >= 0 ? m->first_pts : 0) + p->mpic_Time;
    }
    if (error)
        *error = MPVE_OK;
    return &m->pic;
}

/* a picture's width for its height, as it is meant to be seen (its pixel
 * shape's code, MPEG-1's) */
static ULONG shown_w(ULONG w, ULONG h, int a)
{
    static const UWORD pel[15] = { 0, 10000, 6735, 7031, 7615, 8055, 8437, 8935,
                                   9375, 9815, 10255, 10695, 10950, 11575, 12015 };
    if (a == 3 || a == 6)
        return h * 16 / 9;                  /* (16:9, 625 and 525 lines) */
    if (a == 8 || a == 12)
        return h * 4 / 3;                   /* (CCIR 601: a 4:3 picture) */
    if (a >= 1 && a < 15)
        return w * 10000 / pel[a];
    return w;
}

API LONG MpvGetInfo(APTR mpv, struct MpvInfo *info)
{
    struct Mpv *m = mpv;
    plm_video_t *v;
    if (!m || !info || !plm_video_has_header(m->vd))
        return FALSE;
    v = m->vd;
    memset(info, 0, sizeof(*info));
    info->mi_Width = (UWORD)v->width;
    info->mi_Height = (UWORD)v->height;
    info->mi_RateCode = (UWORD)v->rate_code;
    info->mi_AspectCode = (UWORD)v->aspect_code;
    rate_of(v->rate_code, &info->mi_RateNum, &info->mi_RateDen);
    info->mi_Period = 90000UL * info->mi_RateDen / info->mi_RateNum;
    info->mi_ShownWidth = shown_w((ULONG)v->width, (ULONG)v->height, v->aspect_code);
    info->mi_BitRate = v->bit_rate == 0x3FFFF ? 0 : (ULONG)v->bit_rate * 400;
    info->mi_Kind = m->kind == MPVS_VIDEO ? MPVS_VIDEO : MPVS_PROGRAM;
    return TRUE;
}

API LONG MpvControl(APTR mpv, ULONG cmd, ULONG value)
{
    struct Mpv *m = mpv;
    LONG r = 0;
    if (!m)
        return 0;
    switch (cmd) {
    case MPVC_SKIPB:
        r = m->skipb;
        m->skipb = value != 0;
        plm_video_set_skip_b(m->vd, m->skipb);
        break;
    case MPVC_SKIPPEDB:
        r = (LONG)plm_video_get_skipped_b(m->vd);
        break;
    case MPVC_ENDED:
        r = m->ended || plm_video_has_ended(m->vd);
        break;
    case MPVC_FIRSTPTS:
        r = m->first_pts >= 0 ? (LONG)(ULONG)m->first_pts : -1;
        break;
    case MPVC_END:
        m->pushed_end = TRUE;
        if (m->fbuf)
            plm_buffer_signal_end(m->fbuf);
        if (m->kind != MPVS_PROGRAM)
            plm_buffer_signal_end(m->vbuf);
        break;
    case MPVC_HELD:
        r = (LONG)((m->fbuf ? plm_buffer_get_remaining(m->fbuf) : 0) +
                   plm_buffer_get_remaining(m->vbuf));
        break;
    }
    return r;
}

/* ---- the helpers: a picture made what a screen wants */

/* (the 68k's order in memory: big-endian; the tests on this computer
 * store the same bytes a byte at a time) */
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define MPV_BIG 0
static inline void put32(ULONG *d, ULONG v)
{
    UBYTE *b = (UBYTE *)d;
    b[0] = (UBYTE)(v >> 24);
    b[1] = (UBYTE)(v >> 16);
    b[2] = (UBYTE)(v >> 8);
    b[3] = (UBYTE)v;
}
static inline void put16(UBYTE *b, UWORD v)
{
    b[0] = (UBYTE)(v >> 8);
    b[1] = (UBYTE)v;
}
#else
#define MPV_BIG 1
#define put32(d, v) (*(d) = (v))
#define put16(b, v) (*(UWORD *)(b) = (v))
#endif

/* pl_mpeg's frame for a picture (its conversions take one) */
static void as_frame(const struct MpvPicture *p, plm_frame_t *f)
{
    memset(f, 0, sizeof(*f));
    f->width = p->mpic_Width;
    f->height = p->mpic_Height;
    f->y.width = p->mpic_YModulo;
    f->y.height = p->mpic_YRows;
    f->y.data = p->mpic_Y;
    f->cb.width = f->cr.width = p->mpic_CModulo;
    f->cb.height = f->cr.height = p->mpic_YRows / 2;
    f->cb.data = p->mpic_Cb;
    f->cr.data = p->mpic_Cr;
}

API VOID MpvToYUV422(struct MpvPicture *pic, UBYTE *dst, ULONG modulo, ULONG order)
{
    ULONG r, x, n;
    if (!pic || !dst)
        return;
    n = pic->mpic_Width / 2;
    if (order == MPVY_Y0UY1V) {
        /* a longword at a time (a graphics card's memory, across the bus) */
        for (r = 0; r < pic->mpic_Height; r++) {
            const UWORD *y = (const UWORD *)(pic->mpic_Y + r * pic->mpic_YModulo);
            const UBYTE *cb = pic->mpic_Cb + (r >> 1) * pic->mpic_CModulo;
            const UBYTE *cr = pic->mpic_Cr + (r >> 1) * pic->mpic_CModulo;
            ULONG *d = (ULONG *)(dst + r * modulo);
            for (x = 0; x < n; x++) {
#if MPV_BIG
                ULONG yy = *y++;
                *d++ = ((yy & 0xFF00) << 16) | ((ULONG)*cb++ << 16) | ((yy & 0xFF) << 8) | *cr++;
#else
                const UBYTE *yb = (const UBYTE *)y++;
                put32(d++, ((ULONG)yb[0] << 24) | ((ULONG)*cb++ << 16) | ((ULONG)yb[1] << 8) | *cr++);
#endif
            }
        }
        return;
    }
    {
        /* the others: each byte's place (from the top) */
        static const UBYTE at[4][4] = { /* y0, cb, y1, cr */
            { 24, 16, 8, 0 }, { 16, 24, 0, 8 }, { 8, 16, 24, 0 }, { 24, 0, 8, 16 } };
        const UBYTE *s = at[order & 3];
        for (r = 0; r < pic->mpic_Height; r++) {
            const UBYTE *y = pic->mpic_Y + r * pic->mpic_YModulo;
            const UBYTE *cb = pic->mpic_Cb + (r >> 1) * pic->mpic_CModulo;
            const UBYTE *cr = pic->mpic_Cr + (r >> 1) * pic->mpic_CModulo;
            ULONG *d = (ULONG *)(dst + r * modulo);
            for (x = 0; x < n; x++, y += 2)
                put32(d++, ((ULONG)y[0] << s[0]) | ((ULONG)*cb++ << s[1]) | ((ULONG)y[1] << s[2]) |
                               ((ULONG)*cr++ << s[3]));
        }
    }
}

/* 16 bits a pixel, packed from pl_mpeg's own conversion's sums */
#define MPV_PUT16(Y_OFFSET, DEST_OFFSET) \
    y = ((frame->y.data[y_index + Y_OFFSET] - 16) * 76309) >> 16; \
    rr = plm_clamp(y + r); \
    gg = plm_clamp(y - g); \
    bb = plm_clamp(y + b); \
    v = fmt == MPVR_RGB555 ? (UWORD)(((rr & 0xF8) << 7) | ((gg & 0xF8) << 2) | (bb >> 3)) \
                           : (UWORD)(((rr & 0xF8) << 8) | ((gg & 0xFC) << 3) | (bb >> 3)); \
    if (fmt == MPVR_RGB565PC) \
        v = (UWORD)((v << 8) | (v >> 8)); \
    put16(dest + d_index + DEST_OFFSET, v);

static void to16(plm_frame_t *frame, UBYTE *dest, int stride, ULONG fmt)
{
    int cols = frame->width >> 1, rows = frame->height >> 1;
    int yw = frame->y.width, cw = frame->cb.width, row, col;
    for (row = 0; row < rows; row++) {
        int c_index = row * cw, y_index = row * 2 * yw, d_index = row * 2 * stride;
        for (col = 0; col < cols; col++) {
            int y, rr, gg, bb;
            UWORD v;
            int cr = frame->cr.data[c_index] - 128;
            int cb = frame->cb.data[c_index] - 128;
            int r = (cr * 104597) >> 16;
            int g = (cb * 25674 + cr * 53278) >> 16;
            int b = (cb * 132201) >> 16;
            MPV_PUT16(0, 0);
            MPV_PUT16(1, 2);
            MPV_PUT16(yw, stride);
            MPV_PUT16(yw + 1, stride + 2);
            c_index += 1;
            y_index += 2;
            d_index += 4;
        }
    }
}

API VOID MpvToRGB(struct MpvPicture *pic, UBYTE *dst, ULONG modulo, ULONG format)
{
    plm_frame_t f;
    if (!pic || !dst)
        return;
    as_frame(pic, &f);
    switch (format) {
    case MPVR_RGB24:
        plm_frame_to_rgb(&f, dst, (int)modulo);
        break;
    case MPVR_BGR24:
        plm_frame_to_bgr(&f, dst, (int)modulo);
        break;
    case MPVR_ARGB32:
    case MPVR_RGBA32:
    case MPVR_BGRA32: {
        /* pl_mpeg's leave the alpha byte as it was: 0xFF first */
        ULONG r, x;
        for (r = 0; r < pic->mpic_Height; r++) {
            ULONG *d = (ULONG *)(dst + r * modulo);
            for (x = 0; x < pic->mpic_Width; x++)
                put32(d + x, format == MPVR_ARGB32 ? 0xFF000000UL : 0x000000FFUL);
        }
        if (format == MPVR_ARGB32)
            plm_frame_to_argb(&f, dst, (int)modulo);
        else if (format == MPVR_RGBA32)
            plm_frame_to_rgba(&f, dst, (int)modulo);
        else
            plm_frame_to_bgra(&f, dst, (int)modulo);
        break;
    }
    case MPVR_RGB565:
    case MPVR_RGB565PC:
    case MPVR_RGB555:
        to16(&f, dst, (int)modulo, format);
        break;
    }
}

API VOID MpvToGrey(struct MpvPicture *pic, UBYTE *dst, ULONG modulo, ULONG levels)
{
    UBYTE lut[256];
    ULONG i, r, x;
    if (!pic || !dst)
        return;
    if (!levels || levels > 256)
        levels = 256;
    for (i = 0; i < 256; i++) {
        LONG v = ((LONG)i - 16) * 255 / 219;    /* studio range made full */
        v = v < 0 ? 0 : v > 255 ? 255 : v;
        lut[i] = (UBYTE)((ULONG)v * levels / 256);
    }
    for (r = 0; r < pic->mpic_Height; r++) {
        const UBYTE *y = pic->mpic_Y + r * pic->mpic_YModulo;
        UBYTE *d = dst + r * modulo;
        for (x = 0; x < pic->mpic_Width; x++)
            d[x] = lut[y[x]];
    }
}

API CONST_STRPTR MpvVersion(void)
{
    return (CONST_STRPTR)version;
}

#ifdef __amigaos__
/* ---- the library */
/* No maths libraries: pl_mpeg's times, frame rate and aspect are kept as
 * stored (90 kHz, its codes), its MP2 decoder and player left out, so
 * there is no floating point in it at all */
int __UserLibInit(struct Library *lib)
{
    (void)lib;
    SysBase = *(struct ExecBase *volatile *)4L;
    return 0;
}

void __UserLibCleanup(void)
{
}

/* the jump table, in mpegv_lib.sfd's order (from bias 30) */
ADDTABL_1(MpvOpen, a0);
ADDTABL_1(MpvClose, a0);
ADDTABL_3(MpvWrite, a0, a1, d0);
ADDTABL_1(MpvPump, a0);
ADDTABL_2(MpvDecode, a0, a1);
ADDTABL_2(MpvGetInfo, a0, a1);
ADDTABL_3(MpvControl, a0, d0, d1);
ADDTABL_4(MpvToYUV422, a0, a1, d0, d1);
ADDTABL_4(MpvToRGB, a0, a1, d0, d1);
ADDTABL_4(MpvToGrey, a0, a1, d0, d1);
ADDTABL_0(MpvVersion);
ADDTABL_END();
#endif
