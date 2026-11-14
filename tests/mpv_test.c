/*
 * mpegv.library's tests: a file's pictures through the library's
 * functions (on this computer: src/mpegv.c built in; on the 68k: the
 * library opened from PROGDIR:Libs or LIBS:), each picture's planes
 * hashed as tests/plm_ref.c hashes pl_mpeg's own.
 * MIT licence, (c) 2026 Brian King.
 *
 *   mpv_test [-n pictures] [-nohash] [-all] file
 *
 * Without -all: the same lines as plm_ref. With -all: the stream's
 * details, and each picture's planes, packed YUV (each order), RGB (each
 * format) and grey hashed; then again with B pictures passed over, and
 * pushed with MpvWrite instead of read through the hook; and its other
 * packets counted. tests/expected.txt is what -all gave for the fixtures.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __amigaos__
#include <proto/exec.h>
#include <proto/mpegv.h>
struct Library *MpegvBase;
#define REG(r, a) register a __asm(#r)
#else
#include <exec/types.h>
#include <clib/mpegv_protos.h>
#define REG(r, a) a
#endif

static FILE *fh;
static unsigned long packets, packet_bytes, packet_times;
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
static long run(const char *path, long max, int nohash, int all, int skipb, int push)
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
    if (!(fh = fopen(path, "rb")))
        return -1;
    packets = packet_bytes = packet_times = 0;
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
        lw = p->mpic_Width;
        lh = p->mpic_Height;
        if (!n && all && !skipb && !push && MpvGetInfo(m, &in))
            printf("info %ux%u rate %u %lu/%lu period %lu aspect %u shown %lu bitrate %lu kind %lu\n",
                   (unsigned)in.mi_Width, (unsigned)in.mi_Height, (unsigned)in.mi_RateCode,
                   (unsigned long)in.mi_RateNum, (unsigned long)in.mi_RateDen,
                   (unsigned long)in.mi_Period, (unsigned)in.mi_AspectCode,
                   (unsigned long)in.mi_ShownWidth, (unsigned long)in.mi_BitRate,
                   (unsigned long)in.mi_Kind);
        if (!nohash) {
            unsigned long h = planes(p);
            total = ((total ^ h) * 16777619UL) & 0xFFFFFFFFUL;
            if (!skipb && !push) {
                if (all) {
                    printf("pic %ld %c n%lu t%lu pts%lu%s %08lx", n, " IPB"[p->mpic_Type & 3],
                           (unsigned long)p->mpic_Number, (unsigned long)p->mpic_Time,
                           (unsigned long)p->mpic_PTS, (p->mpic_Flags & MPVPF_PTS) ? "" : "-", h);
                    helpers(p);
                    printf("\n");
                } else
                    printf("%ld %08lx\n", n, h);
            }
        }
        n++;
    }
    if (!skipb && !push && !all)
        printf("%ld pictures, %ux%u, hash %08lx\n", n, (unsigned)lw, (unsigned)lh, total);
    if (all)
        printf("%s %ld pictures, %ld B passed over, hash %08lx, end %ld; %lu packets (%lu bytes, %lu "
               "timed) not video\n",
               skipb ? "skipb" : push ? "pushed" : "read", n,
               (long)MpvControl(m, MPVC_SKIPPEDB, 0), total, (long)e, packets, packet_bytes,
               packet_times);
    MpvClose(m);
    fclose(fh);
    return n;
}

int main(int argc, char **argv)
{
    long max = 0;
    int nohash = 0, all = 0, i;
    const char *path = NULL, *base;
#ifdef __amigaos__
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
    if (run(path, max, nohash, all, 0, 0) < 0) {
        printf("%s: not opened\n", path);
        return 20;
    }
    if (all) {
        run(path, max, 0, 1, 1, 0);
        run(path, max, 0, 1, 0, 1);
    }
#ifdef __amigaos__
    CloseLibrary(MpegvBase);
#endif
    return 0;
}
