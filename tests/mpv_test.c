/*
 * mpegv.library's tests: a file's pictures through the library's
 * functions (on this computer: the decoder built in, as it is, or as
 * libmpeg2 released it with MPEGV_PLAIN; on the 68k: the library opened
 * from PROGDIR:Libs or LIBS:, or built in with MPV_STATIC for make prof),
 * each picture's planes hashed.
 * GPL-3.0-or-later, (c) 2026 Brian King.
 *
 *   mpv_test [-n pictures] [-nohash] [-skipb] [-all] [-s kind] [-dump out.yuv]
 *            [-convert y|r|g] file
 *
 * Without -all: each picture's hash, and the total. With -all: the
 * stream's details, and each picture's planes, packed YUV (each order),
 * RGB (each format) and grey hashed; then again with B pictures passed
 * over, pushed with MpvWrite instead of read through the hook, and
 * flushed (MPVC_FLUSH) after a few pictures and read again from the
 * start; and its other packets counted. tests/expected.txt is what -all
 * gave for the fixtures.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__amigaos__) && !defined(MPV_STATIC)
#include <proto/exec.h>
#include <proto/mpegv.h>
struct Library *MpegvBase;
#define REG(r, a) register a __asm(#r)
#elif defined(__amigaos__)
#include <exec/types.h>
#include <clib/mpegv_protos.h>
#define REG(r, a) register a __asm(#r)
#else
#include <exec/types.h>
#include <clib/mpegv_protos.h>
#define REG(r, a) a
#endif

static FILE *fh, *dump;
static int convert;                         /* -convert: each picture made YUV 4:2:2, RGB565 or grey (make prof) */
static unsigned long packets, packet_bytes, packet_times, packet_sub, packet_nav;
static unsigned long stream_kind = MPVS_AUTO;

/* 32-bit FNV-1a, a longword at a time (as plm_ref's) */
static unsigned long hash(unsigned long h, const UBYTE *p, unsigned long n)
{
    unsigned long i;
    for (i = 0; i + 4 <= n; i += 4) {
        unsigned long w = ((unsigned long)p[i] << 24) | ((unsigned long)p[i + 1] << 16) |
                          ((unsigned long)p[i + 2] << 8) | p[i + 3];
        h = ((h ^ w) * 16777619UL) & 0xFFFFFFFFUL;
    }
    return h;
}

static ULONG read_hook(REG(a0, struct Hook *h), REG(a2, APTR obj), REG(a1, struct MpvRead *r))
{
    size_t n = fread(r->mr_Buffer, 1, (size_t)r->mr_Length, fh);
    (void)h;
    (void)obj;
    return (ULONG)n;
}

static ULONG packet_hook(REG(a0, struct Hook *h), REG(a2, APTR obj), REG(a1, struct MpvPacket *p))
{
    (void)h;
    (void)obj;
    packets++;
    packet_bytes += p->mp_Length;
    if (p->mp_Flags & MPVPF_PTS)
        packet_times++;
    if (p->mp_Stream == 0xBD && p->mp_SubStream >= 0x80 && p->mp_SubStream <= 0x87)
        packet_sub++;
    if (p->mp_Stream == 0xBF)
        packet_nav++;
    return 0;
}

static struct Hook rh, ph;

static unsigned long planes(const struct MpvPicture *p)
{
    unsigned long h = 2166136261UL;
    h = hash(h, p->mpic_Y, (unsigned long)p->mpic_YModulo * p->mpic_YRows);
    h = hash(h, p->mpic_Cb, (unsigned long)p->mpic_CModulo * (p->mpic_YRows / 2));
    h = hash(h, p->mpic_Cr, (unsigned long)p->mpic_CModulo * (p->mpic_YRows / 2));
    return h;
}

static UBYTE *out;

/* each helper's output for the picture, hashed (rows of w * bytes) */
static void helpers(struct MpvPicture *p)
{
    static const ULONG rgb_bpp[8] = { 3, 3, 4, 4, 4, 2, 2, 2 };
    ULONG k;
    ULONG w = p->mpic_Width, h = p->mpic_Height;
    for (k = 0; k < 4; k++) {
        memset(out, 0x55, w * 4 * h);
        MpvToYUV422(p, out, w * 2, k);
        printf(" y%lu=%08lx", (unsigned long)k, hash(2166136261UL, out, w * 2 * h));
    }
    for (k = 0; k < 8; k++) {
        memset(out, 0x55, w * 4 * h);
        MpvToRGB(p, out, w * rgb_bpp[k], k);
        printf(" r%lu=%08lx", (unsigned long)k, hash(2166136261UL, out, w * rgb_bpp[k] * h));
    }
    MpvToGrey(p, out, w, 0);
    printf(" g=%08lx", hash(2166136261UL, out, w * h));
    MpvToGrey(p, out, w, 16);
    printf(" g16=%08lx", hash(2166136261UL, out, w * h));
}

