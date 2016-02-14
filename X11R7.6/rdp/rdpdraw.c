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

Xserver drawing ops and funcs

*/

#include "rdp.h"
#include "gcops.h"
#include "rdpdraw.h"

#include "rdpCopyArea.h"
#include "rdpPolyFillRect.h"
#include "rdpPutImage.h"
#include "rdpPolyRectangle.h"
#include "rdpPolylines.h"
#include "rdpPolySegment.h"
#include "rdpFillSpans.h"
#include "rdpSetSpans.h"
#include "rdpCopyPlane.h"
#include "rdpPolyPoint.h"
#include "rdpPolyArc.h"
#include "rdpFillPolygon.h"
#include "rdpPolyFillArc.h"
#include "rdpPolyText8.h"
#include "rdpPolyText16.h"
#include "rdpImageText8.h"
#include "rdpImageText16.h"
#include "rdpImageGlyphBlt.h"
#include "rdpPolyGlyphBlt.h"
#include "rdpPushPixels.h"
#include "rdpglyph.h"

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
extern WindowPtr g_invalidate_window; /* in rdpmain.c */
extern int g_use_rail; /* in rdpmain.c */
extern int g_do_dirty_os; /* in rdpmain.c */
extern int g_do_dirty_ons; /* in rdpmain.c */
extern rdpPixmapRec g_screenPriv; /* in rdpmain.c */
extern int g_con_number; /* in rdpmain.c */
extern int g_do_glyph_cache; /* in rdpmain.c */

ColormapPtr g_rdpInstalledColormap;

GCFuncs g_rdpGCFuncs =
{
    rdpValidateGC, rdpChangeGC, rdpCopyGC, rdpDestroyGC, rdpChangeClip,
    rdpDestroyClip, rdpCopyClip
};

GCOps g_rdpGCOps =
{
    rdpFillSpans, rdpSetSpans, rdpPutImage, rdpCopyArea, rdpCopyPlane,
    rdpPolyPoint, rdpPolylines, rdpPolySegment, rdpPolyRectangle,
    rdpPolyArc, rdpFillPolygon, rdpPolyFillRect, rdpPolyFillArc,
    rdpPolyText8, rdpPolyText16, rdpImageText8, rdpImageText16,
    rdpImageGlyphBlt, rdpPolyGlyphBlt, rdpPushPixels
};

/******************************************************************************/
/* return 0, draw nothing */
/* return 1, draw with no clip */
/* return 2, draw using clip */
int
rdp_get_clip(RegionPtr pRegion, DrawablePtr pDrawable, GCPtr pGC)
{
    WindowPtr pWindow;
    RegionPtr temp;
    BoxRec box;
    int rv;

    rv = 0;

    if (pDrawable->type == DRAWABLE_PIXMAP)
    {
        switch (pGC->clientClipType)
        {
            case CT_NONE:
                rv = 1;
                break;
            case CT_REGION:
                rv = 2;
                RegionCopy(pRegion, pGC->pCompositeClip);
                break;
            default:
                rdpLog("unimp clip type %d\n", pGC->clientClipType);
                break;
        }

        if (rv == 2) /* check if the clip is the entire pixmap */
        {
            box.x1 = 0;
            box.y1 = 0;
            box.x2 = pDrawable->width;
            box.y2 = pDrawable->height;

            if (RegionContainsRect(pRegion, &box) == rgnIN)
            {
                rv = 1;
            }
        }
    }
    else if (pDrawable->type == DRAWABLE_WINDOW)
    {
        pWindow = (WindowPtr)pDrawable;

        if (pWindow->viewable)
        {
            if (pGC->subWindowMode == IncludeInferiors)
            {
                temp = &pWindow->borderClip;
            }
            else
            {
                temp = &pWindow->clipList;
            }

            if (RegionNotEmpty(temp))
            {
                switch (pGC->clientClipType)
                {
                    case CT_NONE:
                        rv = 2;
                        RegionCopy(pRegion, temp);
                        break;
                    case CT_REGION:
                        rv = 2;
                        RegionCopy(pRegion, pGC->clientClip);
                        RegionTranslate(pRegion,
                                        pDrawable->x + pGC->clipOrg.x,
                                        pDrawable->y + pGC->clipOrg.y);
                        RegionIntersect(pRegion, pRegion, temp);
                        break;
                    default:
                        rdpLog("unimp clip type %d\n", pGC->clientClipType);
                        break;
                }

                if (rv == 2) /* check if the clip is the entire screen */
                {
                    box.x1 = 0;
                    box.y1 = 0;
                    box.x2 = g_rdpScreen.width;
                    box.y2 = g_rdpScreen.height;

                    if (RegionContainsRect(pRegion, &box) == rgnIN)
                    {
                        rv = 1;
                    }
                }
            }
        }
    }

    return rv;
}

/******************************************************************************/
void
GetTextBoundingBox(DrawablePtr pDrawable, FontPtr font, int x, int y,
                   int n, BoxPtr pbox)
{
    int maxAscent;
    int maxDescent;
    int maxCharWidth;

    if (FONTASCENT(font) > FONTMAXBOUNDS(font, ascent))
    {
        maxAscent = FONTASCENT(font);
    }
    else
    {
        maxAscent = FONTMAXBOUNDS(font, ascent);
    }

    if (FONTDESCENT(font) > FONTMAXBOUNDS(font, descent))
    {
        maxDescent = FONTDESCENT(font);
    }
    else
    {
        maxDescent = FONTMAXBOUNDS(font, descent);
    }

    if (FONTMAXBOUNDS(font, rightSideBearing) >
            FONTMAXBOUNDS(font, characterWidth))
    {
        maxCharWidth = FONTMAXBOUNDS(font, rightSideBearing);
    }
    else
    {
        maxCharWidth = FONTMAXBOUNDS(font, characterWidth);
    }

    pbox->x1 = pDrawable->x + x;
    pbox->y1 = pDrawable->y + y - maxAscent;
    pbox->x2 = pbox->x1 + maxCharWidth * n;
    pbox->y2 = pbox->y1 + maxAscent + maxDescent;

    if (FONTMINBOUNDS(font, leftSideBearing) < 0)
    {
        pbox->x1 += FONTMINBOUNDS(font, leftSideBearing);
    }
}

