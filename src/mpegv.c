/*
 * mpegv.library: MPEG-1 and MPEG-2 video decoded by the CPU, for any
 * program.
 *
 * The decoder is libmpeg2 0.5.1 (third_party/libmpeg2, GPL-2.0-or-later,
 * Aaron Holtzman and Michel Lespinasse), made faster for a 68k (each
 * change marked "mpegv:" there, and listed in its CHANGES), built in here
 * with the library's own memory. The demultiplexer is this file's own: it
 * reads a Program Stream (MPEG-1's packs, or MPEG-2's: a DVD's VOB) as the
 * read hook gives it (or MpvWrite), queues its video's packets for the
 * decoder, each with its time, and hands the others to the packet hook; a
 * bare video stream goes to the decoder as it is. A picture is the
 * decoder's own planes, not copied; the helpers (src/convert.c) make them
 * what a screen wants.
 *
 * Built for the Amiga as a library (src/libinit.c: libnix's, public
 * domain), and for the build machine as plain functions (the tests).
 *
 * (c) 2026 Brian King. GPL-3.0-or-later (LICENSE).
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <utility/tagitem.h>
#include <utility/hooks.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include <libraries/mpegv.h>
#include "mpeg2.h"

#ifdef __amigaos__
#include <exec/libraries.h>
#include <proto/exec.h>
#ifndef MPV_STATIC
#include "stabs.h"
/* stabs' stubs call their function with a BSR (16 bits): the stubs come
 * first and the library is more than 32 KB, so the call is a JSR */
