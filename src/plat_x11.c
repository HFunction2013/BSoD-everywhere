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

int plat_show(Canvas *c, const char *title) {
    Display *d = XOpenDisplay(0);
    Window w;
    XImage *img;
    unsigned *buf;
    int screen, i, running = 1;
    GC gc;

    if (!d) return 0;
    screen = DefaultScreen(d);
    w = XCreateSimpleWindow(d, RootWindow(d, screen), 0, 0, (unsigned)c->w, (unsigned)c->h, 0,
                            BlackPixel(d, screen), BlackPixel(d, screen));
    XStoreName(d, w, title);
    XSelectInput(d, w, ExposureMask | KeyPressMask | StructureNotifyMask);
    XMapWindow(d, w);

    buf = (unsigned *)malloc((size_t)c->w * (size_t)c->h * sizeof(unsigned));
    if (!buf) { XDestroyWindow(d, w); XCloseDisplay(d); return 0; }
    for (i = 0; i < c->w * c->h; i++) {
        unsigned char *p = c->px + i * 3;
        buf[i] = ((unsigned)p[0] << 16) | ((unsigned)p[1] << 8) | (unsigned)p[2];
    }
    img = XCreateImage(d, DefaultVisual(d, screen), 24, ZPixmap, 0, (char *)buf,
                       (unsigned)c->w, (unsigned)c->h, 32, 0);
    gc = XCreateGC(d, w, 0, 0);
    XPutImage(d, w, gc, img, 0, 0, 0, 0, (unsigned)c->w, (unsigned)c->h);
    XFlush(d);

    while (running) {
        XEvent e;
        XNextEvent(d, &e);
        if (e.type == KeyPress) running = 0;
        else if (e.type == ClientMessage || e.type == DestroyNotify) running = 0;
        else if (e.type == Expose) {
            XPutImage(d, w, gc, img, 0, 0, 0, 0, (unsigned)c->w, (unsigned)c->h);
            XFlush(d);
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