/* One run: pictures decoded (push: the file given by MpvWrite), the
 * total's hash returned. */
static long run(const char *path, long max, int nohash, int all, int skipb, int push, int flush)
{
    APTR m;
    struct MpvPicture *p = NULL;
    struct MpvInfo in;
    UWORD lw = 0, lh = 0;
    LONG e = 0, more = 0;
    long n = 0;
    unsigned long total = 2166136261UL;
    static UBYTE buf[4096];
    struct TagItem tags[5];
    int flush_run = flush != 0;
    if (!(fh = fopen(path, "rb")))
        return -1;
    packets = packet_bytes = packet_times = packet_sub = packet_nav = 0;
    tags[0].ti_Tag = MPVA_ReadHook;
    tags[0].ti_Data = (unsigned long)(push ? NULL : &rh);
    tags[1].ti_Tag = MPVA_PacketHook;
    tags[1].ti_Data = (unsigned long)&ph;
    tags[2].ti_Tag = MPVA_SkipB;
    tags[2].ti_Data = (unsigned long)skipb;
    tags[3].ti_Tag = MPVA_Stream;
    tags[3].ti_Data = stream_kind;
    tags[4].ti_Tag = TAG_DONE;
    if (!(m = MpvOpen(tags))) {
        fclose(fh);
        return -1;
    }
    for (;;) {
        if (max && n >= max)
            break;
        if (flush && n == flush) {
            /* as after a seek: back to the start, what is held dropped */
            fseek(fh, 0, SEEK_SET);
            MpvControl(m, MPVC_FLUSH, 0);
            flush = 0;
        }
        p = MpvDecode(m, &e);
        if (!p) {
            if (e == MPVE_MORE && push) {
                size_t k = fread(buf, 1, sizeof(buf), fh);
                if (k)
                    MpvWrite(m, buf, (LONG)k);
                else
                    MpvControl(m, MPVC_END, 0);
                continue;
            }
            if (e == MPVE_MORE && ++more < 100000)
                continue;
            break;
        }
        more = 0;
        if (dump) {
            /* the picture as shown (for tests/psnr.py): Y, Cb, Cr, and its number */
            unsigned r;
            for (r = 0; r < p->mpic_Height; r++)
                fwrite(p->mpic_Y + r * p->mpic_YModulo, 1, p->mpic_Width, dump);
            for (r = 0; r < p->mpic_Height / 2u; r++)
                fwrite(p->mpic_Cb + r * p->mpic_CModulo, 1, p->mpic_Width / 2u, dump);
            for (r = 0; r < p->mpic_Height / 2u; r++)
                fwrite(p->mpic_Cr + r * p->mpic_CModulo, 1, p->mpic_Width / 2u, dump);
            printf("n%lu %ux%u\n", (unsigned long)p->mpic_Number, (unsigned)p->mpic_Width,
                   (unsigned)p->mpic_Height);
        }
        lw = p->mpic_Width;
        lh = p->mpic_Height;
        if (convert) {
            static UBYTE *cv;
            if (!cv && !(cv = malloc((size_t)lw * lh * 2)))
                break;
            if (convert == 'y')
                MpvToYUV422(p, cv, (ULONG)lw * 2, MPVY_Y0UY1V);
            else if (convert == 'r')
                MpvToRGB(p, cv, (ULONG)lw * 2, MPVR_RGB565);
            else
                MpvToGrey(p, cv, lw, 0);
        }
        if (!n && all && !skipb && !push && !flush_run && MpvGetInfo(m, &in)) {
            printf("info %ux%u rate %u %lu/%lu period %lu aspect %u shown %lu bitrate %lu kind %lu\n",
                   (unsigned)in.mi_Width, (unsigned)in.mi_Height, (unsigned)in.mi_RateCode,
                   (unsigned long)in.mi_RateNum, (unsigned long)in.mi_RateDen,
                   (unsigned long)in.mi_Period, (unsigned)in.mi_AspectCode,
                   (unsigned long)in.mi_ShownWidth, (unsigned long)in.mi_BitRate,
                   (unsigned long)in.mi_Kind);
            memset(&in, 0xAA, sizeof(in));
            if (MpvExamine(m, &in, sizeof(in)))
                printf("examine flags %lx profile %u level %u chroma %u aspect %u:%u display %ux%u\n",
                       (unsigned long)in.mi_Flags, (unsigned)in.mi_Profile, (unsigned)in.mi_Level,
                       (unsigned)in.mi_Chroma, (unsigned)in.mi_AspectNum, (unsigned)in.mi_AspectDen,
                       (unsigned)in.mi_DisplayWidth, (unsigned)in.mi_DisplayHeight);
        }
        if (!nohash) {
            unsigned long h = planes(p);
            total = ((total ^ h) * 16777619UL) & 0xFFFFFFFFUL;
            if (!skipb && !push && !flush_run) {
                if (all) {
                    printf("pic %ld %c n%lu t%lu d%lu f%u %c%c%c%c pts%lu%s %08lx", n,
                           " IPB"[p->mpic_Type & 3], (unsigned long)p->mpic_Number,
                           (unsigned long)p->mpic_Time, (unsigned long)p->mpic_Duration,
                           (unsigned)p->mpic_Fields, (p->mpic_Flags & MPVPF_TOPFIRST) ? 't' : '-',
                           (p->mpic_Flags & MPVPF_REPEAT) ? 'r' : '-',
                           (p->mpic_Flags & MPVPF_PROGRESSIVE) ? 'p' : '-',
                           (p->mpic_Flags & MPVPF_FIELDS) ? 'f' : '-', (unsigned long)p->mpic_PTS,
                           (p->mpic_Flags & MPVPF_PTS) ? "" : "-", h);
                    if (n < 4)
                        helpers(p);         /* (the first few: vamos is slow) */
                    printf("\n");
                } else
                    printf("%ld %08lx\n", n, h);
            }
        }
        n++;
    }
    if (!push && !all && !flush_run)
        printf("%ld pictures, %ux%u, %ld B passed over, hash %08lx\n", n, (unsigned)lw, (unsigned)lh,
               (long)MpvControl(m, MPVC_SKIPPEDB, 0), total);
    if (all)
        printf("%s %ld pictures, %ld B passed over, hash %08lx, end %ld; %lu packets (%lu bytes, %lu "
               "timed, %lu AC-3, %lu navigation) not video\n",
               skipb ? "skipb" : push ? "pushed" : flush_run ? "flushed" : "read", n,
               (long)MpvControl(m, MPVC_SKIPPEDB, 0), total, (long)e, packets, packet_bytes,
               packet_times, packet_sub, packet_nav);
    MpvClose(m);
    fclose(fh);
    return n;
}