/******************************************************************************/
#define GC_FUNC_PROLOGUE(_pGC) \
    { \
        priv = (rdpGCPtr)(dixGetPrivateAddr(&(_pGC->devPrivates), &g_rdpGCIndex)); \
        (_pGC)->funcs = priv->funcs; \
        if (priv->ops != 0) \
        { \
            (_pGC)->ops = priv->ops; \
        } \
    }

/******************************************************************************/
#define GC_FUNC_EPILOGUE(_pGC) \
    { \
        priv->funcs = (_pGC)->funcs; \
        (_pGC)->funcs = &g_rdpGCFuncs; \
        if (priv->ops != 0) \
        { \
            priv->ops = (_pGC)->ops; \
            (_pGC)->ops = &g_rdpGCOps; \
        } \
    }

/******************************************************************************/
static void
rdpValidateGC(GCPtr pGC, unsigned long changes, DrawablePtr d)
{
    rdpGCRec *priv;
    int wrap;
    RegionPtr pRegion;

    LLOGLN(10, ("rdpValidateGC:"));
    GC_FUNC_PROLOGUE(pGC);
    pGC->funcs->ValidateGC(pGC, changes, d);

    if (g_wrapPixmap)
    {
        wrap = 1;
    }
    else
    {
        wrap = (d->type == DRAWABLE_WINDOW) && ((WindowPtr)d)->viewable;

        if (wrap)
        {
            if (pGC->subWindowMode == IncludeInferiors)
            {
                pRegion = &(((WindowPtr)d)->borderClip);
            }
            else
            {
                pRegion = &(((WindowPtr)d)->clipList);
            }

            wrap = RegionNotEmpty(pRegion);
        }
    }

    priv->ops = 0;

    if (wrap)
    {
        priv->ops = pGC->ops;
    }

    GC_FUNC_EPILOGUE(pGC);
}

/******************************************************************************/
static void
rdpChangeGC(GCPtr pGC, unsigned long mask)
{
    rdpGCRec *priv;

    LLOGLN(10, ("in rdpChangeGC"));
    GC_FUNC_PROLOGUE(pGC);
    pGC->funcs->ChangeGC(pGC, mask);
    GC_FUNC_EPILOGUE(pGC);
}

/******************************************************************************/
static void
rdpCopyGC(GCPtr src, unsigned long mask, GCPtr dst)
{
    rdpGCRec *priv;

    LLOGLN(10, ("in rdpCopyGC"));
    GC_FUNC_PROLOGUE(dst);
    dst->funcs->CopyGC(src, mask, dst);
    GC_FUNC_EPILOGUE(dst);
}

/******************************************************************************/
static void
rdpDestroyGC(GCPtr pGC)
{
    rdpGCRec *priv;

    LLOGLN(10, ("in rdpDestroyGC"));
    GC_FUNC_PROLOGUE(pGC);
    pGC->funcs->DestroyGC(pGC);
    GC_FUNC_EPILOGUE(pGC);
}

/******************************************************************************/
static void
rdpChangeClip(GCPtr pGC, int type, pointer pValue, int nrects)
{
    rdpGCRec *priv;

    LLOGLN(10, ("in rdpChangeClip"));
    GC_FUNC_PROLOGUE(pGC);
    pGC->funcs->ChangeClip(pGC, type, pValue, nrects);
    GC_FUNC_EPILOGUE(pGC);
}

/******************************************************************************/
static void
rdpDestroyClip(GCPtr pGC)
{
    rdpGCRec *priv;

    LLOGLN(10, ("in rdpDestroyClip"));
    GC_FUNC_PROLOGUE(pGC);
    pGC->funcs->DestroyClip(pGC);
    GC_FUNC_EPILOGUE(pGC);
}

/******************************************************************************/
static void
rdpCopyClip(GCPtr dst, GCPtr src)
{
    rdpGCRec *priv;

    LLOGLN(0, ("in rdpCopyClip"));
    GC_FUNC_PROLOGUE(dst);
    dst->funcs->CopyClip(dst, src);
    GC_FUNC_EPILOGUE(dst);
}

/******************************************************************************/
#define GC_OP_PROLOGUE(_pGC) \
    { \
        priv = (rdpGCPtr)dixGetPrivateAddr(&(pGC->devPrivates), &g_rdpGCIndex); \
        oldFuncs = _pGC->funcs; \
        (_pGC)->funcs = priv->funcs; \
        (_pGC)->ops = priv->ops; \
    }

/******************************************************************************/
#define GC_OP_EPILOGUE(_pGC) \
    { \
        priv->ops = (_pGC)->ops; \
        (_pGC)->funcs = oldFuncs; \
        (_pGC)->ops = &g_rdpGCOps; \
    }

/******************************************************************************/
Bool
rdpCloseScreen(int i, ScreenPtr pScreen)
{
    LLOGLN(10, ("in rdpCloseScreen"));
    pScreen->CloseScreen = g_rdpScreen.CloseScreen;
    pScreen->CreateGC = g_rdpScreen.CreateGC;
    //pScreen->PaintWindowBackground = g_rdpScreen.PaintWindowBackground;
    //pScreen->PaintWindowBorder = g_rdpScreen.PaintWindowBorder;
    pScreen->CopyWindow = g_rdpScreen.CopyWindow;
    pScreen->ClearToBackground = g_rdpScreen.ClearToBackground;
    pScreen->RestoreAreas = g_rdpScreen.RestoreAreas;
    return 1;
}