#undef _ADDTABL_CALL
#define _ADDTABL_CALL(name)                 \
__asm__("\tmovel a6@(40:W),a4");             \
__asm__("\tjsr _" #name)

const char LibName[] = "mpegv.library";
const char LibIdString[] = "mpegv 2.2 (15.11.2026) libmpeg2, MPEG-1 and MPEG-2 video";
static const char __attribute__((used)) LibVerTag[] =
    "$VER: mpegv.library 2.2 (15.11.2026) libmpeg2, MPEG-1 and MPEG-2 video (68020+)";
const UWORD LibVersion = 2;
const UWORD LibRevision = 2;

struct ExecBase *SysBase;
#endif
#define API

/* the library's memory (and libmpeg2's: its alloc.c): exec's */
void *mpv_malloc(unsigned long n)
{
    return AllocVec(n, MEMF_ANY);
}

void mpv_free(void *p)
{
    if (p)
        FreeVec(p);
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
void *mpv_malloc(unsigned long n)
{
    return malloc(n);
}

void mpv_free(void *p)
{
    free(p);
}

static LONG call_hook(struct Hook *h, APTR obj, APTR msg)
{
    return (LONG)((ULONG(*)(struct Hook *, APTR, APTR))h->h_Entry)(h, obj, msg);
}
#endif

static const char version[] = "mpegv 2.2, libmpeg2 0.5.1 (MPEG-1 and MPEG-2 video)";

#define FEED 65536                          /* the most a packet holds: what libmpeg2 is given at once */
#define QHEAD 12                            /* a queued video packet's head: its length, flags, time */

struct Mpv {
    struct Hook *read, *packet;
    ULONG readsize;
    ULONG kind;                             /* MPVS_PROGRAM or MPVS_VIDEO; MPVS_AUTO: not yet known */
    ULONG want_vid, vid;                    /* the video's stream id: asked for (0 any); found */
    UBYTE *in;                              /* the stream's bytes, read (or written) not yet used */
    ULONG cap, rd, wr;
    UBYTE *vq;                              /* the video's packets, demultiplexed not yet decoded */
    ULONG vqcap, vqrd, vqwr;
    UBYTE *feed;                            /* the packet libmpeg2 is reading (FEED bytes) */
    mpeg2dec_t *dec;
    const mpeg2_info_t *info;
    BOOL ended;                             /* no more bytes: the read hook's end (or MPVC_END) */
    BOOL failed;                            /* the read hook could not read */
    BOOL pushed_end;
    BOOL fed_end;                           /* the decoder given a sequence end, to show its last picture */
    BOOL done;                              /* no more pictures */
    BOOL bad;                               /* not a stream it decodes */
    BOOL format;                            /* MPEG-2 not 4:2:0 */
    BOOL nomem;
    BOOL got_seq;
    BOOL packs2;                            /* MPEG-2 packs seen */
    BOOL skipb;
    BOOL seen_i;                            /* an I picture since the start (or MPVC_FLUSH) */
    BOOL closed;                            /* the last GOP closed: its first B pictures need no earlier one */
    BOOL have_first, have_pts;
    UWORD refs;                             /* I and P pictures since the start (2 at most) */
    ULONG skipped;                          /* B pictures passed over (MPVC_SKIPB) */
    ULONG number;                           /* pictures in display order so far */
    uint64_t fields;                        /* fields shown so far times the frame period (27 MHz) */
    ULONG first_pts;                        /* the first video packet's time */
    ULONG base_pts, base_time;              /* the last picture with a time: its time and mpic_Time */
    /* the last I or P picture decoded with a time of its own, and its
     * temporal reference: the B pictures shown before it, with none,
     * timed back from it while no time is known (after the start or a
     * flush: an open GOP's first B pictures, as a DVD times only its I
     * pictures) */
    BOOL ref_ok;
    ULONG ref_pts;
    UWORD ref_tr;
    struct MpvPicture pic;
};

/* ---- the stream's bytes */

/* At least need bytes held (reading them with a read hook): FALSE if
 * there are not, at the stream's end or (without a read hook) until more
 * are written. */
static BOOL more(struct Mpv *m, ULONG need)
{
    while (m->wr - m->rd < need) {
        struct MpvRead r;
        LONG n;
        if (!m->read || m->ended || m->failed) {
            if (m->pushed_end)
                m->ended = TRUE;
            return FALSE;
        }
        if (m->cap - m->wr < m->readsize) {
            memmove(m->in, m->in + m->rd, m->wr - m->rd);
            m->wr -= m->rd;
            m->rd = 0;
        }
        r.mr_Buffer = m->in + m->wr;
        r.mr_Length = (LONG)m->readsize;
        n = call_hook(m->read, m, &r);
        if (n <= 0) {
            if (n < 0)
                m->failed = TRUE;
            m->ended = TRUE;
            return FALSE;
        }
        if ((ULONG)n > m->readsize)
            n = (LONG)m->readsize;
        m->wr += (ULONG)n;
    }
    return TRUE;
}

/* what the bytes are, from the first of them (a pack header, or a
 * sequence header): FALSE until known */
static BOOL detect(struct Mpv *m)
{
    while (m->kind == MPVS_AUTO) {
        const UBYTE *b = m->in + m->rd;
        ULONG n = m->wr - m->rd, i = 0;
        while (i < n && !b[i])
            i++;
        if (i + 2 > n && n < 4096) {
            if (!more(m, n + 1)) {
                if (m->ended) {
                    m->bad = TRUE;
                    m->done = TRUE;
                }
                return FALSE;
            }
            continue;
        }
        if (i >= 2 && i + 2 <= n && b[i] == 1 && (b[i + 1] == 0xBA || b[i + 1] == 0xB3))
            m->kind = b[i + 1] == 0xBA ? MPVS_PROGRAM : MPVS_VIDEO;
        else {
            m->bad = TRUE;                  /* (neither a pack nor a sequence header first) */
            m->done = TRUE;
            return FALSE;
        }
    }
    return TRUE;
}

/* ---- the Program Stream's packets */

static ULONG pts_of(const UBYTE *p)
{
    return ((ULONG)(p[0] & 0x0E) << 29) | ((ULONG)p[1] << 22) | ((ULONG)(p[2] & 0xFE) << 14) |
           ((ULONG)p[3] << 7) | (ULONG)(p[4] >> 1);
}

static void put_long(UBYTE *b, ULONG v)
{
    b[0] = (UBYTE)(v >> 24);
    b[1] = (UBYTE)(v >> 16);
    b[2] = (UBYTE)(v >> 8);
    b[3] = (UBYTE)v;
}

static ULONG get_long(const UBYTE *b)
{
    return ((ULONG)b[0] << 24) | ((ULONG)b[1] << 16) | ((ULONG)b[2] << 8) | b[3];
}

/* a video packet queued for the decoder */
static void queue(struct Mpv *m, const UBYTE *p, ULONG n, BOOL timed, ULONG pts)
{
    if (m->vqcap - m->vqwr < n + QHEAD) {
        if (m->vqrd) {
            memmove(m->vq, m->vq + m->vqrd, m->vqwr - m->vqrd);
            m->vqwr -= m->vqrd;
            m->vqrd = 0;
        }
        if (m->vqcap - m->vqwr < n + QHEAD) {
            ULONG c = m->vqcap ? m->vqcap * 2 : 65536;
            UBYTE *q;
            while (c - m->vqwr < n + QHEAD)
                c *= 2;
            if (!(q = mpv_malloc(c))) {
                m->nomem = TRUE;
                return;
            }
            if (m->vq) {
                memcpy(q, m->vq, m->vqwr);
                mpv_free(m->vq);
            }
            m->vq = q;
            m->vqcap = c;
        }
    }
    put_long(m->vq + m->vqwr, n);
    put_long(m->vq + m->vqwr + 4, timed);
    put_long(m->vq + m->vqwr + 8, pts);
    memcpy(m->vq + m->vqwr + QHEAD, p, n);
    m->vqwr += n + QHEAD;
}

/* a packet: b its start code, its payload n bytes from b + 6 */
static void packet(struct Mpv *m, const UBYTE *b, ULONG n)
{
    ULONG id = b[3], pts = 0;
    const UBYTE *p = b + 6, *e = b + 6 + n;
    BOOL timed = FALSE;
    if (id == 0xBB || id == 0xBC || id == 0xBE || id >= 0xF0)
        return;                             /* system header, stream map, padding, the rest */
    if (id != 0xBF) {
        /* its PES header */
        if (e - p >= 3 && (p[0] & 0xC0) == 0x80) {
            ULONG hl = p[2];                /* MPEG-2's: flags, its length */
            if (3 + hl > (ULONG)(e - p))
                return;
            if ((p[1] & 0x80) && hl >= 5) {
                pts = pts_of(p + 3);
                timed = TRUE;
            }
            p += 3 + hl;
        } else {
            int k = 0;                      /* MPEG-1's: stuffing, buffer size, times */
            while (p < e && *p == 0xFF && k++ < 16)
                p++;
            if (e - p >= 2 && (p[0] & 0xC0) == 0x40)
                p += 2;
            if (e - p >= 5 && (p[0] & 0xF0) == 0x20) {
                pts = pts_of(p);
                timed = TRUE;
                p += 5;
            } else if (e - p >= 10 && (p[0] & 0xF0) == 0x30) {
                pts = pts_of(p);
                timed = TRUE;
                p += 10;
            } else if (e - p >= 1 && p[0] == 0x0F)
                p++;
            else
                return;
        }
    }
    if (id >= 0xE0 && id <= 0xEF) {
        if (!m->vid && (!m->want_vid || id == m->want_vid))
            m->vid = id;
        if (id == m->vid) {
            if (timed && !m->have_first) {
                m->first_pts = pts;
                m->have_first = TRUE;
            }
            queue(m, p, (ULONG)(e - p), timed, pts);
            return;
        }
    }
    if (m->packet) {
        struct MpvPacket pk;
        pk.mp_Stream = id;
        pk.mp_Flags = timed ? MPVPF_PTS : 0;
        pk.mp_PTS = pts;
        pk.mp_Data = (UBYTE *)p;
        pk.mp_Length = (ULONG)(e - p);
        pk.mp_SubStream = id == 0xBD && p < e ? *p : 0;
        call_hook(m->packet, m, &pk);
    }
}

/* One thing from the Program Stream: a pack header, a packet (video
 * queued, the rest to the packet hook), an end code, or bytes passed over
 * to the next start code. FALSE when more bytes are wanted (or at the
 * end). */
static BOOL step(struct Mpv *m)
{
    for (;;) {
        const UBYTE *b = m->in + m->rd;
        ULONG n = m->wr - m->rd, need;
        if (n < 6) {
            if (!more(m, 6))
                return FALSE;
            continue;
        }
        if (b[0] || b[1] || b[2] != 1) {
            ULONG i = 1;                    /* not at a start code: to the next one */
            while (i + 3 <= n && (b[i] || b[i + 1] || b[i + 2] != 1))
                i++;
            m->rd += i;
            continue;
        }
        if (b[3] == 0xBA) {
            if ((b[4] & 0xC0) == 0x40) {
                if (n < 14) {
                    if (!more(m, 14))
                        return FALSE;
                    continue;
                }
                need = 14 + (b[13] & 7);    /* MPEG-2's, and its stuffing */
                m->packs2 = TRUE;
            } else if ((b[4] & 0xF0) == 0x20)
                need = 12;                  /* MPEG-1's */
            else
                need = 4;
        } else if (b[3] == 0xB9)
            need = 4;                       /* the program's end: another may follow */
        else if (b[3] < 0xBB)
            need = 3;                       /* not a pack's: passed over */
        else {
            need = 6 + (((ULONG)b[4] << 8) | b[5]);
            if (n < need) {
                if (!more(m, need))
                    return FALSE;
                continue;
            }
            packet(m, b, need - 6);
        }
        if (n < need) {
            if (!more(m, need))
                return FALSE;
            continue;
        }
        m->rd += need;
        return TRUE;
    }
}

/* The decoder's next bytes: a video packet (with its time), or a bare
 * stream's next bytes. 1 given; 0 none yet (more to be written, or the
 * read hook failed); -1 the stream's end. */
static int give(struct Mpv *m)
{
    ULONG n;
    if (!detect(m))
        return m->done ? -1 : 0;
    if (m->kind == MPVS_VIDEO) {
        if (m->wr == m->rd && !more(m, 1))
            return m->ended && !m->failed ? -1 : 0;
        n = m->wr - m->rd;
        if (n > FEED)
            n = FEED;
        memcpy(m->feed, m->in + m->rd, n);
        m->rd += n;
        mpeg2_buffer(m->dec, m->feed, m->feed + n);
        return 1;
    }
    while (m->vqwr == m->vqrd) {
        if (m->nomem)
            return 0;
        if (!step(m))
            return m->ended && !m->failed ? -1 : 0;
    }
    n = get_long(m->vq + m->vqrd);
    memcpy(m->feed, m->vq + m->vqrd + QHEAD, n);
    if (get_long(m->vq + m->vqrd + 4))
        mpeg2_tag_picture(m->dec, get_long(m->vq + m->vqrd + 8), 1);
    m->vqrd += n + QHEAD;
    mpeg2_buffer(m->dec, m->feed, m->feed + n);
    return 1;
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
    m->want_vid = (ULONG)tag_data(tags, MPVA_VideoStream, 0);
    if (m->want_vid < 0xE0 || m->want_vid > 0xEF)
        m->want_vid = 0;
    if (m->kind > MPVS_VIDEO)
        m->kind = MPVS_AUTO;
    if (m->readsize < 2048)
        m->readsize = 2048;
    if (m->readsize > 1024 * 1024)
        m->readsize = 1024 * 1024;
    m->cap = m->readsize + FEED + 64;       /* a whole packet, and a read after it */
    if (!(m->in = mpv_malloc(m->cap)) || !(m->feed = mpv_malloc(FEED)) || !(m->dec = mpeg2_init()))
        goto fail;
    m->info = mpeg2_info(m->dec);
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
    if (m->dec)
        mpeg2_close(m->dec);
    mpv_free(m->feed);
    mpv_free(m->vq);
    mpv_free(m->in);
    mpv_free(m);
}

API LONG MpvWrite(APTR mpv, CONST UBYTE *buf, LONG len)
{
    struct Mpv *m = mpv;
    ULONG n = (ULONG)len;
    if (!m || !buf || len <= 0 || m->pushed_end || m->read)
        return 0;
    if (m->cap - m->wr < n) {
        memmove(m->in, m->in + m->rd, m->wr - m->rd);
        m->wr -= m->rd;
        m->rd = 0;
        if (m->cap - m->wr < n) {
            ULONG c = m->cap * 2;
            UBYTE *b;
            while (c - m->wr < n)
                c *= 2;
            if (!(b = mpv_malloc(c)))
                return 0;
            memcpy(b, m->in, m->wr);
            mpv_free(m->in);
            m->in = b;
            m->cap = c;
        }
    }
    memcpy(m->in + m->wr, buf, n);
    m->wr += n;
    return len;
}

API LONG MpvPump(APTR mpv)
{
    struct Mpv *m = mpv;
    if (!m || !detect(m) || m->kind != MPVS_PROGRAM)
        return FALSE;
    return step(m);
}

/* ---- the decoder */

/* a picture's place in the stream as it begins: B pictures that need a
 * picture not there (after the start, or a flush), and everything before
 * the first I picture, are passed over; B pictures too with MPVC_SKIPB */
static void begin(struct Mpv *m)
{
    const mpeg2_picture_t *cp = m->info->current_picture;
    int t = cp ? (int)(cp->flags & PIC_MASK_CODING_TYPE) : 0, skip = 0;
    if ((t == PIC_FLAG_CODING_TYPE_I || t == PIC_FLAG_CODING_TYPE_P) && (cp->flags & PIC_FLAG_TAGS)) {
        m->ref_ok = TRUE;
        m->ref_pts = cp->tag;
        m->ref_tr = (UWORD)cp->temporal_reference;
    }
    if (t == PIC_FLAG_CODING_TYPE_I) {
        m->seen_i = TRUE;
        if (m->refs < 2)
            m->refs++;
    } else if (!m->seen_i)
        skip = 1;
    else if (t == PIC_FLAG_CODING_TYPE_P) {
        if (m->refs < 2)
            m->refs++;
    } else if (t == PIC_FLAG_CODING_TYPE_B) {
        if (m->refs < 2 && !m->closed)
            skip = 1;
        else if (m->skipb) {
            skip = 1;
            m->skipped++;
        }
    }
    mpeg2_skip(m->dec, skip);
}

/* the picture to be shown next, as the caller's: NULL if it was passed
 * over (its time still counted) */
static struct MpvPicture *shown(struct Mpv *m)
{
    const mpeg2_info_t *in = m->info;
    const mpeg2_picture_t *dp = in->display_picture;
    const mpeg2_sequence_t *s = in->sequence;
    const mpeg2_fbuf_t *f = in->display_fbuf;
    struct MpvPicture *p = &m->pic;
    ULONG fields = dp->nb_fields < 2 ? 2 : dp->nb_fields, time, end;
    time = (ULONG)(m->fields / 600);
    m->fields += (uint64_t)fields * s->frame_period;
    end = (ULONG)(m->fields / 600);
    m->number++;
    if (dp->flags & PIC_FLAG_TAGS) {
        m->base_pts = dp->tag;
        m->base_time = time;
        m->have_pts = TRUE;
    } else if (!m->have_pts && m->ref_ok && dp->temporal_reference < m->ref_tr && s->frame_period) {
        /* no time known yet (the start, or a flush): a B picture shown
         * before the reference picture after it, timed back from that one
         * by their temporal references */
        m->base_pts = m->ref_pts - (ULONG)(m->ref_tr - dp->temporal_reference) * (s->frame_period / 300);
        m->base_time = time;
        m->have_pts = TRUE;
    }
    if (dp->flags & PIC_FLAG_SKIP)
        return NULL;
    p->mpic_Y = f->buf[0];
    p->mpic_Cb = f->buf[1];
    p->mpic_Cr = f->buf[2];
    p->mpic_Width = (UWORD)s->picture_width;
    p->mpic_Height = (UWORD)s->picture_height;
    p->mpic_YModulo = (UWORD)s->width;
    p->mpic_CModulo = (UWORD)s->chroma_width;
    p->mpic_YRows = (UWORD)s->height;
    p->mpic_Type = (UWORD)(dp->flags & PIC_MASK_CODING_TYPE);
    p->mpic_Number = m->number - 1;
    p->mpic_Time = time;
    p->mpic_Duration = end - time;
    p->mpic_Fields = (UWORD)fields;
    p->mpic_Flags = (m->have_pts ? MPVPF_PTS : 0) |
                    (dp->flags & PIC_FLAG_TOP_FIELD_FIRST ? MPVPF_TOPFIRST : 0) |
                    (fields > 2 ? MPVPF_REPEAT : 0) |
                    (dp->flags & PIC_FLAG_PROGRESSIVE_FRAME ? MPVPF_PROGRESSIVE : 0) |
                    (dp->nb_fields == 1 ? MPVPF_FIELDS : 0);
    p->mpic_PTS = m->have_pts ? m->base_pts + (time - m->base_time) : time;
    return p;
}

/* the decoder run to its next picture (or, with seq, to its sequence
 * header): NULL with *e if there is none (yet) */
static struct MpvPicture *run(struct Mpv *m, BOOL seq, LONG *e)
{
    static UBYTE seq_end[4] = { 0, 0, 1, 0xB7 };
    *e = MPVE_OK;
    for (;;) {
        mpeg2_state_t st;
        if (m->done || m->nomem) {
            *e = m->nomem ? MPVE_NOMEM : m->failed ? MPVE_READ : m->format ? MPVE_FORMAT
                 : (m->bad || !m->got_seq) ? MPVE_STREAM : MPVE_END;
            return NULL;
        }
        st = mpeg2_parse(m->dec);
        switch (st) {
        case STATE_BUFFER: {
            int r = give(m);
            if (r > 0)
                break;
            if (r == 0) {
                if (m->failed)
                    m->done = TRUE;
                else if (!m->nomem) {
                    *e = MPVE_MORE;
                    return NULL;
                }
                break;
            }
            if (!m->fed_end) {
                m->fed_end = TRUE;          /* its last picture shown */
                mpeg2_buffer(m->dec, seq_end, seq_end + 4);
            } else
                m->done = TRUE;
            break;
        }
        case STATE_SEQUENCE:
        case STATE_SEQUENCE_MODIFIED: {
            const mpeg2_sequence_t *s = m->info->sequence;
            if (s->chroma_width * 2 != s->width || s->chroma_height * 2 != s->height) {
                m->format = TRUE;
                m->done = TRUE;
                break;
            }
            m->got_seq = TRUE;
            if (seq)
                return NULL;
            break;
        }
        case STATE_GOP:
            m->closed = (m->info->gop->flags & (GOP_FLAG_CLOSED_GOP | GOP_FLAG_BROKEN_LINK)) ==
                        GOP_FLAG_CLOSED_GOP;
            break;
        case STATE_PICTURE:
            begin(m);
            break;
        case STATE_SLICE:
        case STATE_END:
        case STATE_INVALID_END:
            if (m->info->display_fbuf && m->info->display_picture) {
                struct MpvPicture *p = shown(m);
                if (p)
                    return p;
            }
            break;
        default:
            break;
        }
    }
}

API struct MpvPicture *MpvDecode(APTR mpv, LONG *error)
{
    struct Mpv *m = mpv;
    struct MpvPicture *p;
    LONG e;
    if (!m) {
        if (error)
            *error = MPVE_STREAM;
        return NULL;
    }
    p = run(m, FALSE, &e);
    if (error)
        *error = p ? MPVE_OK : e;
    return p;
}

/* a picture's width for its height, as it is meant to be seen: MPEG-1's
 * pixel shape's code */
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

static ULONG gcd(ULONG a, ULONG b)
{
    while (b) {
        ULONG t = a % b;
        a = b;
        b = t;
    }
    return a ? a : 1;
}

static LONG examine(struct Mpv *m, struct MpvInfo *info, ULONG size)
{
    struct MpvInfo i;
    const mpeg2_sequence_t *s;
    ULONG an, ad, g;
    LONG e;
    if (!m || !info)
        return FALSE;
    if (!m->got_seq)
        run(m, TRUE, &e);
    if (!m->got_seq || !(s = m->info->sequence))
        return FALSE;
    memset(&i, 0, sizeof(i));
    i.mi_Width = (UWORD)s->picture_width;
    i.mi_Height = (UWORD)s->picture_height;
    i.mi_RateCode = s->rate_code;
    i.mi_AspectCode = s->aspect_code;
    if (s->frame_period) {
        g = gcd(27000000UL, s->frame_period);
        i.mi_RateNum = 27000000UL / g;
        i.mi_RateDen = s->frame_period / g;
        i.mi_Period = s->frame_period / 300;
    }
    i.mi_BitRate = s->byte_rate * 8;
    i.mi_Kind = m->kind == MPVS_VIDEO ? MPVS_VIDEO : MPVS_PROGRAM;
    i.mi_Flags = (s->flags & SEQ_FLAG_MPEG2 ? MPVIF_MPEG2 : 0) |
                 (s->flags & SEQ_FLAG_PROGRESSIVE_SEQUENCE ? MPVIF_PROGRESSIVE : 0) |
                 (s->flags & SEQ_FLAG_LOW_DELAY ? MPVIF_LOWDELAY : 0) | (m->packs2 ? MPVIF_PACKS2 : 0);
    i.mi_Chroma = MPVCF_420;
    i.mi_DisplayWidth = (UWORD)s->display_width;
    i.mi_DisplayHeight = (UWORD)s->display_height;
    if (s->flags & SEQ_FLAG_MPEG2) {
        if (!(s->profile_level_id & 0x80)) {
            i.mi_Profile = (UWORD)((s->profile_level_id >> 4) & 7);
            i.mi_Level = (UWORD)(s->profile_level_id & 15);
        }
        /* the picture's shape (its displayed part's, as the standard has it) */
        switch (s->aspect_code) {
        case 2:
            an = 4, ad = 3;
            break;
        case 3:
            an = 16, ad = 9;
            break;
        case 4:
            an = 221, ad = 100;
            break;
        default:
            an = s->display_width, ad = s->display_height;
            break;
        }
        i.mi_ShownWidth = ad ? (s->picture_height * an + ad / 2) / ad : s->picture_width;
    } else {
        i.mi_ShownWidth = shown_w(s->picture_width, s->picture_height, s->aspect_code);
        an = i.mi_ShownWidth, ad = s->picture_height;
    }
    g = gcd(an, ad);
    i.mi_AspectNum = (UWORD)(an / g);
    i.mi_AspectDen = (UWORD)(ad / g);
    memcpy(info, &i, size < sizeof(i) ? size : sizeof(i));
    return TRUE;
}

API LONG MpvGetInfo(APTR mpv, struct MpvInfo *info)
{
    return examine(mpv, info, offsetof(struct MpvInfo, mi_Flags));
}

API LONG MpvExamine(APTR mpv, struct MpvInfo *info, ULONG size)
{
    return examine(mpv, info, size);
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
        break;
    case MPVC_SKIPPEDB:
        r = (LONG)m->skipped;
        break;
    case MPVC_ENDED:
        r = m->ended || m->done;
        break;
    case MPVC_FIRSTPTS:
        r = m->have_first ? (LONG)m->first_pts : -1;
        break;
    case MPVC_END:
        m->pushed_end = TRUE;
        break;
    case MPVC_HELD:
        r = (LONG)(m->wr - m->rd + m->vqwr - m->vqrd + (ULONG)mpeg2_getpos(m->dec));
        break;
    case MPVC_FLUSH:
        m->rd = m->wr = 0;
        m->vqrd = m->vqwr = 0;
        mpeg2_reset(m->dec, 0);
        m->seen_i = m->closed = FALSE;
        m->refs = 0;
        m->have_pts = m->ref_ok = FALSE;    /* (no time from before it: the next part's own) */
        m->ended = m->pushed_end = m->fed_end = FALSE;
        if (!m->bad && !m->format && !m->failed && !m->nomem)
            m->done = FALSE;
        break;
    }
    return r;
}

API CONST_STRPTR MpvVersion(void)
{
    return (CONST_STRPTR)version;
}

/* the helpers: src/convert.c */
API VOID MpvToYUV422(struct MpvPicture *pic, UBYTE *dst, ULONG modulo, ULONG order);
API VOID MpvToRGB(struct MpvPicture *pic, UBYTE *dst, ULONG modulo, ULONG format);
API VOID MpvToGrey(struct MpvPicture *pic, UBYTE *dst, ULONG modulo, ULONG levels);

#if defined(__amigaos__) && !defined(MPV_STATIC)
/* ---- the library */
/* No maths libraries: libmpeg2 is integers throughout, and times are kept
 * as MPEG stores them (90 kHz); its tables are made once, here */
int __UserLibInit(struct Library *lib)
{
    (void)lib;
    SysBase = *(struct ExecBase *volatile *)4L;
    mpeg2_accel(0);
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
ADDTABL_3(MpvExamine, a0, a1, d0);
ADDTABL_END();
#else
/* built into a program (the tests): libmpeg2's tables made before the
 * first decoder */
__attribute__((constructor)) static void mpv_init(void)
{
    mpeg2_accel(0);
}
#endif
