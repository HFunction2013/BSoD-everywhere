/* BSoD-everywhere — single-header public API
 * MIT License. See LICENSE.
 *
 * Everything here is dependency-free C11. The renderer is a pure software
 * pipeline: TrueType parse -> outline flatten -> scanline coverage -> blend.
 * That keeps the binary in the tens of KB and makes every platform produce
 * byte-identical pixels.
 */
#ifndef BSOD_H
#define BSOD_H

#include <stdint.h>

/* ---------------------------------------------------------------- TrueType */
typedef struct {
    unsigned char *buf;
    unsigned len;
    unsigned upem;          /* head.unitsPerEm            */
    int asc, desc;          /* hhea.ascender / descender  */
    unsigned nglyph;        /* maxp.numGlyphs             */
    unsigned nhm;           /* hhea.numberOfHMetrics      */
    int locfmt;             /* head.indexToLocFormat      */
    struct { unsigned tag, off, len; } tab[40];
    int ntab;
    const unsigned char *glyf, *loca, *hmtx, *cmap;
    unsigned glyf_len, loca_len, hmtx_len, cmap_len;
} TtFont;

int  tt_open(TtFont *f, const char *path);
void tt_close(TtFont *f);
int  tt_gid(TtFont *f, unsigned cp);        /* cmap lookup, 0 = none */
int  tt_adv(TtFont *f, int gid);            /* advance width, font units */

typedef struct { float *xy; int n, cap; } Loop;   /* x/y interleaved, closed */
typedef struct { Loop *lp; int n, cap; } Path;

int  tt_path(TtFont *f, int gid, Path *out); /* outline, font units, y up */
void tt_path_free(Path *p);

/* cell (line-box height) -> em, the core relation recovered from ntoskrnl:
 *   cell = em * (asc - desc) / upem   =>   em = cell * upem / (asc - desc)
 * For the Windows boot font that factor is 1/1.3298 = 0.7519.            */
float tt_em_from_cell(TtFont *f, float cell);
float tt_asc_px(TtFont *f, float em);

/* ------------------------------------------------------------ rasterizer */
typedef struct {
    unsigned char *px;   /* w*h coverage, 0..255 */
    int w, h;
    int ox, oy;          /* offset of px[0] from pen origin (y down) */
} Bitmap;

int  ras_glyph(TtFont *f, int gid, float em, Bitmap *bm);

/* ---------------------------------------------------------------- canvas  */
typedef struct { unsigned char *px; int w, h; } Canvas;   /* RGB888 */

void cv_init(Canvas *c, int w, int h);
void cv_free(Canvas *c);
void cv_clear(Canvas *c, unsigned rgb);
void cv_rect(Canvas *c, int x, int y, int w, int h, unsigned rgb);
void cv_blit(Canvas *c, const Bitmap *bm, int x, int y, unsigned rgb);

/* ---------------------------------------------------------------- text    */
float tx_width(TtFont *f, const char *s, float em);
void  tx_draw(Canvas *c, TtFont *f, const char *s, float em, int x, int base, unsigned rgb);
int   tx_px(TtFont *f, const char *s, float em);   /* width, rounded */
void  tx_reset_cache(void);   /* free cached glyph bitmaps between frames */

/* ---------------------------------------------------------------- screen  */
typedef struct {
    int w, h;                 /* target resolution                 */
    int progress;             /* 0..100                            */
    const char *stop_code;    /* default "CRITICAL_PROCESS_DIED"   */
    const char *driver;       /* "What failed:" line, optional     */
    const char *params[8];
    int nparams;
    int no_qr, dark, insider; /* variants                         */
    int dump;                 /* print the computed layout        */
    int fullscreen;           /* -f / --full-screen                */
    double complete_secs;     /* >0: total seconds to reach 100%   */
    double per_percent;       /* >0: seconds spent per 1%          */
    uint32_t stop_code_val;   /* raw hex code when stop_code_hex   */
    int stop_code_hex;        /* stop_code came from a hex literal */
} Opt;

int bsod_draw(Canvas *c, TtFont *f, const Opt *o);

/* ---------------------------------------------------------------- output  */
int out_ppm(const Canvas *c, const char *path);
int out_bmp(const Canvas *c, const char *path);
int out_write(const Canvas *c, const char *path);   /* dispatch on extension */

/* ---------------------------------------------------------------- platform */
/* Per-frame callback for animated backends. Returns 1 to keep running.
 * It redraws the canvas; the backend then blits it to the window. */
typedef int (*BsodTick)(Canvas *c, TtFont *f, Opt *o, void *ctx);

/* Paints the canvas and runs the event loop. `tick` may be NULL (one frame).
 * Returns 0 if no window backend is available. */
int plat_show(Canvas *c, TtFont *f, Opt *o, BsodTick tick, void *ctx);

/* Available screen work area in pixels (client area, no taskbar).
 * Returns 0,0 on platforms without a display. */
void plat_screen_size(int *w, int *h);

/* Full monitor size in pixels (covers the taskbar / work area). */
void plat_screen_full(int *w, int *h);

/* Monotonic wall-clock seconds since an arbitrary epoch. */
double plat_now_seconds(void);

#endif /* BSOD_H */