/******************************************************************************/
int
draw_item_add(rdpPixmapRec *priv, struct rdp_draw_item *di)
{
    priv->is_alpha_dirty_not = 0;
    
    if (priv->draw_item_tail == 0)
    {
        priv->draw_item_tail = di;
        priv->draw_item_head = di;
    }
    else
    {
        di->prev = priv->draw_item_tail;
        priv->draw_item_tail->next = di;
        priv->draw_item_tail = di;
    }

    if (priv == &g_screenPriv)
    {
        rdpScheduleDeferredUpdate();
    }

    return 0;
}

/******************************************************************************/
int
draw_item_remove(rdpPixmapRec *priv, struct rdp_draw_item *di)
{
    if (di->prev != 0)
    {
        di->prev->next = di->next;
    }

    if (di->next != 0)
    {
        di->next->prev = di->prev;
    }

    if (priv->draw_item_head == di)
    {
        priv->draw_item_head = di->next;
    }

    if (priv->draw_item_tail == di)
    {
        priv->draw_item_tail = di->prev;
    }

    if (di->type == RDI_LINE)
    {
        if (di->u.line.segs != 0)
        {
            g_free(di->u.line.segs);
        }
    }
    
    if (di->type == RDI_TEXT)
    {
        delete_rdp_text(di->u.text.rtext);
    }

    RegionDestroy(di->reg);
    g_free(di);
    return 0;
}

/******************************************************************************/
int
draw_item_remove_all(rdpPixmapRec *priv)
{
    struct rdp_draw_item *di;

    di = priv->draw_item_head;

    while (di != 0)
    {
        draw_item_remove(priv, di);
        di = priv->draw_item_head;
    }

    return 0;
}

/******************************************************************************/
int
region_get_pixel_count(RegionPtr reg)
{
    int index;
    int count;
    int pixels;
    int width;
    int height;
    BoxRec box;
    
    pixels = 0;
    count = REGION_NUM_RECTS(reg);
    for (index = 0; index < count; index++)
    {
        box = REGION_RECTS(reg)[index];
        width = box.x2 - box.x1;
        height = box.y2 - box.y1;
        pixels += width * height;
    }
    return pixels;
}

/******************************************************************************/
/* returns boolean */
int
region_in_region(RegionPtr reg_small, int sreg_pcount, RegionPtr reg_big)
{
    int rv;
    RegionRec reg;
    
    rv = 0;
    RegionInit(&reg, NullBox, 0);
    RegionIntersect(&reg, reg_small, reg_big);
    if (sreg_pcount == -1)
    {
        sreg_pcount = region_get_pixel_count(reg_small);
    }
    if (sreg_pcount == 0)
    {
        /* empty region not even in */
        return 0;
    }
    if (region_get_pixel_count(&reg) == sreg_pcount)
    {
        rv = 1;
    }
    RegionUninit(&reg);
    return rv;
}

/******************************************************************************/
static int
remove_empties(rdpPixmapRec* priv)
{
    struct rdp_draw_item* di;
    struct rdp_draw_item* di_prev;
    int rv;
    
    rv = 0;
    /* remove draw items with empty regions */
    di = priv->draw_item_head;
    di_prev = 0;
    while (di != 0)
    {
        if (!RegionNotEmpty(di->reg))
        {
            LLOGLN(10, ("remove_empties: removing empty item type %d", di->type));
            draw_item_remove(priv, di);
            di = di_prev == 0 ? priv->draw_item_head : di_prev->next;
            rv++;
        }
        else
        {
            di_prev = di;
            di = di->next;
        }
    }
    return rv;
}

/******************************************************************************/
static int
dump_draw_list(rdpPixmapRec* priv)
{
    struct rdp_draw_item* di;
    int index;
    int count;
    BoxRec box;
    
    LLOGLN(0, ("dump_draw_list:"));
    di = priv->draw_item_head;
    while (di != 0)
    {
        LLOGLN(0, ("  type %d", di->type));
        count = REGION_NUM_RECTS(di->reg);
        if (count == 0)
        {
            LLOGLN(0, ("  empty region"));
        }
        else
        {
            box = RegionExtents(di->reg)[0];
            LLOGLN(0, ("  region list follows extents x1 %d y1 %d x2 %d y2 %d",
                       box.x1, box.y1, box.x2, box.y2));
            for (index = 0; index < count; index++)
            {
                box = REGION_RECTS(di->reg)[index];
                LLOGLN(0, ("    index %d x1 %d y1 %d x2 %d y2 %d",
                           index, box.x1, box.y1, box.x2, box.y2));
            }
        }
        di = di->next;
    }
    return 0;
}

/******************************************************************************/
/* returns boolean */
static int
region_intersect_at_all(RegionPtr reg_small, RegionPtr reg_big)
{
    int rv;
    RegionRec reg;
    
    if (!RegionNotEmpty(reg_small))
    {
        return 0;
    }
    rv = 0;
    RegionInit(&reg, NullBox, 0);
    RegionIntersect(&reg, reg_big, reg_big);
    if (RegionNotEmpty(&reg))
    {
        rv = 1;
    }
    RegionUninit(&reg);
    return rv;
}

