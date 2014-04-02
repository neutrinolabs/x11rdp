/*
Copyright 2005-2014 Jay Sorg

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

#ifndef _RDP_H
#define _RDP_H

#include <xorg-server.h>
#include <scrnintstr.h>
#include <gcstruct.h>
#include <mipointer.h>
#include <randrstr.h>

#include "rdpPri.h"

#define COLOR8(r, g, b) \
    ((((r) >> 5) << 0)  | (((g) >> 5) << 3) | (((b) >> 6) << 6))
#define COLOR15(r, g, b) \
    ((((r) >> 3) << 10) | (((g) >> 3) << 5) | (((b) >> 3) << 0))
#define COLOR16(r, g, b) \
    ((((r) >> 3) << 11) | (((g) >> 2) << 5) | (((b) >> 3) << 0))
#define COLOR24(r, g, b) \
    ((((r) >> 0) << 0)  | (((g) >> 0) << 8) | (((b) >> 0) << 16))
#define SPLITCOLOR32(r, g, b, c) \
    do { \
        r = ((c) >> 16) & 0xff; \
        g = ((c) >> 8) & 0xff; \
        b = (c) & 0xff; \
    } while (0)

/* PIXMAN_a8b8g8r8 */
#define XRDP_a8b8g8r8 \
((32 << 24) | (3 << 16) | (8 << 12) | (8 << 8) | (8 << 4) | 8)
/* PIXMAN_a8r8g8b8 */
#define XRDP_a8r8g8b8 \
((32 << 24) | (2 << 16) | (8 << 12) | (8 << 8) | (8 << 4) | 8)
/* PIXMAN_r5g6b5 */
#define XRDP_r5g6b5 \
((16 << 24) | (2 << 16) | (0 << 12) | (5 << 8) | (6 << 4) | 5)
/* PIXMAN_a1r5g5b5 */
#define XRDP_a1r5g5b5 \
((16 << 24) | (2 << 16) | (1 << 12) | (5 << 8) | (5 << 4) | 5)
/* PIXMAN_r3g3b2 */
#define XRDP_r3g3b2 \
((8 << 24) | (2 << 16) | (0 << 12) | (3 << 8) | (3 << 4) | 2)

#define PixelDPI 100
#define PixelToMM(_size) (((_size) * 254 + (PixelDPI) * 5) / ((PixelDPI) * 10))

#define RDPMIN(_val1, _val2) ((_val1) < (_val2) ? (_val1) : (_val2))
#define RDPMAX(_val1, _val2) ((_val1) < (_val2) ? (_val2) : (_val1))
#define RDPCLAMP(_val, _lo, _hi) \
  (_val) < (_lo) ? (_lo) : (_val) > (_hi) ? (_hi) : (_val)

#define XRDP_CD_NODRAW 0
#define XRDP_CD_NOCLIP 1
#define XRDP_CD_CLIP   2

#if 0
#define RegionCopy DONOTUSE
#define RegionTranslate DONOTUSE
#define RegionNotEmpty DONOTUSE
#define RegionIntersect DONOTUSE
#define RegionContainsRect DONOTUSE
#define RegionInit DONOTUSE
#define RegionUninit DONOTUSE
#define RegionFromRects DONOTUSE
#define RegionDestroy DONOTUSE
#define RegionCreate DONOTUSE
#define RegionUnion DONOTUSE
#define RegionSubtract DONOTUSE
#define RegionInverse DONOTUSE
#define RegionExtents DONOTUSE
#define RegionReset DONOTUSE
#define RegionBreak DONOTUSE
#define RegionUnionRect DONOTUSE
#endif

struct image_data
{
    int width;
    int height;
    int bpp;
    int Bpp;
    int lineBytes;
    char *pixels;
    char *shmem_pixels;
    int shmem_id;
    int shmem_offset;
    int shmem_lineBytes;
};

/* defined in rdpClientCon.h */
typedef struct _rdpClientCon rdpClientCon;

struct _rdpPointer
{
    int cursor_x;
    int cursor_y;
    int old_button_mask;
    int button_mask;
    DeviceIntPtr device;
};
typedef struct _rdpPointer rdpPointer;

struct _rdpKeyboard
{
    int pause_spe;
    int ctrl_down;
    int alt_down;
    int shift_down;
    int tab_down;
    /* this is toggled every time num lock key is released, not like the
       above *_down vars */
    int scroll_lock_down;
    DeviceIntPtr device;
};
typedef struct _rdpKeyboard rdpKeyboard;


struct _rdpPixmapRec
{
    int status;
    int rdpindex;
    int con_number;
    int is_dirty;
    int is_scratch;
    int is_alpha_dirty_not;
    /* number of times used in a remote operation
       if this gets above XRDP_USE_COUNT_THRESHOLD
       then we force remote the pixmap */
    int use_count;
    int kind_width;
    struct rdp_draw_item *draw_item_head;
    struct rdp_draw_item *draw_item_tail;
};
typedef struct _rdpPixmapRec rdpPixmapRec;
typedef struct _rdpPixmapRec * rdpPixmapPtr;
#define GETPIXPRIV(_dev, _pPixmap) (rdpPixmapPtr) \
rdpGetPixmapPrivate(&((_pPixmap)->devPrivates),  (_dev)->privateKeyRecPixmap)

