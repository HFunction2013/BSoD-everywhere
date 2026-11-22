/* TrueType/OpenType reader — the equivalent of ntoskrnl FopLayer/Rasp loaders.
 *
 * Only what the bugcheck screen needs: head, hhea, maxp, hmtx, loca, glyf,
 * cmap(4/12). No hinting: BGP's rasterizer (RaspScaleCoordinates) scales
 * outlines by size*96/72/upem and then scan-converts; we do the same, so the
 * byte layout matches and only sub-pixel hinting differs (see docs/ras.md).
 */
#include "bsod.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* sfnt/TrueType is big-endian */
static unsigned g16(const unsigned char *p) { return ((unsigned)p[0] << 8) | (unsigned)p[1]; }
static int      gs16(const unsigned char *p) { return (int)(short)(((unsigned)p[0] << 8) | (unsigned)p[1]); }
static unsigned g32(const unsigned char *p) {
    return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) | ((unsigned)p[2] << 8) | (unsigned)p[3];
}
static unsigned tag4(const char *s) {
    return ((unsigned)(unsigned char)s[0] << 24) | ((unsigned)(unsigned char)s[1] << 16) |
           ((unsigned)(unsigned char)s[2] << 8) | (unsigned)(unsigned char)s[3];
}

static const unsigned char *find(TtFont *f, const char *tag, unsigned *len) {
    unsigned t = tag4(tag);
    int i;
    for (i = 0; i < f->ntab; i++)
        if (f->tab[i].tag == t) {
            if (len) *len = f->tab[i].len;
            if (f->tab[i].off >= f->len) return 0;
            return f->buf + f->tab[i].off;
        }
    return 0;
}

int tt_open(TtFont *f, const char *path) {
    FILE *fp = fopen(path, "rb");
    long sz;
    unsigned ot = 0, n, i;

    memset(f, 0, sizeof(*f));
    if (!fp) return 0;
    fseek(fp, 0, SEEK_END);
    sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz <= 0 || sz > (long)64 << 20) { fclose(fp); return 0; }
    f->buf = (unsigned char *)malloc((size_t)sz);
    if (!f->buf) { fclose(fp); return 0; }
    if (fread(f->buf, 1, (size_t)sz, fp) != (size_t)sz) { fclose(fp); free(f->buf); f->buf = 0; return 0; }
    fclose(fp);

    f->len = (unsigned)sz;
    if (f->len < 12) return 0;

    /* TrueType Collection: BGP receives a 'ttcf' blob (font + qr bitmap share
     * the loader blob). Pick sub-font 0, which is the boot UI face. */
    if (g32(f->buf) == tag4("ttcf")) {
        if (f->len < 16) return 0;
        ot = g32(f->buf + 12);
        if (ot + 12 > f->len) return 0;
    } else if (g32(f->buf) != tag4("OTTO") && g32(f->buf) != 0x00010000u && g32(f->buf) != tag4("true")) {
        return 0;   /* not a sfnt */
    }

    n = g16(f->buf + ot + 4);
    if (n == 0 || n > 40 || ot + 12 + n * 16 > f->len) return 0;
    for (i = 0; i < n; i++) {
        const unsigned char *rec = f->buf + ot + 12 + i * 16;
        f->tab[i].tag = g32(rec);
        f->tab[i].off = g32(rec + 8);
        f->tab[i].len = g32(rec + 12);
    }
    f->ntab = (int)n;

    {
        const unsigned char *head = find(f, "head", 0);
        const unsigned char *hhea = find(f, "hhea", 0);
        const unsigned char *maxp = find(f, "maxp", 0);

        if (!head || !hhea || !maxp) return 0;
        f->upem = g16(head + 18);
        f->locfmt = gs16(head + 50);
        f->asc = gs16(hhea + 4);
        f->desc = gs16(hhea + 6);
        f->nhm = g16(hhea + 34);
        f->nglyph = g16(maxp + 4);

        f->glyf = find(f, "glyf", &f->glyf_len);
        f->loca = find(f, "loca", &f->loca_len);
        f->hmtx = find(f, "hmtx", &f->hmtx_len);
        f->cmap = find(f, "cmap", &f->cmap_len);
    }
    if (!f->upem || !f->nglyph) return 0;
    return 1;
}

void tt_close(TtFont *f) {
    if (f->buf) free(f->buf);
    f->buf = 0;
    f->len = 0;
}

