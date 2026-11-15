/*
 * mpegv.library's tests: its motion compensation (third_party/libmpeg2/
 * libmpeg2/motion_comp.c, the "mpegv:" functions, four pixels at a time)
 * against libmpeg2's C: every function (put and average; whole, half
 * across, half down, half both ways; 16 and 8 wide), random pictures,
 * references at every alignment, the same bytes to the bit.
 * GPL-3.0-or-later, (c) 2026 Brian King.
 *
 *   mc_test [rounds]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "motion_comp.c"

static unsigned long seed = 7;

static int rnd(int n)
{
    seed = seed * 1103515245 + 12345;
    return (int)((seed >> 8) % (unsigned long)n);
}

int main(int argc, char **argv)
{
    static uint8_t ref[64 * 48], a[64 * 40], b[64 * 40];
    int rounds = argc > 1 ? atoi(argv[1]) : 20000, n, f, bad = 0, i;
    for (n = 0; n < rounds; n++) {
        for (f = 0; f < 16; f++) {
            mpeg2_mc_fct *c = f < 8 ? mpeg2_mc_c.put[f] : mpeg2_mc_c.avg[f - 8];
            mpeg2_mc_fct *v = f < 8 ? mpeg2_mc_mpegv.put[f] : mpeg2_mc_mpegv.avg[f - 8];
            int stride = 64, h = (f & 4) ? (rnd(2) ? 4 : 8) : (rnd(2) ? 8 : 16);
            int ro = rnd(16) + stride * rnd(8), dof = 8 * rnd(4) + stride * rnd(8);
            for (i = 0; i < (int)sizeof(ref); i++)
                ref[i] = (uint8_t)(rnd(4) ? rnd(256) : (rnd(2) ? 0 : 255));
            for (i = 0; i < (int)sizeof(a); i++)
                a[i] = b[i] = (uint8_t)rnd(256);
            c(a + dof, ref + ro, stride, h);
            v(b + dof, ref + ro, stride, h);
            if (memcmp(a, b, sizeof(a)) && bad++ < 5)
                printf("function %d (%s), reference at %d: differs\n", f & 7, f < 8 ? "put" : "avg", ro);
        }
    }
    printf("mpegv's motion compensation against libmpeg2's C: %d calls, %d differ\n", rounds * 16, bad);
    return bad != 0;
}