/******************************************************************************/
int
draw_item_pack(PixmapPtr pix, rdpPixmapRec *priv)
{
    struct rdp_draw_item *di;
    struct rdp_draw_item *di_prev;
    BoxRec box;
    RegionRec treg;

#if 1
    if (pix != 0)
    {
        box.x1 = 0;
        box.x2 = pix->drawable.width;
        box.y1 = 0;
        box.y2 = pix->drawable.height;
        RegionInit(&treg, &box, 0);
        di = priv->draw_item_head;
        di_prev = 0;
        while (di != 0)
        {
            RegionIntersect(di->reg, di->reg, &treg);
            di_prev = di;
            di = di->next;
        }
        RegionUninit(&treg);
        remove_empties(priv);
    }
#endif

#if 1
    /* look for repeating draw types */
    if (priv->draw_item_head != 0)
    {
        if (priv->draw_item_head->next != 0)
        {
            di_prev = priv->draw_item_head;
            di = priv->draw_item_head->next;

            while (di != 0)
            {
#if 0
                if ((di_prev->type == RDI_IMGLL || di_prev->type == RDI_IMGLY) &&
                    (di->type == RDI_IMGLL || di->type == RDI_IMGLY))
                {
                    LLOGLN(10, ("draw_item_pack: packing RDI_IMGLL and RDI_IMGLY"));
                    di_prev->type = RDI_IMGLY;
                    RegionUnion(di_prev->reg, di_prev->reg, di->reg);
                              draw_item_remove(priv, di);
                              di = di_prev->next;
                }
#else
                if ((di_prev->type == RDI_IMGLL) && (di->type == RDI_IMGLL))
                {
                    LLOGLN(10, ("draw_item_pack: packing RDI_IMGLL"));
                    RegionUnion(di_prev->reg, di_prev->reg, di->reg);
                    draw_item_remove(priv, di);
                    di = di_prev->next;
                }
#endif
                else if ((di_prev->type == RDI_IMGLY) && (di->type == RDI_IMGLY))
                {
                    LLOGLN(10, ("draw_item_pack: packing RDI_IMGLY"));
                    RegionUnion(di_prev->reg, di_prev->reg, di->reg);
                    draw_item_remove(priv, di);
                    di = di_prev->next;
                }
                else
                {
                    di_prev = di;
                    di = di_prev->next;
                }
            }
        }
    }
    remove_empties(priv);
#endif

#if 0
    if (priv->draw_item_tail != 0)
    {
        if (priv->draw_item_tail->prev != 0)
        {
            di = priv->draw_item_tail;
            while (di->prev != 0)
            {
                di_prev = di->prev;
                while (di_prev != 0)
                {
                    if ((di->type == RDI_TEXT) && (di_prev->type == RDI_IMGLY))
                    {
                        if (region_intersect_at_all(di->reg, di_prev->reg))
                        {
                            di_prev->type = RDI_IMGLL;
                        }
                    }
                    di_prev = di_prev->prev;
                }
                di = di->prev;
            }
        }
    }
    remove_empties(priv);
#endif

#if 0
    /* subtract regions */
    if (priv->draw_item_tail != 0)
    {
        if (priv->draw_item_tail->prev != 0)
        {
            di = priv->draw_item_tail;

            while (di->prev != 0)
            {
                /* skip subtract flag
                 * draw items like line can't be used to clear(subtract) previous
                 * draw items since they are not opaque
                 * eg they can not be the 'S' in 'D = M - S'
                 * the region for line draw items is the clip region */
                if ((di->flags & 1) == 0)
                {
                    di_prev = di->prev;

                    while (di_prev != 0)
                    {
                        if (region_in_region(di_prev->reg, -1, di->reg))
                        {
                            /* empty region so this draw item will get removed below */
                            RegionEmpty(di_prev->reg);
                        }
                        di_prev = di_prev->prev;
                    }
                }

                di = di->prev;
            }
        }
    }
    remove_empties(priv);
#endif

    return 0;
}

static char g_strings[][32] =
{
    "Composite",         /* 0 */
    "CopyArea",          /* 1 */
    "PolyFillRect",      /* 2 */
    "PutImage",          /* 3 */
    "PolyRectangle",     /* 4 */
    "CopyPlane",         /* 5 */
    "PolyArc",           /* 6 */
    "FillPolygon",       /* 7 */
    "PolyFillArc",       /* 8 */
    "ImageText8",        /* 9 */
    "PolyText8",         /* 10 */
    "PolyText16",        /* 11 */
    "ImageText16",       /* 12 */
    "ImageGlyphBlt",     /* 13 */
    "PolyGlyphBlt",      /* 14 */
    "PushPixels",        /* 15 */
    "Other"
};

/******************************************************************************/
int
draw_item_add_img_region(rdpPixmapRec *priv, RegionPtr reg, int opcode,
                         int type, int code)
{
    struct rdp_draw_item *di;

    LLOGLN(10, ("draw_item_add_img_region: %s", g_strings[code]));
    di = (struct rdp_draw_item *)g_malloc(sizeof(struct rdp_draw_item), 1);
    di->type = type;
    di->reg = RegionCreate(NullBox, 0);
    RegionCopy(di->reg, reg);
    di->u.img.opcode = opcode;
    draw_item_add(priv, di);
    return 0;
}

/******************************************************************************/
int
draw_item_add_fill_region(rdpPixmapRec *priv, RegionPtr reg, int color,
                          int opcode)
{
    struct rdp_draw_item *di;

    LLOGLN(10, ("draw_item_add_fill_region:"));
    di = (struct rdp_draw_item *)g_malloc(sizeof(struct rdp_draw_item), 1);
    di->type = RDI_FILL;
    di->u.fill.fg_color = color;
    di->u.fill.opcode = opcode;
    di->reg = RegionCreate(NullBox, 0);
    RegionCopy(di->reg, reg);
    draw_item_add(priv, di);
    return 0;
}