float tt_em_from_cell(TtFont *f, float cell) {
    int span = f->asc - f->desc;
    if (span <= 0) span = (int)f->upem;
    return cell * (float)f->upem / (float)span;
}

float tt_asc_px(TtFont *f, float em) {
    return em * (float)f->asc / (float)f->upem;
}

/* ---------------------------------------------------------------- cmap */
static int cmap_lookup4(const unsigned char *t, unsigned n, unsigned cp) {
    unsigned segX2, i;
    if (n < 16 || g16(t) != 4) return 0;
    segX2 = g16(t + 6);
    for (i = 0; i < segX2; i += 2) {
        unsigned end = g16(t + 14 + i);
        if (cp > end) continue;
        {
            unsigned start = g16(t + 16 + segX2 + i);
            unsigned delta = g16(t + 16 + segX2 * 2 + i);
            unsigned ro = g16(t + 16 + segX2 * 3 + i);
            unsigned gid = 0;
            if (cp < start) return 0;
            if (ro == 0) {
                gid = (cp + delta) & 0xFFFF;
            } else {
                const unsigned char *p = t + 16 + segX2 * 3 + i + ro + (cp - start) * 2;
                if ((unsigned)(p - t) + 2 > n) return 0;
                gid = g16(p);
                if (gid) gid = (gid + delta) & 0xFFFF;
            }
            return (int)gid;
        }
    }
    return 0;
}

static int cmap_lookup12(const unsigned char *t, unsigned n, unsigned cp) {
    unsigned ngroups, i;
    if (n < 16 || g16(t) != 12) return 0;
    ngroups = g32(t + 12);
    for (i = 0; i < ngroups; i++) {
        const unsigned char *g = t + 16 + i * 12;
        unsigned s, e;
        if ((unsigned)(g - t) + 12 > n) break;
        s = g32(g);
        e = g32(g + 4);
        if (cp >= s && cp <= e) return (int)(g32(g + 8) + (cp - s));
    }
    return 0;
}

int tt_gid(TtFont *f, unsigned cp) {
    unsigned n, i;
    const unsigned char *t;
    int best = -1, best_score = -1;

    if (!f->cmap) return 0;
    n = g16(f->cmap + 2);
    for (i = 0; i < n; i++) {
        const unsigned char *rec = f->cmap + 4 + i * 8;
        unsigned pid = g16(rec), eid = g16(rec + 2), off = g32(rec + 4);
        const unsigned char *sub = f->cmap + off;
        int score = -1;
        unsigned fmt;

        if (off + 4 > f->cmap_len) continue;
        fmt = g16(sub);
        if (pid == 3 && eid == 1 && fmt == 4) score = 100;      /* BMP, Windows */
        else if (pid == 3 && eid == 10 && fmt == 12) score = 90;
        else if (pid == 0 && (eid == 3 || eid == 4) && fmt == 4) score = 80;
        else if (pid == 0 && (eid == 6 || eid == 4) && fmt == 12) score = 70;
        if (score > best_score) { best_score = score; best = (int)off; }
    }
    if (best < 0) return 0;
    t = f->cmap + best;
    return g16(t) == 12 ? cmap_lookup12(t, f->cmap_len - (unsigned)best, cp)
                        : cmap_lookup4(t, f->cmap_len - (unsigned)best, cp);
}

int tt_adv(TtFont *f, int gid) {
    if (gid < 0) return 0;
    if (!f->hmtx || f->hmtx_len < 4) return (int)f->upem / 2;
    if ((unsigned)gid < f->nhm) return g16(f->hmtx + (unsigned)gid * 4);
    if (f->nhm == 0) return (int)f->upem / 2;
    return g16(f->hmtx + (f->nhm - 1) * 4);
}

/* ---------------------------------------------------------------- outlines */
static void loop_push(Loop *l, float x, float y) {
    if (l->n >= l->cap) {
        l->cap = l->cap * 2 + 8;
        l->xy = (float *)realloc(l->xy, sizeof(float) * 2 * (size_t)l->cap);
        if (!l->xy) { l->n = 0; return; }
    }
    l->xy[2 * l->n] = x;
    l->xy[2 * l->n + 1] = y;
    l->n++;
}

