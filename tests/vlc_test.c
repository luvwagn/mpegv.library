/*
 * mpegv.library's tests: the blocks' coefficients as vlc_68k.s decodes
 * them (68020) against slice.c's C (get_non_intra_block and the rest):
 * random streams (short codes, escapes, the longest codes, ends of block,
 * codes that are not valid), random quantisers (saturating too), random
 * scans; the same coefficients, i and bitstream state, to the bit.
 * GPL-3.0-or-later, (c) 2026 Brian King.
 *
 *   vlc_test [rounds [dump]]   (dump: the first few streams that differ)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MPEGV_VLC_TEST
#include "slice.c"

/* the rest of libmpeg2 is linked in (slice.c needs it); its allocator */
void *mpv_malloc(unsigned long size)
{
    return malloc(size);
}

void mpv_free(void *buf)
{
    free(buf);
}

static unsigned long seed = 11;
static int dump;

static int rnd(int n)
{
    seed = seed * 1103515245 + 12345;
    return (int)((seed >> 8) % (unsigned long)n);
}

static mpeg2_decoder_t dc, da;
static uint8_t stream[256 + 64];
static uint16_t quant[64];
static uint8_t scan[64];

int main(int argc, char **argv)
{
    static const char *const name[5] = { "ni2", "intra2 B14", "intra2 B15", "ni1", "intra1" };
    int rounds = argc > 1 ? atoi(argv[1]) : 20000, n, i, kind, bad = 0, rc = 0, ra = 0;
    long coefs = 0;
    dump = argc > 2;
    mv_vlc_init();
    for (n = 0; n < rounds; n++) {
        int zeros = rnd(4);
        for (i = 0; i < (int)sizeof(stream); i++)
            stream[i] = (uint8_t)(rnd(4) < zeros ? rnd(8) : rnd(256));
        for (i = 0; i < 64; i++) {
            int big = rnd(8);
            quant[i] = (uint16_t)(big == 0 ? 1 + rnd(28560) : big == 1 ? 1 + rnd(8) : 1 + rnd(64 * 31));
            scan[i] = (uint8_t)i;
        }
        for (i = 63; i > 0; i--) {
            int k = rnd(i + 1);
            uint8_t t = scan[i];
            scan[i] = scan[k];
            scan[k] = t;
        }
        kind = n % 5;
        rc = ra = 0;
        memset(&dc, 0, sizeof(dc));
        dc.scan = scan;
        dc.quantizer_matrix[0] = dc.quantizer_matrix[1] = quant;
        dc.bitstream_ptr = stream + 4;
        dc.bitstream_buf = (uint32_t)stream[0] << 24 | stream[1] << 16 | stream[2] << 8 | stream[3];
        dc.bitstream_bits = -16;
        {
            /* a few bits already taken */
            int k = rnd(17);
            dc.bitstream_buf <<= k;
            dc.bitstream_bits += k;
            if (dump && kind == 0)
                printf("round %d skip %d\n", n, k);
        }
        dc.DCTblock[0] = (int16_t)(rnd(2048) << rnd(4));
        da = dc;
        switch (kind) {
        case 0:
            rc = get_non_intra_block(&dc, quant);
            ra = MV_VLC(ni2, &da, quant, &mv_tabs_ni);
            break;
        case 1:
            get_intra_block_B14(&dc, quant);
            MV_VLC(intra2, &da, quant, &mv_tabs_b14);
            break;
        case 2:
            get_intra_block_B15(&dc, quant);
            MV_VLC(intra2, &da, quant, &mv_tabs_b15);
            break;
        case 3:
            rc = get_mpeg1_non_intra_block(&dc);
            ra = MV_VLC(ni1, &da, quant, &mv_tabs_ni);
            break;
        default:
            get_mpeg1_intra_block(&dc);
            MV_VLC(intra1, &da, quant, &mv_tabs_b14);
            break;
        }
        for (i = 0; i < 64; i++)
            coefs += dc.DCTblock[i] != 0;
        if (rc != ra || dc.bitstream_buf != da.bitstream_buf || dc.bitstream_bits != da.bitstream_bits
            || dc.bitstream_ptr != da.bitstream_ptr || memcmp(dc.DCTblock, da.DCTblock, sizeof(dc.DCTblock))) {
            if (bad++ < 5) {
                printf("round %d (%s): i %d / %d, buf %08lx / %08lx, bits %d / %d, ptr +%d / +%d\n", n,
                       name[kind], rc, ra, (unsigned long)dc.bitstream_buf, (unsigned long)da.bitstream_buf,
                       dc.bitstream_bits, da.bitstream_bits, (int)(dc.bitstream_ptr - stream),
                       (int)(da.bitstream_ptr - stream));
                if (dump)
                    for (i = 0; i < 12; i++)
                        printf("%02x%s", stream[i], i == 11 ? "\n" : " ");
                for (i = 0; i < 64; i++)
                    if (dc.DCTblock[i] != da.DCTblock[i])
                        printf("  [%d] %d / %d\n", i, dc.DCTblock[i], da.DCTblock[i]);
            }
        }
    }
    printf("mpegv's coefficient decoding against libmpeg2's C: %d blocks (%ld coefficients), %d differ\n",
           rounds, coefs, bad);
    return bad != 0;
}
