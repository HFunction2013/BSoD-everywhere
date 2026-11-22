/* Image writers: PPM (P6) and BMP (24-bit). Both are trivial, which keeps the
 * binary small and lets every platform dump a pixel-identical frame. */
#include "bsod.h"
#include <stdio.h>
#include <string.h>

int out_ppm(const Canvas *c, const char *path) {
    FILE *fp = fopen(path, "wb");
    int i;
    if (!fp) return 0;
    fprintf(fp, "P6\n%d %d\n255\n", c->w, c->h);
    for (i = 0; i < c->w * c->h; i++) fwrite(c->px + i * 3, 1, 3, fp);
    fclose(fp);
    return 1;
}

static void put32(unsigned char *p, unsigned v) {
    p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16); p[3] = (unsigned char)(v >> 24);
}
static void put16(unsigned char *p, unsigned v) {
    p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8);
}

int out_bmp(const Canvas *c, const char *path) {
    FILE *fp = fopen(path, "wb");
    int rowsz = (c->w * 3 + 3) & ~3;
    int size = 54 + rowsz * c->h;
    unsigned char h[54];
    int y, x;
    if (!fp) return 0;
    memset(h, 0, sizeof(h));
    h[0] = 'B'; h[1] = 'M';
    put32(h + 2, (unsigned)size);
    put32(h + 10, 54);
    put32(h + 14, 40);
    put32(h + 18, (unsigned)c->w);
    put32(h + 22, (unsigned)c->h);
    put16(h + 26, 1);
    put16(h + 28, 24);
    put32(h + 34, (unsigned)(rowsz * c->h));
    fwrite(h, 1, sizeof(h), fp);
    for (y = c->h - 1; y >= 0; y--) {          /* BMP rows are bottom-up */
        const unsigned char *src = c->px + (size_t)y * (size_t)c->w * 3;
        for (x = 0; x < c->w; x++) {
            unsigned char bgr[3];
            bgr[0] = src[x * 3 + 2];
            bgr[1] = src[x * 3 + 1];
            bgr[2] = src[x * 3 + 0];
            fwrite(bgr, 1, 3, fp);
        }
        for (x = c->w * 3; x < rowsz; x++) fputc(0, fp);
    }
    fclose(fp);
    return 1;
}

int out_write(const Canvas *c, const char *path) {
    const char *dot = strrchr(path, '.');
    if (dot && (dot[1] == 'b' || dot[1] == 'B') && (dot[2] == 'm' || dot[2] == 'M'))
        return out_bmp(c, path);
    return out_ppm(c, path);
}