int main(int argc, char **argv)
{
    long max = 0;
    int nohash = 0, all = 0, skipb = 0, i;
    const char *path = NULL, *base;
#ifdef MPV_STATIC
    {
        /* where the program's code is, for tests/prof.py */
        FILE *b = fopen("prof.base", "w");
        if (b) {
            fprintf(b, "code at %08lx\n", (unsigned long)main);
            fclose(b);
        }
    }
#elif defined(__amigaos__)
    if (!(MpegvBase = OpenLibrary((CONST_STRPTR) "PROGDIR:Libs/" MPEGVNAME, MPEGVVERSION)) &&
        !(MpegvBase = OpenLibrary((CONST_STRPTR)MPEGVNAME, MPEGVVERSION))) {
        printf("no mpegv.library\n");
        return 20;
    }
#endif
    rh.h_Entry = (HOOKFUNC)(APTR)read_hook;
    ph.h_Entry = (HOOKFUNC)(APTR)packet_hook;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-n") && i + 1 < argc)
            max = atol(argv[++i]);
        else if (!strcmp(argv[i], "-nohash"))
            nohash = 1;
        else if (!strcmp(argv[i], "-convert") && i + 1 < argc)
            convert = argv[++i][0];
        else if (!strcmp(argv[i], "-skipb"))
            skipb = 1;
        else if (!strcmp(argv[i], "-dump") && i + 1 < argc) {
            if (!(dump = fopen(argv[++i], "wb")))
                return 20;
            nohash = 1;
        }
        else if (!strcmp(argv[i], "-s") && i + 1 < argc)
            stream_kind = (unsigned long)atol(argv[++i]);
        else if (!strcmp(argv[i], "-all"))
            all = 1;
        else
            path = argv[i];
    }
    if (!path)
        return 20;
    if (all) {
        base = strrchr(path, '/') ? strrchr(path, '/') + 1 : strrchr(path, ':') ? strrchr(path, ':') + 1 : path;
        printf("file %s (%s)\n", base, (const char *)MpvVersion());
        out = malloc(1024 * 1024 * 4);
        if (!out)
            return 20;
    }
    if (run(path, max, nohash, all, skipb && !all, 0, 0) < 0) {
        printf("%s: not opened\n", path);
        return 20;
    }
    if (all) {
        run(path, max, 0, 1, 1, 0, 0);
        run(path, max, 0, 1, 0, 1, 0);
        run(path, max, 0, 1, 0, 0, 7);
    }
#if defined(__amigaos__) && !defined(MPV_STATIC)
    CloseLibrary(MpegvBase);
#endif
    return 0;
}
