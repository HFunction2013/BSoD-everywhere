/* Scanline rasterizer — software equivalent of RaspRasterize/RaspScanConvert.
 *
 * ntoskrnl scales each outline coordinate by size*96/(72*upem) into 26.6
 * fixed point (RaspScaleCoordinates) and then scan-converts into an 8-bit
 * coverage bitmap (RaspRasterize reads back "bits per pixel" as header>>2).
 * This implementation does exactly that, with 4 sub-scanlines per pixel row
 * and analytic horizontal coverage, which is the same integral up to
 * quantisation.
 */
#include "bsod.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define SUB 4          /* sub-scanlines per pixel row */

typedef struct { float x; int dir; } Cross;

static int cmp_cross(const void *a, const void *b) {
    float d = ((const Cross *)a)->x - ((const Cross *)b)->x;
    return d < 0 ? -1 : d > 0 ? 1 : 0;
}

int ras_glyph(TtFont *f, int gid, float em, Bitmap *bm) {
    Path p;
    float s, minx = 1e30f, maxx = -1e30f, miny = 1e30f, maxy = -1e30f;
    int x0, y0, w, h, i, k, row, sub, maxcross;
    Cross *cx;
    float *acc;

    memset(bm, 0, sizeof(*bm));
    if (em < 1.0f) em = 1.0f;
    if (!tt_path(f, gid, &p)) return 0;
    if (p.n == 0) { tt_path_free(&p); return 0; }

    s = em / (float)f->upem;
    for (i = 0; i < p.n; i++) {
        Loop *l = &p.lp[i];
        for (k = 0; k < l->n; k++) {
            float x = l->xy[2 * k], y = l->xy[2 * k + 1];
            if (x < minx) minx = x;
            if (x > maxx) maxx = x;
            if (y < miny) miny = y;
            if (y > maxy) maxy = y;
        }
    }

    /* y is up in font units, down in device space */
    x0 = (int)floorf(minx * s) - 1;
    w = (int)ceilf(maxx * s) + 1 - x0;
    y0 = (int)floorf(-maxy * s) - 1;
    h = (int)ceilf(-miny * s) + 1 - y0;
    if (w <= 0 || h <= 0 || w > 4096 || h > 4096) { tt_path_free(&p); return 0; }

    bm->px = (unsigned char *)calloc((size_t)w * (size_t)h, 1);
    if (!bm->px) { tt_path_free(&p); return 0; }
    bm->w = w; bm->h = h; bm->ox = x0; bm->oy = y0;

    maxcross = 0;
    for (i = 0; i < p.n; i++) maxcross += p.lp[i].n + 1;
    cx = (Cross *)malloc(sizeof(Cross) * (size_t)maxcross);
    acc = (float *)malloc(sizeof(float) * (size_t)w);
    if (!cx || !acc) { free(cx); free(acc); tt_path_free(&p); return 0; }

    for (row = 0; row < h; row++) {
        memset(acc, 0, sizeof(float) * (size_t)w);
        for (sub = 0; sub < SUB; sub++) {
            float sy = (float)(y0 + row) + ((float)sub + 0.5f) / (float)SUB;
            int nc = 0, wi = 0;

            for (i = 0; i < p.n; i++) {
                Loop *l = &p.lp[i];
                for (k = 0; k < l->n; k++) {
                    int j = (k + 1 == l->n) ? 0 : k + 1;
                    float ax = l->xy[2 * k] * s, ay = -(l->xy[2 * k + 1] * s);
                    float bx = l->xy[2 * j] * s, by = -(l->xy[2 * j + 1] * s);
                    if ((ay <= sy && by > sy) || (by <= sy && ay > sy)) {
                        float t = (sy - ay) / (by - ay);
                        cx[nc].x = ax + t * (bx - ax);
                        cx[nc].dir = (by > ay) ? 1 : -1;
                        nc++;
                    }
                }
            }
            if (nc < 2) continue;
            qsort(cx, (size_t)nc, sizeof(Cross), cmp_cross);

            for (i = 0; i < nc - 1; i++) {
                float a, b;
                int ia, ib, m;
                wi += cx[i].dir;
                if (wi == 0) continue;
                a = cx[i].x - (float)x0;
                b = cx[i + 1].x - (float)x0;
                if (a < 0.0f) a = 0.0f;
                if (b > (float)w) b = (float)w;
                if (b <= a) continue;
                ia = (int)floorf(a);
                ib = (int)floorf(b);
                if (ib >= w) ib = w - 1;
                if (ia == ib) {
                    acc[ia] += (b - a) / (float)SUB;
                } else {
                    acc[ia] += ((float)(ia + 1) - a) / (float)SUB;
                    for (m = ia + 1; m < ib; m++) acc[m] += 1.0f / (float)SUB;
                    if (ib >= ia + 1) acc[ib] += (b - (float)ib) / (float)SUB;
                }
            }
        }
        for (i = 0; i < w; i++) {
            float v = acc[i];
            int q;
            if (v <= 0.0f) continue;
            if (v > 1.0f) v = 1.0f;
            q = (int)(v * 255.0f + 0.5f);
            if (q > 255) q = 255;
            bm->px[(size_t)row * (size_t)w + (size_t)i] = (unsigned char)q;
        }
    }

    free(cx);
    free(acc);
    tt_path_free(&p);
    return 1;
}