/******************************************************************************/
int
draw_item_add_line_region(rdpPixmapRec *priv, RegionPtr reg, int color,
                          int opcode, int width, xSegment *segs, int nseg,
                          int is_segment)
{
    struct rdp_draw_item *di;

    LLOGLN(10, ("draw_item_add_line_region:"));
    di = (struct rdp_draw_item *)g_malloc(sizeof(struct rdp_draw_item), 1);
    di->type = RDI_LINE;
    di->u.line.fg_color = color;
    di->u.line.opcode = opcode;
    di->u.line.width = width;
    di->u.line.segs = (xSegment *)g_malloc(sizeof(xSegment) * nseg, 1);
    memcpy(di->u.line.segs, segs, sizeof(xSegment) * nseg);
    di->u.line.nseg = nseg;

    if (is_segment)
    {
        di->u.line.flags = 1;
    }

    di->reg = RegionCreate(NullBox, 0);
    di->flags |= 1;
    RegionCopy(di->reg, reg);
    draw_item_add(priv, di);
    return 0;
}

/******************************************************************************/
int
draw_item_add_srcblt_region(rdpPixmapRec *priv, RegionPtr reg,
                            int srcx, int srcy, int dstx, int dsty,
                            int cx, int cy)
{
    struct rdp_draw_item *di;

    LLOGLN(10, ("draw_item_add_srcblt_region:"));
    di = (struct rdp_draw_item *)g_malloc(sizeof(struct rdp_draw_item), 1);
    di->type = RDI_SCRBLT;
    di->u.scrblt.srcx = srcx;
    di->u.scrblt.srcy = srcy;
    di->u.scrblt.dstx = dstx;
    di->u.scrblt.dsty = dsty;
    di->u.scrblt.cx = cx;
    di->u.scrblt.cy = cy;
    di->reg = RegionCreate(NullBox, 0);
    RegionCopy(di->reg, reg);
    draw_item_add(priv, di);
    return 0;
}

/******************************************************************************/
int
draw_item_add_text_region(rdpPixmapRec* priv, RegionPtr reg, int color,
                          int opcode, struct rdp_text* rtext)
{
    struct rdp_draw_item* di;

    LLOGLN(10, ("draw_item_add_text_region:"));
    di = (struct rdp_draw_item*)g_malloc(sizeof(struct rdp_draw_item), 1);
    di->type = RDI_TEXT;
    di->u.text.fg_color = color;
    di->u.text.opcode = opcode;
    di->u.text.rtext = rtext;
    di->reg = RegionCreate(NullBox, 0);
    RegionCopy(di->reg, reg);
    draw_item_add(priv, di);
    return 0;
}

/******************************************************************************/
PixmapPtr
rdpCreatePixmap(ScreenPtr pScreen, int width, int height, int depth,
                unsigned usage_hint)
{
    PixmapPtr rv;
    rdpPixmapRec *priv;
    int org_width;

    org_width = width;
    /* width must be a multiple of 4 in rdp */
    width = (width + 3) & ~3;
    LLOGLN(10, ("rdpCreatePixmap: width %d org_width %d depth %d screen depth %d",
                width, org_width, depth, g_rdpScreen.depth));
    pScreen->CreatePixmap = g_rdpScreen.CreatePixmap;
    rv = pScreen->CreatePixmap(pScreen, width, height, depth, usage_hint);
    pScreen->CreatePixmap = rdpCreatePixmap;
    priv = GETPIXPRIV(rv);
    priv->rdpindex = -1;
    priv->kind_width = width;
    pScreen->ModifyPixmapHeader(rv, org_width, 0, 0, 0, 0, 0);
    if ((org_width == 0) && (height == 0))
    {
        LLOGLN(10, ("rdpCreatePixmap: setting is_scratch"));
        priv->is_scratch = 1;
    }
    return rv;
}

extern struct rdpup_os_bitmap *g_os_bitmaps;

/******************************************************************************/
Bool
rdpDestroyPixmap(PixmapPtr pPixmap)
{
    Bool rv;
    ScreenPtr pScreen;
    rdpPixmapRec *priv;

    LLOGLN(10, ("rdpDestroyPixmap:"));
    priv = GETPIXPRIV(pPixmap);
    LLOGLN(10, ("status %d refcnt %d", priv->status, pPixmap->refcnt));

    if (pPixmap->refcnt < 2)
    {
        if (XRDP_IS_OS(priv))
        {
            if (priv->rdpindex >= 0)
            {
                rdpup_remove_os_bitmap(priv->rdpindex);
                rdpup_delete_os_surface(priv->rdpindex);
            }
        }
    }

    pScreen = pPixmap->drawable.pScreen;
    pScreen->DestroyPixmap = g_rdpScreen.DestroyPixmap;
    rv = pScreen->DestroyPixmap(pPixmap);
    pScreen->DestroyPixmap = rdpDestroyPixmap;
    return rv;
}