static void path_push_loop(Path *p, Loop *l) {
    if (l->n < 3) { free(l->xy); l->xy = 0; l->n = 0; return; }
    if (p->n >= p->cap) {
        p->cap = p->cap * 2 + 4;
        p->lp = (Loop *)realloc(p->lp, sizeof(Loop) * (size_t)p->cap);
        if (!p->lp) return;
    }
    p->lp[p->n++] = *l;
    l->xy = 0; l->n = l->cap = 0;
}

/* quadratic bezier, flat enough for screen sizes: segment count from length */
static void quad(Loop *l, float x0, float y0, float cx, float cy, float x1, float y1) {
    float len = fabsf(cx - x0) + fabsf(cy - y0) + fabsf(x1 - cx) + fabsf(y1 - cy);
    int n = 2 + (int)(len / 6.0f), i;
    if (n < 3) n = 3;
    if (n > 24) n = 24;
    for (i = 1; i <= n; i++) {
        float t = (float)i / (float)n, u = 1.0f - t;
        loop_push(l, u * u * x0 + 2 * u * t * cx + t * t * x1,
                     u * u * y0 + 2 * u * t * cy + t * t * y1);
    }
}

static void flatten(Loop *l, const float *x, const float *y, const unsigned char *on, int n) {
    int start = -1, i, guard = 0;
    float px, py;

    for (i = 0; i < n; i++) if (on[i]) { start = i; break; }
    if (start < 0) {                      /* all-off contour: rare, skip curve bits */
        for (i = 0; i < n; i++) loop_push(l, x[i], y[i]);
        return;
    }
    px = x[start];
    py = y[start];
    loop_push(l, px, py);
    i = start;
    while (guard++ < n + 4) {
        int a = (i + 1) % n;
        if (on[a]) {
            loop_push(l, x[a], y[a]);
            px = x[a]; py = y[a]; i = a;
        } else {
            int b = (a + 1) % n;
            if (on[b]) {
                quad(l, px, py, x[a], y[a], x[b], y[b]);
                px = x[b]; py = y[b]; i = b;
            } else {
                float mx = (x[a] + x[b]) * 0.5f, my = (y[a] + y[b]) * 0.5f;
                quad(l, px, py, x[a], y[a], mx, my);
                px = mx; py = my; i = a;
            }
        }
        if (i == start) break;
    }
}

static int load_glyph(TtFont *f, int gid, Path *out, int depth);

static int load_simple(TtFont *f, const unsigned char *g, unsigned glen, Path *out) {
    int nc = gs16(g);
    const unsigned char *p = g + 10;
    unsigned *endpts, i;
    int inst;
    int npts, np;

    if (nc <= 0 || (unsigned)(p - g) + (size_t)nc * 2 > glen) return 0;
    endpts = (unsigned *)malloc(sizeof(unsigned) * (size_t)nc);
    if (!endpts) return 0;
    for (i = 0; i < (unsigned)nc; i++) endpts[i] = g16(p + i * 2);
    npts = (int)endpts[nc - 1] + 1;
    if (npts <= 0 || npts > 65535) { free(endpts); return 0; }
    p += nc * 2;
    inst = gs16(p); p += 2;
    if (inst < 0) { free(endpts); return 0; }
    p += inst;

    /* first pass: flags RLE */
    {
        unsigned char *fl = (unsigned char *)malloc((size_t)npts);
        int k = 0;
        if (!fl) { free(endpts); return 0; }
        while (k < npts) {
            unsigned char v = *p++;
            fl[k++] = v;
            if (v & 8) {
                int rep = *p++;
                while (rep-- > 0 && k < npts) fl[k++] = v;
            }
        }
        p = p;   /* p now past flags */
        /* x coords */
        {
            float *xs = (float *)malloc(sizeof(float) * (size_t)npts);
            float *ys = (float *)malloc(sizeof(float) * (size_t)npts);
            unsigned char *on = (unsigned char *)malloc((size_t)npts);
            int v = 0;
            if (!xs || !ys || !on) { free(xs); free(ys); free(on); free(fl); free(endpts); return 0; }
            for (k = 0; k < npts; k++) {
                if (fl[k] & 2) {
                    int d = *p++;
                    v += (fl[k] & 16) ? d : -d;
                } else if (!(fl[k] & 16)) {
                    v += gs16(p); p += 2;
                }
                xs[k] = (float)v;
            }
            v = 0;
            for (k = 0; k < npts; k++) {
                if (fl[k] & 4) {
                    int d = *p++;
                    v += (fl[k] & 32) ? d : -d;
                } else if (!(fl[k] & 32)) {
                    v += gs16(p); p += 2;
                }
                ys[k] = (float)v;
            }
            for (k = 0; k < npts; k++) on[k] = (fl[k] & 1) ? 1 : 0;

            {
                int s = 0;
                for (i = 0; i < (unsigned)nc; i++) {
                    int e = (int)endpts[i];
                    int cnt = e - s + 1;
                    Loop l;
                    memset(&l, 0, sizeof(l));
                    if (cnt >= 3) flatten(&l, xs + s, ys + s, on + s, cnt);
                    path_push_loop(out, &l);
                    s = e + 1;
                }
            }
            free(xs); free(ys); free(on);
        }
        free(fl);
    }
    free(endpts);
    (void)np;
    return 1;
}