struct _rdpCounts
{
    CARD32 rdpFillSpansCallCount; /* 1 */
    CARD32 rdpSetSpansCallCount;
    CARD32 rdpPutImageCallCount;
    CARD32 rdpCopyAreaCallCount;
    CARD32 rdpCopyPlaneCallCount;
    CARD32 rdpPolyPointCallCount;
    CARD32 rdpPolylinesCallCount;
    CARD32 rdpPolySegmentCallCount;
    CARD32 rdpPolyRectangleCallCount;
    CARD32 rdpPolyArcCallCount; /* 10 */
    CARD32 rdpFillPolygonCallCount;
    CARD32 rdpPolyFillRectCallCount;
    CARD32 rdpPolyFillArcCallCount;
    CARD32 rdpPolyText8CallCount;
    CARD32 rdpPolyText16CallCount;
    CARD32 rdpImageText8CallCount;
    CARD32 rdpImageText16CallCount;
    CARD32 rdpImageGlyphBltCallCount;
    CARD32 rdpPolyGlyphBltCallCount;
    CARD32 rdpPushPixelsCallCount; /* 20 */
    CARD32 rdpCompositeCallCount;
    CARD32 rdpCopyWindowCallCount; /* 22 */
    CARD32 rdpTrapezoidsCallCount;
    CARD32 callCount[64 - 23];
};

/* move this to common header */
struct _rdpRec
{
    int width;
    int height;
    int depth;
    int paddedWidthInBytes;
    int sizeInBytes;
    int num_modes;
    int bitsPerPixel;
    int Bpp;
    int Bpp_mask;
    char *pfbMemory;
    ScreenPtr pScreen;
    rdpDevPrivateKey privateKeyRecGC;
    rdpDevPrivateKey privateKeyRecPixmap;

    CopyWindowProcPtr CopyWindow;
    CreateGCProcPtr CreateGC;
    CreatePixmapProcPtr CreatePixmap;
    DestroyPixmapProcPtr DestroyPixmap;
    ModifyPixmapHeaderProcPtr ModifyPixmapHeader;
    CloseScreenProcPtr CloseScreen;
    CompositeProcPtr Composite;
    GlyphsProcPtr Glyphs;
    TrapezoidsProcPtr Trapezoids;

    /* keyboard and mouse */
    miPointerScreenFuncPtr pCursorFuncs;
    /* mouse */
    rdpPointer pointer;
    /* keyboard */
    rdpKeyboard keyboard;

    /* RandR */
    RRSetConfigProcPtr rrSetConfig;
    RRGetInfoProcPtr rrGetInfo;
    RRScreenSetSizeProcPtr rrScreenSetSize;
    RRCrtcSetProcPtr rrCrtcSet;
    RRCrtcSetGammaProcPtr rrCrtcSetGamma;
    RRCrtcGetGammaProcPtr rrCrtcGetGamma;
    RROutputSetPropertyProcPtr rrOutputSetProperty;
    RROutputValidateModeProcPtr rrOutputValidateMode;
    RRModeDestroyProcPtr rrModeDestroy;
    RROutputGetPropertyProcPtr rrOutputGetProperty;
    RRGetPanningProcPtr rrGetPanning;
    RRSetPanningProcPtr rrSetPanning;

    int listen_sck;
    char uds_data[256];
    rdpClientCon *clientConHead;
    rdpClientCon *clientConTail;

    rdpPixmapRec screenPriv;
    int sendUpdateScheduled; /* boolean */
    OsTimerPtr sendUpdateTimer;

    int do_dirty_os; /* boolean */
    int do_dirty_ons; /* boolean */
    int disconnect_scheduled; /* boolean */
    int do_kill_disconnected; /* boolean */

    OsTimerPtr disconnectTimer;
    int disconnectScheduled; /* boolean */
    int disconnect_timeout_s;
    int disconnect_time_ms;

    int conNumber;

    struct _rdpCounts counts;

};
typedef struct _rdpRec rdpRec;
typedef struct _rdpRec * rdpPtr;
#define XRDPPTR(_p) ((rdpPtr)((_p)->driverPrivate))

struct _rdpGCRec
{
    GCFuncs *funcs;
    GCOps *ops;
};
typedef struct _rdpGCRec rdpGCRec;
typedef struct _rdpGCRec * rdpGCPtr;

#define RDI_FILL 1
#define RDI_IMGLL 2 /* lossless */
#define RDI_IMGLY 3 /* lossy */
#define RDI_LINE 4
#define RDI_SCRBLT 5
#define RDI_TEXT 6

struct urdp_draw_item_fill
{
  int opcode;
  int fg_color;
  int bg_color;
  int pad0;
};

struct urdp_draw_item_img
{
  int opcode;
  int pad0;
};

struct urdp_draw_item_line
{
  int opcode;
  int fg_color;
  int bg_color;
  int width;
  xSegment* segs;
  int nseg;
  int flags;
};

struct urdp_draw_item_scrblt
{
  int srcx;
  int srcy;
  int dstx;
  int dsty;
  int cx;
  int cy;
};

struct urdp_draw_item_text
{
  int opcode;
  int fg_color;
  struct rdp_text* rtext; /* in rdpglyph.h */
};

union urdp_draw_item
{
  struct urdp_draw_item_fill fill;
  struct urdp_draw_item_img img;
  struct urdp_draw_item_line line;
  struct urdp_draw_item_scrblt scrblt;
  struct urdp_draw_item_text text;
};

struct rdp_draw_item
{
  int type; /* RDI_FILL, RDI_IMGLL, ... */
  int flags;
  struct rdp_draw_item* prev;
  struct rdp_draw_item* next;
  RegionPtr reg;
  union urdp_draw_item u;
};

#define XRDP_USE_COUNT_THRESHOLD 1
#endif