/*****************************************************************************/
int
xrdp_is_os(PixmapPtr pix, rdpPixmapPtr priv)
{
    RegionRec reg1;
    BoxRec box;
    int width;
    int height;
    struct image_data id;

    if (XRDP_IS_OS(priv))
    {
        /* update time stamp */
        rdpup_update_os_use(priv->rdpindex);
    }
    else
    {
        width = pix->drawable.width;
        height = pix->drawable.height;
        if ((pix->usage_hint == 0) &&
            (pix->drawable.depth >= g_rdpScreen.depth) &&
            (width > 0) && (height > 0) && (priv->kind_width > 0) &&
            (priv->is_scratch == 0) && (priv->use_count >= 0))
        {
            width = (width + 3) & ~3;
            priv->rdpindex = rdpup_add_os_bitmap(pix, priv);
            if (priv->rdpindex >= 0)
            {
                priv->status = 1;
                rdpup_create_os_surface(priv->rdpindex, width, height);
                box.x1 = 0;
                box.y1 = 0;
                box.x2 = width;
                box.y2 = height;
                if (g_do_dirty_os)
                {
                    LLOGLN(10, ("xrdp_is_os: priv->con_number %d g_con_number %d",
                           priv->con_number, g_con_number));
                    LLOGLN(10, ("xrdp_is_os: priv->use_count %d", priv->use_count));
                    if (priv->con_number != g_con_number)
                    {
                        LLOGLN(10, ("xrdp_is_os: queuing invalidating all"));
                        draw_item_remove_all(priv);
                        RegionInit(&reg1, &box, 0);
                        draw_item_add_img_region(priv, &reg1, GXcopy, RDI_IMGLY, 16);
                        RegionUninit(&reg1);
                        priv->is_dirty = 1;
                        priv->con_number = g_con_number;
                    }
                }
                else
                {
                    rdpup_get_pixmap_image_rect(pix, &id);
                    rdpup_switch_os_surface(priv->rdpindex);
                    rdpup_begin_update();
                    rdpup_send_area(&id, box.x1, box.y1, box.x2 - box.x1,
                                    box.y2 - box.y1);
                    rdpup_end_update();
                    rdpup_switch_os_surface(-1);
                }
                priv->use_count++;
                return 1;
            }
            else
            {
                LLOGLN(10, ("xrdp_is_os: rdpup_add_os_bitmap failed"));
            }
        }
        priv->use_count++;
        return 0;
    }
    priv->use_count++;
    return 1;
}

/******************************************************************************/
Bool
rdpCreateWindow(WindowPtr pWindow)
{
    ScreenPtr pScreen;
    rdpWindowRec *priv;
    Bool rv;

    LLOGLN(10, ("rdpCreateWindow:"));
    priv = GETWINPRIV(pWindow);
    LLOGLN(10, ("  %p status %d", priv, priv->status));
    pScreen = pWindow->drawable.pScreen;
    pScreen->CreateWindow = g_rdpScreen.CreateWindow;
    rv = pScreen->CreateWindow(pWindow);
    pScreen->CreateWindow = rdpCreateWindow;

    if (g_use_rail)
    {
    }

    return rv;
}

/******************************************************************************/
Bool
rdpDestroyWindow(WindowPtr pWindow)
{
    ScreenPtr pScreen;
    rdpWindowRec *priv;
    Bool rv;

    LLOGLN(10, ("rdpDestroyWindow:"));
    priv = GETWINPRIV(pWindow);
    pScreen = pWindow->drawable.pScreen;
    pScreen->DestroyWindow = g_rdpScreen.DestroyWindow;
    rv = pScreen->DestroyWindow(pWindow);
    pScreen->DestroyWindow = rdpDestroyWindow;

    if (g_use_rail)
    {
#ifdef XRDP_WM_RDPUP
        LLOGLN(10, ("  rdpup_delete_window"));
        rdpup_delete_window(pWindow, priv);
#endif
    }

    return rv;
}

/******************************************************************************/
Bool
rdpPositionWindow(WindowPtr pWindow, int x, int y)
{
    ScreenPtr pScreen;
    rdpWindowRec *priv;
    Bool rv;

    LLOGLN(10, ("rdpPositionWindow:"));
    priv = GETWINPRIV(pWindow);
    pScreen = pWindow->drawable.pScreen;
    pScreen->PositionWindow = g_rdpScreen.PositionWindow;
    rv = pScreen->PositionWindow(pWindow, x, y);
    pScreen->PositionWindow = rdpPositionWindow;

    if (g_use_rail)
    {
        if (priv->status == 1)
        {
            LLOGLN(10, ("rdpPositionWindow:"));
            LLOGLN(10, ("  x %d y %d", x, y));
        }
    }

    return rv;
}

/******************************************************************************/
Bool
rdpRealizeWindow(WindowPtr pWindow)
{
    ScreenPtr pScreen;
    rdpWindowRec *priv;
    Bool rv;

    LLOGLN(10, ("rdpRealizeWindow:"));
    priv = GETWINPRIV(pWindow);
    pScreen = pWindow->drawable.pScreen;
    pScreen->RealizeWindow = g_rdpScreen.RealizeWindow;
    rv = pScreen->RealizeWindow(pWindow);
    pScreen->RealizeWindow = rdpRealizeWindow;

    if (g_use_rail)
    {
        if ((pWindow != g_invalidate_window) && (pWindow->parent != 0))
        {
            if (XR_IS_ROOT(pWindow->parent))
            {
                LLOGLN(10, ("rdpRealizeWindow:"));
                LLOGLN(10, ("  pWindow %p id 0x%x pWindow->parent %p id 0x%x x %d "
                            "y %d width %d height %d",
                            pWindow, (int)(pWindow->drawable.id),
                            pWindow->parent, (int)(pWindow->parent->drawable.id),
                            pWindow->drawable.x, pWindow->drawable.y,
                            pWindow->drawable.width, pWindow->drawable.height));
                priv->status = 1;
#ifdef XRDP_WM_RDPUP
                rdpup_create_window(pWindow, priv);
#endif
            }
        }
    }

    return rv;
}

/******************************************************************************/
Bool
rdpUnrealizeWindow(WindowPtr pWindow)
{
    ScreenPtr pScreen;
    rdpWindowRec *priv;
    Bool rv;

    LLOGLN(10, ("rdpUnrealizeWindow:"));
    priv = GETWINPRIV(pWindow);
    pScreen = pWindow->drawable.pScreen;
    pScreen->UnrealizeWindow = g_rdpScreen.UnrealizeWindow;
    rv = pScreen->UnrealizeWindow(pWindow);
    pScreen->UnrealizeWindow = rdpUnrealizeWindow;

    if (g_use_rail)
    {
        if (priv->status == 1)
        {
            LLOGLN(10, ("rdpUnrealizeWindow:"));
            priv->status = 0;
            if (pWindow->overrideRedirect) {
#ifdef XRDP_WM_RDPUP
                /*
                 * Popups are unmapped by X server, so probably
                 * they will be mapped again. Thereby we should
                 * just hide those popups instead of destroying
                 * them.
                 */
                LLOGLN(10, ("  rdpup_show_window"));
                rdpup_show_window(pWindow, priv, 0x0); /* 0x0 - do not show the window */
#endif
            }
        }
    }

    return rv;
}

