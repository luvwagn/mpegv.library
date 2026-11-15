/*
 * mpvplay: mpegv.library's example. Plays an MPEG-1 or MPEG-2 file's
 * pictures (.mpg, .m1v, .m2v, a DVD's .vob; no sound) at their pace: in
 * a window on Workbench, drawn as RGB, where it is an RTG screen of 15 bits
 * or more (cybergraphics.library); else on a screen of its own, 256 greys
 * (AGA, graphics.library 40's WriteChunkyPixels; high resolution and
 * interlaced for a DVD's 720 x 576). When it falls behind it passes B
 * pictures over until it has caught up. The window's close gadget or
 * Ctrl-C stops it.
 *
 *   mpvplay file
 *
 * MIT licence, (c) 2026 Brian King.
 */
#include <stdio.h>
#include <exec/types.h>
#include <intuition/intuition.h>
#include <graphics/gfx.h>
#include <cybergraphx/cybergraphics.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/cybergraphics.h>
#include <proto/mpegv.h>

struct Library *MpegvBase, *CyberGfxBase;

static BPTR fh;

/* the read hook: the file's next bytes */
static ULONG read_hook(register struct Hook *h __asm("a0"), register APTR mpv __asm("a2"),
                       register struct MpvRead *r __asm("a1"))
{
    (void)h;
    (void)mpv;
    return (ULONG)Read(fh, r->mr_Buffer, r->mr_Length);
}

