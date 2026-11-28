/* Linux / Unix window backend: plain Xlib, no toolkit. */
#if defined(__linux__) || defined(__unix__)
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#include "bsod.h"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <time.h>

void plat_screen_size(int *w, int *h) {
    Display *d = XOpenDisplay(0);
    if (!d) { *w = 0; *h = 0; return; }
    *w = DisplayWidth(d, DefaultScreen(d));
    *h = DisplayHeight(d, DefaultScreen(d));
    XCloseDisplay(d);
}

void plat_screen_full(int *w, int *h) {
    plat_screen_size(w, h);
}

double plat_now_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static void repaint(Display *d, Window w, GC gc, XImage *img,
                    const Canvas *c, int dw, int dh) {
    unsigned *buf = (unsigned *)img->data;
    int i, j;
    for (j = 0; j < dh; j++) {
        int sy = (int)((size_t)j * c->h / dh);
        for (i = 0; i < dw; i++) {
            int sx = (int)((size_t)i * c->w / dw);
            const unsigned char *p = c->px + ((size_t)sy * c->w + sx) * 3;
            buf[j * dw + i] = ((unsigned)p[0] << 16) | ((unsigned)p[1] << 8) | (unsigned)p[2];
        }
    }
    XPutImage(d, w, gc, img, 0, 0, 0, 0, (unsigned)dw, (unsigned)dh);
    XFlush(d);
}

int plat_show(Canvas *c, TtFont *f, Opt *o, BsodTick tick, void *ctx) {
    Display *d = XOpenDisplay(0);
    Window w;
    XImage *img;
    unsigned *buf;
    int screen, running = 1;
    GC gc;
    int aw, ah, dw, dh;
    float s;

    if (!d) return 0;
    screen = DefaultScreen(d);

    aw = DisplayWidth(d, screen);
    ah = DisplayHeight(d, screen);
    dw = c->w;
    dh = c->h;
    if (c->w > aw || c->h > ah) {
        s = (float)aw / c->w;
        if ((float)ah / c->h < s) s = (float)ah / c->h;
        dw = (int)(c->w * s + 0.5f);
        dh = (int)(c->h * s + 0.5f);
    }

    if (o->fullscreen) {
        /* override-redirect covers the root window; the WM cannot decorate
         * or move it, matching the kernel's full-framebuffer paint. */
        dw = aw;
        dh = ah;
        w = XCreateWindow(d, RootWindow(d, screen), 0, 0, (unsigned)dw, (unsigned)dh,
                          0, CopyFromParent, InputOutput, CopyFromParent,
                          CWOverrideRedirect, &(XSetWindowAttributes){ .override_redirect = True });
    } else {
        w = XCreateSimpleWindow(d, RootWindow(d, screen), 0, 0, (unsigned)dw, (unsigned)dh, 0,
                                BlackPixel(d, screen), BlackPixel(d, screen));
    }
    XStoreName(d, w, "Windows");
    XSelectInput(d, w, ExposureMask | KeyPressMask | StructureNotifyMask);
    XMapWindow(d, w);

    buf = (unsigned *)malloc((size_t)dw * (size_t)dh * sizeof(unsigned));
    if (!buf) { XDestroyWindow(d, w); XCloseDisplay(d); return 0; }
    img = XCreateImage(d, DefaultVisual(d, screen), 24, ZPixmap, 0, (char *)buf,
                       (unsigned)dw, (unsigned)dh, 32, 0);
    gc = XCreateGC(d, w, 0, 0);
    repaint(d, w, gc, img, c, dw, dh);

    while (running) {
        XEvent e;
        while (XPending(d)) {
            XNextEvent(d, &e);
            if (e.type == KeyPress) running = 0;
            else if (e.type == ClientMessage || e.type == DestroyNotify) running = 0;
            else if (e.type == Expose) repaint(d, w, gc, img, c, dw, dh);
        }
        if (!running) break;
        if (tick) {
            if (tick(c, f, o, ctx)) repaint(d, w, gc, img, c, dw, dh);
            usleep(16000);
        } else {
            XNextEvent(d, &e);
            if (e.type == KeyPress) running = 0;
            else if (e.type == ClientMessage || e.type == DestroyNotify) running = 0;
            else if (e.type == Expose) repaint(d, w, gc, img, c, dw, dh);
        }
    }
    XFreeGC(d, gc);
    img->data = 0;              /* XDestroyImage would free our buffer twice */
    XDestroyImage(img);
    free(buf);
    XDestroyWindow(d, w);
    XCloseDisplay(d);
    return 1;
}

#endif /* __linux__ || __unix__ */