/******************************************************************************/
Bool
rdpChangeWindowAttributes(WindowPtr pWindow, unsigned long mask)
{
    ScreenPtr pScreen;
    rdpWindowRec *priv;
    Bool rv;

    LLOGLN(10, ("rdpChangeWindowAttributes:"));
    priv = GETWINPRIV(pWindow);
    pScreen = pWindow->drawable.pScreen;
    pScreen->ChangeWindowAttributes = g_rdpScreen.ChangeWindowAttributes;
    rv = pScreen->ChangeWindowAttributes(pWindow, mask);
    pScreen->ChangeWindowAttributes = rdpChangeWindowAttributes;

    if (g_use_rail)
    {
    }

    return rv;
}

/******************************************************************************/
void
rdpWindowExposures(WindowPtr pWindow, RegionPtr pRegion, RegionPtr pBSRegion)
{
    ScreenPtr pScreen;
    rdpWindowRec *priv;

    LLOGLN(10, ("rdpWindowExposures:"));
    priv = GETWINPRIV(pWindow);
    pScreen = pWindow->drawable.pScreen;
    pScreen->WindowExposures = g_rdpScreen.WindowExposures;
    pScreen->WindowExposures(pWindow, pRegion, pBSRegion);

    if (g_use_rail)
    {
    }

    pScreen->WindowExposures = rdpWindowExposures;
}

/******************************************************************************/
Bool
rdpCreateGC(GCPtr pGC)
{
    rdpGCRec *priv;
    Bool rv;

    LLOGLN(10, ("in rdpCreateGC\n"));
    priv = GETGCPRIV(pGC);
    g_pScreen->CreateGC = g_rdpScreen.CreateGC;
    rv = g_pScreen->CreateGC(pGC);

    if (rv)
    {
        priv->funcs = pGC->funcs;
        priv->ops = 0;
        pGC->funcs = &g_rdpGCFuncs;
    }
    else
    {
        rdpLog("error in rdpCreateGC, CreateGC failed\n");
    }

    g_pScreen->CreateGC = rdpCreateGC;
    return rv;
}

/******************************************************************************/
void
rdpCopyWindow(WindowPtr pWin, DDXPointRec ptOldOrg, RegionPtr pOldRegion)
{
    RegionRec reg;
    RegionRec reg1;
    RegionRec clip;
    int dx;
    int dy;
    int i;
    int j;
    int num_clip_rects;
    int num_reg_rects;
    BoxRec box1;
    BoxRec box2;
    BoxPtr box3;

    LLOGLN(10, ("rdpCopyWindow:"));
    LLOGLN(10, ("rdpCopyWindow: new x %d new y %d old x %d old y %d",
           pWin->drawable.x, pWin->drawable.y, ptOldOrg.x, ptOldOrg.y));
    RegionInit(&reg, NullBox, 0);
    RegionCopy(&reg, pOldRegion);
    RegionInit(&clip, NullBox, 0);
    RegionCopy(&clip, &pWin->borderClip);
    dx = pWin->drawable.x - ptOldOrg.x;
    dy = pWin->drawable.y - ptOldOrg.y;

    if (g_do_dirty_ons)
    {
        rdpup_check_dirty_screen(&g_screenPriv);
    }

    g_pScreen->CopyWindow = g_rdpScreen.CopyWindow;
    g_pScreen->CopyWindow(pWin, ptOldOrg, pOldRegion);
    g_pScreen->CopyWindow = rdpCopyWindow;

    num_clip_rects = REGION_NUM_RECTS(&clip);
    num_reg_rects = REGION_NUM_RECTS(&reg);
    LLOGLN(10, ("rdpCopyWindow: num_clip_rects %d num_reg_rects %d",
           num_clip_rects, num_reg_rects));

    if ((num_clip_rects == 0) || (num_reg_rects == 0))
    {
        return;
    }
    rdpup_begin_update();

    /* when there is a huge list of screen copies, just send as bitmap
       firefox dragging test does this */
    if ((num_clip_rects > 16) && (num_reg_rects > 16))
    {
        box3 = RegionExtents(&reg);
        rdpup_send_area(0, box3->x1 + dx, box3->y1 + dy,
                        box3->x2 - box3->x1,
                        box3->y2 - box3->y1);
    }
    else
    {

        /* should maybe sort the rects instead of checking dy < 0 */
        /* If we can depend on the rects going from top to bottom, left
        to right we are ok */
        if (dy < 0 || (dy == 0 && dx < 0))
        {
            for (j = 0; j < num_clip_rects; j++)
            {
                box1 = REGION_RECTS(&clip)[j];
                LLOGLN(10, ("clip x %d y %d w %d h %d", box1.x1, box1.y1,
                       box1.x2 - box1.x1, box1.y2 - box1.y1));
                rdpup_set_clip(box1.x1, box1.y1,
                               box1.x2 - box1.x1,
                               box1.y2 - box1.y1);

                for (i = 0; i < num_reg_rects; i++)
                {
                    box2 = REGION_RECTS(&reg)[i];
                    LLOGLN(10, ("reg  x %d y %d w %d h %d", box2.x1, box2.y1,
                           box2.x2 - box2.x1, box2.y2 - box2.y1));
                    rdpup_screen_blt(box2.x1 + dx, box2.y1 + dy,
                                     box2.x2 - box2.x1,
                                     box2.y2 - box2.y1,
                                     box2.x1, box2.y1);
                }
            }
        }
        else
        {
            for (j = num_clip_rects - 1; j >= 0; j--)
            {
                box1 = REGION_RECTS(&clip)[j];
                LLOGLN(10, ("clip x %d y %d w %d h %d", box1.x1, box1.y1,
                       box1.x2 - box1.x1, box1.y2 - box1.y1));
                rdpup_set_clip(box1.x1, box1.y1,
                               box1.x2 - box1.x1,
                               box1.y2 - box1.y1);

                for (i = num_reg_rects - 1; i >= 0; i--)
                {
                    box2 = REGION_RECTS(&reg)[i];
                    LLOGLN(10, ("reg  x %d y %d w %d h %d", box2.x1, box2.y1,
                           box2.x2 - box2.x1, box2.y2 - box2.y1));
                    rdpup_screen_blt(box2.x1 + dx, box2.y1 + dy,
                                     box2.x2 - box2.x1,
                                     box2.y2 - box2.y1,
                                     box2.x1, box2.y1);
                }
            }
        }
    }

    rdpup_reset_clip();
    rdpup_end_update();

    RegionUninit(&reg);
    RegionUninit(&clip);
}