int main(int argc, char **argv)
{
    static struct Hook rh;
    struct Screen *wb = NULL, *scr = NULL;
    struct Window *win = NULL;
    struct MpvInfo in;
    BOOL skipping = FALSE;
    struct MpvPicture *p;
    UBYTE *buf = NULL;
    APTR m = NULL;
    BOOL rtg = FALSE;
    LONG e = 0, rc = 20;
    struct DateStamp t0, now;
    if (argc < 2) {
        printf("usage: mpvplay file (.mpg, .m1v, .m2v, .vob)\n");
        return 5;
    }
    if (!(MpegvBase = OpenLibrary((CONST_STRPTR)MPEGVNAME, MPEGVVERSION))) {
        printf("mpvplay: no %s %d\n", MPEGVNAME, MPEGVVERSION);
        return 20;
    }
    if (!(fh = Open((CONST_STRPTR)argv[1], MODE_OLDFILE))) {
        printf("mpvplay: %s not opened\n", argv[1]);
        goto out;
    }
    rh.h_Entry = (HOOKFUNC)(APTR)read_hook;
    if (!(m = MpvOpenTags(MPVA_ReadHook, (ULONG)&rh, TAG_DONE)) || !MpvExamine(m, &in, sizeof(in))) {
        printf("mpvplay: not an MPEG stream mpegv.library decodes\n");
        goto out;
    }
    printf("%s %ux%u (%u:%u), %lu/%lu pictures a second, %s (%s)\n",
           in.mi_Flags & MPVIF_MPEG2 ? "MPEG-2" : "MPEG-1", (unsigned)in.mi_Width,
           (unsigned)in.mi_Height, (unsigned)in.mi_AspectNum, (unsigned)in.mi_AspectDen,
           (unsigned long)in.mi_RateNum, (unsigned long)in.mi_RateDen,
           in.mi_Flags & MPVIF_PROGRESSIVE ? "progressive" : "interlaced", (const char *)MpvVersion());
    CyberGfxBase = OpenLibrary((CONST_STRPTR) "cybergraphics.library", 40);
    if (CyberGfxBase && (wb = LockPubScreen(NULL)) &&
        GetCyberMapAttr(wb->RastPort.BitMap, CYBRMATTR_ISCYBERGFX) &&
        GetCyberMapAttr(wb->RastPort.BitMap, CYBRMATTR_DEPTH) > 8)
        rtg = TRUE;
    if (rtg) {
        win = OpenWindowTags(NULL, WA_PubScreen, (ULONG)wb, WA_InnerWidth, in.mi_Width,
                             WA_InnerHeight, in.mi_Height, WA_Title, (ULONG) "mpvplay",
                             WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE,
                             WA_Activate, TRUE, WA_IDCMP, IDCMP_CLOSEWINDOW, TAG_DONE);
        buf = AllocVec((ULONG)in.mi_Width * in.mi_Height * 3, MEMF_ANY);
    } else if (GfxBase->LibNode.lib_Version >= 40) {
        /* AGA: a screen of 256 greys, the pictures as chunky pixels */
        ULONG cols[2 + 256 * 3], i;
        cols[0] = 256L << 16;
        for (i = 0; i < 256; i++)
            cols[1 + i * 3] = cols[2 + i * 3] = cols[3 + i * 3] = i * 0x01010101UL;
        cols[1 + 256 * 3] = 0;
        scr = OpenScreenTags(NULL, SA_Width, in.mi_Width, SA_Height, in.mi_Height, SA_Depth, 8,
                             SA_DisplayID, (in.mi_Width > 400 ? HIRES_KEY : LORES_KEY) |
                                               (in.mi_Height > 300 ? LACE : 0),
                             SA_Colors32, (ULONG)cols, SA_Quiet, TRUE, TAG_DONE);
        if (scr)
            win = OpenWindowTags(NULL, WA_CustomScreen, (ULONG)scr, WA_Borderless, TRUE,
                                 WA_Backdrop, TRUE, WA_RMBTrap, TRUE, WA_Activate, TRUE, TAG_DONE);
        buf = AllocVec((ULONG)in.mi_Width * in.mi_Height, MEMF_ANY);
    }
    if (wb)
        UnlockPubScreen(NULL, wb);
    if (!win || !buf) {
        printf("mpvplay: no window (an RTG screen of 15 bits or more, or AGA, is needed)\n");
        goto out;
    }
    DateStamp(&t0);
    while ((p = MpvDecode(m, &e)) || e == MPVE_MORE) {
        struct IntuiMessage *im;
        LONG due, ms;
        if (!p)
            continue;
        if (SetSignal(0, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C)
            break;
        if (win->UserPort && (im = (struct IntuiMessage *)GetMsg(win->UserPort))) {
            ReplyMsg((struct Message *)im);
            break;                          /* (its close gadget) */
        }
        /* at its time (50 ticks a second): a late picture is shown at once */
        DateStamp(&now);
        ms = (now.ds_Days - t0.ds_Days) * 86400000 + (now.ds_Minute - t0.ds_Minute) * 60000 +
             (now.ds_Tick - t0.ds_Tick) * 20;
        due = (LONG)(p->mpic_Time / 90);
        if (due > ms + 20)
            Delay((due - ms) / 20);
        /* more than two pictures behind: B pictures passed over (nothing
         * is predicted from them) until it has caught up */
        if (!skipping && ms > due + 2 * (LONG)(in.mi_Period / 90))
            MpvControl(m, MPVC_SKIPB, skipping = TRUE);
        else if (skipping && ms < due)
            MpvControl(m, MPVC_SKIPB, skipping = FALSE);
        if (rtg) {
            MpvToRGB(p, buf, (ULONG)p->mpic_Width * 3, MPVR_RGB24);
            WritePixelArray(buf, 0, 0, (UWORD)(p->mpic_Width * 3), win->RPort, win->BorderLeft,
                            win->BorderTop, p->mpic_Width, p->mpic_Height, RECTFMT_RGB);
        } else {
            MpvToGrey(p, buf, p->mpic_Width, 256);
            WriteChunkyPixels(win->RPort, 0, 0, p->mpic_Width - 1, p->mpic_Height - 1, buf,
                              p->mpic_Width);
        }
    }
    rc = 0;
out:
    if (win)
        CloseWindow(win);
    if (scr)
        CloseScreen(scr);
    if (buf)
        FreeVec(buf);
    if (m)
        MpvClose(m);
    if (fh)
        Close(fh);
    if (CyberGfxBase)
        CloseLibrary(CyberGfxBase);
    CloseLibrary(MpegvBase);
    return rc;
}
