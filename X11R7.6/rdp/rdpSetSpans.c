/*
Copyright 2005-2013 Jay Sorg

Permission to use, copy, modify, distribute, and sell this software and its
documentation for any purpose is hereby granted without fee, provided that
the above copyright notice appear in all copies and that both that
copyright notice and this permission notice appear in supporting
documentation.

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
OPEN GROUP BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

#include "rdp.h"
#include "rdpdraw.h"

#define LDEBUG 0

#define LOG_LEVEL 1
#define LLOG(_level, _args) \
    do { if (_level < LOG_LEVEL) { ErrorF _args ; } } while (0)
#define LLOGLN(_level, _args) \
    do { if (_level < LOG_LEVEL) { ErrorF _args ; ErrorF("\n"); } } while (0)

extern rdpScreenInfoRec g_rdpScreen; /* from rdpmain.c */
extern DevPrivateKeyRec g_rdpGCIndex; /* from rdpmain.c */
extern DevPrivateKeyRec g_rdpWindowIndex; /* from rdpmain.c */
extern DevPrivateKeyRec g_rdpPixmapIndex; /* from rdpmain.c */
extern int g_Bpp; /* from rdpmain.c */
extern ScreenPtr g_pScreen; /* from rdpmain.c */
extern Bool g_wrapPixmap; /* from rdpmain.c */
extern int g_do_dirty_os; /* in rdpmain.c */
extern int g_do_dirty_ons; /* in rdpmain.c */
extern rdpPixmapRec g_screenPriv; /* in rdpmain.c */

extern GCOps g_rdpGCOps; /* from rdpdraw.c */

extern int g_con_number; /* in rdpup.c */

/******************************************************************************/
void
rdpSetSpansOrg(DrawablePtr pDrawable, GCPtr pGC, char *psrc,
               DDXPointPtr ppt, int *pwidth, int nspans, int fSorted)
{
    rdpGCPtr priv;
    GCFuncs *oldFuncs;

    GC_OP_PROLOGUE(pGC);
    pGC->ops->SetSpans(pDrawable, pGC, psrc, ppt, pwidth, nspans, fSorted);
    GC_OP_EPILOGUE(pGC);
}

/******************************************************************************/
void
rdpSetSpans(DrawablePtr pDrawable, GCPtr pGC, char *psrc,
            DDXPointPtr ppt, int *pwidth, int nspans, int fSorted)
{
    RegionRec clip_reg;
    int cd;
    int got_id;
    int dirty_type;
    int post_process;
    int reset_surface;
    struct image_data id;
    WindowPtr pDstWnd;
    PixmapPtr pDstPixmap;
    rdpPixmapRec *pDstPriv;
    rdpPixmapRec *pDirtyPriv;

    LLOGLN(0, ("rdpSetSpans: todo"));

    /* do original call */
    rdpSetSpansOrg(pDrawable, pGC, psrc, ppt, pwidth, nspans, fSorted);

    dirty_type = 0;
    pDirtyPriv = 0;
    post_process = 0;
    reset_surface = 0;
    got_id = 0;

    if (pDrawable->type == DRAWABLE_PIXMAP)
    {
        pDstPixmap = (PixmapPtr)pDrawable;
        pDstPriv = GETPIXPRIV(pDstPixmap);

        if (xrdp_is_os(pDstPixmap, pDstPriv))
        {
            post_process = 1;

            if (g_do_dirty_os)
            {
                LLOGLN(10, ("rdpSetSpans: getting dirty"));
                pDstPriv->is_dirty = 1;
                pDirtyPriv = pDstPriv;
                dirty_type = RDI_IMGLY;
            }
            else
            {
                rdpup_switch_os_surface(pDstPriv->rdpindex);
                reset_surface = 1;
                rdpup_get_pixmap_image_rect(pDstPixmap, &id);
                got_id = 1;
            }
        }
    }
    else
    {
        if (pDrawable->type == DRAWABLE_WINDOW)
        {
            pDstWnd = (WindowPtr)pDrawable;

            if (pDstWnd->viewable)
            {
                post_process = 1;
                if (g_do_dirty_ons)
                {
                    LLOGLN(10, ("rdpSetSpans: getting dirty"));
                    g_screenPriv.is_dirty = 1;
                    pDirtyPriv = &g_screenPriv;
                    dirty_type = RDI_IMGLL;
                }
                else
                {
                    rdpup_get_screen_image_rect(&id);
                    got_id = 1;
                }
            }
        }
    }

    if (!post_process)
    {
        return;
    }

    RegionInit(&clip_reg, NullBox, 0);
    cd = rdp_get_clip(&clip_reg, pDrawable, pGC);

    if (cd == 1)
    {
        if (dirty_type != 0)
        {
        }
        else if (got_id)
        {
        }
    }
    else if (cd == 2)
    {
        if (dirty_type != 0)
        {
        }
        else if (got_id)
        {
        }
    }

    RegionUninit(&clip_reg);

    if (reset_surface)
    {
        rdpup_switch_os_surface(-1);
    }
}
