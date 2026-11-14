/*
 * mpegv.library's tests: the reference. pl_mpeg called directly, as a
 * program that builds it in does (its demuxer, the video packets to the
 * video decoder, read 16 KB at a time), each picture's planes hashed. The
 * library's pictures (tests/mpv_test.c) must hash the same; and on a
 * 68020 under vamos it times the built-in way (make prof).
 * MIT licence, (c) 2026 Brian King.
 *
 *   plm_ref file [pictures [nohash]]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define PLM_NO_STDIO
#define PL_MPEG_IMPLEMENTATION
#include "pl_mpeg.h"

static FILE *fh;
static plm_buffer_t *fbuf, *vbuf;
static plm_demux_t *demux;
static int ended;
static uint8_t in[16384];

static int pump(void)
{
    plm_packet_t *p;
    if (ended)
        return 0;
    if (!(p = plm_demux_decode(demux))) {
        ended = 1;
        plm_buffer_signal_end(vbuf);
        return 0;
    }
    if (p->type == (int)PLM_DEMUX_PACKET_VIDEO_1)
        plm_buffer_write(vbuf, p->data, p->length);
    return 1;
}

static void fload(plm_buffer_t *b, void *user)
{
    size_t n = fread(in, 1, sizeof(in), fh);
    (void)user;
    if (!n)
        plm_buffer_signal_end(b);
    else
        plm_buffer_write(b, in, n);
}

static void vload(plm_buffer_t *b, void *user)
{
    size_t had = plm_buffer_get_remaining(b);
    (void)user;
    if (!demux) {
        fload(b, user);                     /* (a bare video stream: the file itself) */
        return;
    }
    while (plm_buffer_get_remaining(b) == had && pump())
        ;
}

/* 32-bit FNV-1a over the plane, a longword at a time */
static uint32_t hash(uint32_t h, const uint8_t *p, size_t n)
{
    size_t i;
    for (i = 0; i + 4 <= n; i += 4) {
        uint32_t w = ((uint32_t)p[i] << 24) | ((uint32_t)p[i + 1] << 16) | ((uint32_t)p[i + 2] << 8) | p[i + 3];
        h = (h ^ w) * 16777619u;
    }
    return h;
}

int main(int argc, char **argv)
{
    plm_video_t *vd;
    plm_frame_t *f;
    long max = argc > 2 ? atol(argv[2]) : 0, n = 0;
    int nohash = argc > 3 && !strcmp(argv[3], "nohash");
    uint32_t all = 2166136261u;
    uint8_t h4[4];
    if (argc < 2 || !(fh = fopen(argv[1], "rb")))
        return 20;
    if (!(fread(h4, 1, 4, fh) == 4 && !h4[0] && !h4[1] && h4[2] == 1 && h4[3] == 0xB3)) {
        fseek(fh, 0, SEEK_SET);             /* (before the demuxer: it reads at once) */
        fbuf = plm_buffer_create_with_capacity(PLM_BUFFER_DEFAULT_SIZE);
        plm_buffer_set_load_callback(fbuf, fload, NULL);
        demux = plm_demux_create(fbuf, 0);
    } else
        fseek(fh, 0, SEEK_SET);
    vbuf = plm_buffer_create_with_capacity(PLM_BUFFER_DEFAULT_SIZE);
    plm_buffer_set_load_callback(vbuf, vload, NULL);
    vd = plm_video_create_with_buffer(vbuf, 0);
    while ((!max || n < max) && (f = plm_video_decode(vd))) {
        uint32_t h = 2166136261u;
        if (!nohash) {
            h = hash(h, f->y.data, (size_t)f->y.width * f->y.height);
            h = hash(h, f->cb.data, (size_t)f->cb.width * f->cb.height);
            h = hash(h, f->cr.data, (size_t)f->cr.width * f->cr.height);
            printf("%ld %08lx\n", n, (unsigned long)h);
        }
        all = (all ^ h) * 16777619u;
        n++;
    }
    printf("%ld pictures, %ux%u, hash %08lx\n", n, (unsigned)plm_video_get_width(vd),
           (unsigned)plm_video_get_height(vd), (unsigned long)all);
    return 0;
}