static int load_composite(TtFont *f, const unsigned char *g, unsigned glen, Path *out, int depth) {
    const unsigned char *p = g + 10;
    int more = 1;

    if (depth > 4) return 0;
    while (more) {
        unsigned flags, gi;
        int arg1, arg2, dx, dy;
        float a = 1, b = 0, c = 0, d = 1, e = 0, fv = 0;
        Path sub = {0};
        int k;

        if ((unsigned)(p - g) + 4 > glen) return 0;
        flags = g16(p); gi = g16(p + 2); p += 4;

        if (flags & 1) { arg1 = gs16(p); arg2 = gs16(p + 2); p += 4; }
        else { arg1 = (int)(signed char)p[0]; arg2 = (int)(signed char)p[1]; p += 2; }

        dx = (flags & 2) ? arg1 : 0;   /* ARGS_ARE_XY_VALUES */
        dy = (flags & 2) ? arg2 : 0;

        if (flags & 8) { e = (float)gs16(p); fv = (float)gs16(p + 2); p += 4; }
        if (flags & 0x40) { a = d = (float)gs16(p) / 16384.0f; p += 2; }
        else if (flags & 0x80) {
            a = (float)gs16(p) / 16384.0f; d = (float)gs16(p + 2) / 16384.0f; p += 4;
        } else if (flags & 0x100) {
            a = (float)gs16(p) / 16384.0f; b = (float)gs16(p + 2) / 16384.0f;
            c = (float)gs16(p + 4) / 16384.0f; d = (float)gs16(p + 6) / 16384.0f; p += 8;
        }

        if (load_glyph(f, (int)gi, &sub, depth + 1)) {
            for (k = 0; k < sub.n; k++) {
                Loop *sl = &sub.lp[k];
                int j;
                for (j = 0; j < sl->n; j++) {
                    float x = sl->xy[2 * j], y = sl->xy[2 * j + 1];
                    sl->xy[2 * j] = a * x + c * y + (flags & 8 ? e : (float)dx);
                    sl->xy[2 * j + 1] = b * x + d * y + (flags & 8 ? fv : (float)dy);
                }
                path_push_loop(out, sl);
                sl->xy = 0;
            }
            free(sub.lp);
        }
        more = (flags & 0x20) ? 1 : 0;
    }
    return out->n > 0;
}

static int load_glyph(TtFont *f, int gid, Path *out, int depth) {
    unsigned off, next, i;

    if (!f->loca || !f->glyf) return 0;
    if (gid < 0 || (unsigned)gid >= f->nglyph) return 0;
    i = (unsigned)gid;
    if (f->locfmt == 0) {
        if (f->loca_len < (i + 2) * 2) return 0;
        off = g16(f->loca + i * 2) * 2;
        next = g16(f->loca + (i + 1) * 2) * 2;
    } else {
        if (f->loca_len < (i + 2) * 4) return 0;
        off = g32(f->loca + i * 4);
        next = g32(f->loca + (i + 1) * 4);
    }
    if (next <= off || next > f->glyf_len) return 0;

    {
        const unsigned char *g = f->glyf + off;
        unsigned glen = next - off;
        int nc;
        if (glen < 10) return 0;
        nc = gs16(g);
        if (nc >= 0) return load_simple(f, g, glen, out);
        return load_composite(f, g, glen, out, depth);
    }
}

int tt_path(TtFont *f, int gid, Path *out) {
    memset(out, 0, sizeof(*out));
    return load_glyph(f, gid, out, 0);
}

void tt_path_free(Path *p) {
    int i;
    for (i = 0; i < p->n; i++) free(p->lp[i].xy);
    free(p->lp);
    p->lp = 0;
    p->n = p->cap = 0;
}
