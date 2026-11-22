/* Canvas + text. Glyphs are cached by (gid, em) like RaspBitmapCache does,
 * only the cache is tiny because a bugcheck screen uses ~60 glyphs.
 */
#include "bsod.h"
#include <stdlib.h>
#include <string.h>

void cv_init(Canvas *c, int w, int h) {
    c->px = (unsigned char *)malloc((size_t)w * (size_t)h * 3);
    c->w = w;
    c->h = h;
    if (c->px) memset(c->px, 0, (size_t)w * (size_t)h * 3);
}

void cv_free(Canvas *c) {
    free(c->px);
    c->px = 0;
    c->w = c->h = 0;
}

void cv_clear(Canvas *c, unsigned rgb) {
    unsigned char *p = c->px;
    int i, n = c->w * c->h;
    for (i = 0; i < n; i++) {
        p[0] = (unsigned char)(rgb >> 16);
        p[1] = (unsigned char)(rgb >> 8);
        p[2] = (unsigned char)rgb;
        p += 3;
    }
}

void cv_rect(Canvas *c, int x, int y, int w, int h, unsigned rgb) {
    int i, j;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > c->w) w = c->w - x;
    if (y + h > c->h) h = c->h - y;
    for (j = 0; j < h; j++) {
        unsigned char *p = c->px + ((size_t)(y + j) * (size_t)c->w + (size_t)x) * 3;
        for (i = 0; i < w; i++) {
            p[0] = (unsigned char)(rgb >> 16);
            p[1] = (unsigned char)(rgb >> 8);
            p[2] = (unsigned char)rgb;
            p += 3;
        }
    }
}

void cv_blit(Canvas *c, const Bitmap *bm, int x, int y, unsigned rgb) {
    int i, j;
    unsigned char fr = (unsigned char)(rgb >> 16), fg = (unsigned char)(rgb >> 8), fb = (unsigned char)rgb;
    for (j = 0; j < bm->h; j++) {
        int dy = y + j;
        if (dy < 0 || dy >= c->h) continue;
        for (i = 0; i < bm->w; i++) {
            int dx = x + i;
            unsigned char a;
            unsigned char *d;
            if (dx < 0 || dx >= c->w) continue;
            a = bm->px[(size_t)j * (size_t)bm->w + (size_t)i];
            if (!a) continue;
            d = c->px + ((size_t)dy * (size_t)c->w + (size_t)dx) * 3;
            if (a == 255) { d[0] = fr; d[1] = fg; d[2] = fb; }
            else {
                d[0] = (unsigned char)((fr * a + d[0] * (255 - a)) / 255);
                d[1] = (unsigned char)((fg * a + d[1] * (255 - a)) / 255);
                d[2] = (unsigned char)((fb * a + d[2] * (255 - a)) / 255);
            }
        }
    }
}

/* ------------------------------------------------------------- glyph cache */
#define CACHE_N 256
typedef struct { int gid; float em; Bitmap bm; } Entry;
static Entry g_cache[CACHE_N];
static int g_used;

static void cache_clear(void) {
    int i;
    for (i = 0; i < g_used; i++) free(g_cache[i].bm.px);
    g_used = 0;
}

static const Bitmap *get_glyph(TtFont *f, int gid, float em) {
    int i;
    for (i = 0; i < g_used; i++) {
        if (g_cache[i].gid == gid && g_cache[i].em == em) return &g_cache[i].bm;
    }
    if (g_used >= CACHE_N) {
        /* simple: drop half the table, rendering stays deterministic */
        int half = CACHE_N / 2, j;
        for (j = 0; j < half; j++) free(g_cache[j].bm.px);
        memmove(g_cache, g_cache + half, sizeof(Entry) * (size_t)(CACHE_N - half));
        g_used = CACHE_N - half;
    }
    g_cache[g_used].gid = gid;
    g_cache[g_used].em = em;
    if (!ras_glyph(f, gid, em, &g_cache[g_used].bm)) {
        memset(&g_cache[g_used].bm, 0, sizeof(Bitmap));
    }
    return &g_cache[g_used++].bm;
}

float tx_width(TtFont *f, const char *s, float em) {
    float w = 0.0f, sc = em / (float)f->upem;
    for (; *s; s++) {
        int gid = tt_gid(f, (unsigned char)*s);
        if (!gid) { w += sc * (float)f->upem * 0.3f; continue; }
        w += sc * (float)tt_adv(f, gid);
    }
    return w;
}

int tx_px(TtFont *f, const char *s, float em) {
    float w = tx_width(f, s, em);
    return (int)(w + 0.5f);
}

void tx_draw(Canvas *c, TtFont *f, const char *s, float em, int x, int base, unsigned rgb) {
    float pen = (float)x, sc = em / (float)f->upem;
    for (; *s; s++) {
        int gid = tt_gid(f, (unsigned char)*s);
        const Bitmap *bm;
        if (!gid) { pen += 0.3f * em; continue; }
        bm = get_glyph(f, gid, em);
        if (bm->px) cv_blit(c, bm, (int)(pen + 0.5f) + bm->ox, base + bm->oy, rgb);
        pen += sc * (float)tt_adv(f, gid);
    }
}

/* callers that only draw one screen can reset the cache between frames */
void tx_reset_cache(void) { cache_clear(); }