/******************************************************************************/
void
rdpClearToBackground(WindowPtr pWin, int x, int y, int w, int h,
                     Bool generateExposures)
{
    int j;
    BoxRec box;
    RegionRec reg;

    LLOGLN(10, ("in rdpClearToBackground"));
    g_pScreen->ClearToBackground = g_rdpScreen.ClearToBackground;
    g_pScreen->ClearToBackground(pWin, x, y, w, h, generateExposures);

    if (!generateExposures)
    {
        if (w > 0 && h > 0)
        {
            box.x1 = x;
            box.y1 = y;
            box.x2 = box.x1 + w;
            box.y2 = box.y1 + h;
        }
        else
        {
            box.x1 = pWin->drawable.x;
            box.y1 = pWin->drawable.y;
            box.x2 = box.x1 + pWin->drawable.width;
            box.y2 = box.y1 + pWin->drawable.height;
        }

        RegionInit(&reg, &box, 0);
        RegionIntersect(&reg, &reg, &pWin->clipList);

        if (g_do_dirty_ons)
        {
            draw_item_add_img_region(&g_screenPriv, &reg, GXcopy, RDI_IMGLY, 16);
        }
        else
        {
            rdpup_begin_update();

            for (j = REGION_NUM_RECTS(&reg) - 1; j >= 0; j--)
            {
                box = REGION_RECTS(&reg)[j];
                rdpup_send_area(0, box.x1, box.y1, box.x2 - box.x1, box.y2 - box.y1);
            }

            rdpup_end_update();
        }

        RegionUninit(&reg);
    }

    g_pScreen->ClearToBackground = rdpClearToBackground;
}

/******************************************************************************/
RegionPtr
rdpRestoreAreas(WindowPtr pWin, RegionPtr prgnExposed)
{
    RegionRec reg;
    RegionPtr rv;
    int j;
    BoxRec box;

    LLOGLN(0, ("in rdpRestoreAreas"));
    RegionInit(&reg, NullBox, 0);
    RegionCopy(&reg, prgnExposed);
    g_pScreen->RestoreAreas = g_rdpScreen.RestoreAreas;
    rv = g_pScreen->RestoreAreas(pWin, prgnExposed);

    if (g_do_dirty_ons)
    {
        draw_item_add_img_region(&g_screenPriv, &reg, GXcopy, RDI_IMGLY, 16);
    }
    else
    {
        rdpup_begin_update();

        for (j = REGION_NUM_RECTS(&reg) - 1; j >= 0; j--)
        {
            box = REGION_RECTS(&reg)[j];
            rdpup_send_area(0, box.x1, box.y1, box.x2 - box.x1, box.y2 - box.y1);
        }

        rdpup_end_update();
    }

    RegionUninit(&reg);
    g_pScreen->RestoreAreas = rdpRestoreAreas;
    return rv;
}

/******************************************************************************/
void
rdpInstallColormap(ColormapPtr pmap)
{
    ColormapPtr oldpmap;

    oldpmap = g_rdpInstalledColormap;

    if (pmap != oldpmap)
    {
        if (oldpmap != (ColormapPtr)None)
        {
            WalkTree(pmap->pScreen, TellLostMap, (char *)&oldpmap->mid);
        }

        /* Install pmap */
        g_rdpInstalledColormap = pmap;
        WalkTree(pmap->pScreen, TellGainedMap, (char *)&pmap->mid);
        /*rfbSetClientColourMaps(0, 0);*/
    }

    /*g_rdpScreen.InstallColormap(pmap);*/
}

/******************************************************************************/
void
rdpUninstallColormap(ColormapPtr pmap)
{
    ColormapPtr curpmap;

    curpmap = g_rdpInstalledColormap;

    if (pmap == curpmap)
    {
        if (pmap->mid != pmap->pScreen->defColormap)
        {
            //curpmap = (ColormapPtr)LookupIDByType(pmap->pScreen->defColormap,
            //                                      RT_COLORMAP);
            //pmap->pScreen->InstallColormap(curpmap);
        }
    }
}

/******************************************************************************/
int
rdpListInstalledColormaps(ScreenPtr pScreen, Colormap *pmaps)
{
    *pmaps = g_rdpInstalledColormap->mid;
    return 1;
}

/******************************************************************************/
void
rdpStoreColors(ColormapPtr pmap, int ndef, xColorItem *pdefs)
{
}

/******************************************************************************/
Bool
rdpSaveScreen(ScreenPtr pScreen, int on)
{
    return 1;
}
