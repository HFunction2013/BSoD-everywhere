/* Win32 window backend: one window, one StretchDIBits, no controls. */
#if defined(_WIN32)
#include "bsod.h"
#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static Canvas *g_cv;
static TtFont *g_f;
static Opt *g_o;
static BsodTick g_tick;
static void *g_ctx;
static BITMAPINFO g_bi;
static unsigned char *g_bgr;              /* DIB copy: 24bpp BI_RGB is BGR */
static int g_bgr_w, g_bgr_h;
static int g_dw, g_dh;                    /* client (possibly scaled) size */
static int g_dirty;

/* A 24-bit BI_RGB DIB stores pixels as B,G,R in memory; our canvas keeps
 * R,G,B. Feeding the canvas straight to StretchDIBits swaps red and blue,
 * which turns #0078D7 into #D77800 (orange). */
static int ensure_bgr(const Canvas *c) {
    int i, n;
    if (g_bgr && g_bgr_w == c->w && g_bgr_h == c->h && !g_dirty) return 1;
    free(g_bgr);
    n = c->w * c->h;
    g_bgr = (unsigned char *)malloc((size_t)n * 3);
    if (!g_bgr) { g_bgr_w = g_bgr_h = 0; return 0; }
    for (i = 0; i < n; i++) {
        const unsigned char *s = c->px + (size_t)i * 3;
        g_bgr[i * 3 + 0] = s[2];            /* B */
        g_bgr[i * 3 + 1] = s[1];            /* G */
        g_bgr[i * 3 + 2] = s[0];            /* R */
    }
    g_bgr_w = c->w;
    g_bgr_h = c->h;
    g_dirty = 0;
    return 1;
}

void plat_screen_size(int *w, int *h) {
    RECT r;
    if (SystemParametersInfoW(SPI_GETWORKAREA, 0, &r, 0)) {
        *w = r.right - r.left;
        *h = r.bottom - r.top;
    } else {
        *w = GetSystemMetrics(SM_CXSCREEN);
        *h = GetSystemMetrics(SM_CYSCREEN);
    }
}

void plat_screen_full(int *w, int *h) {
    *w = GetSystemMetrics(SM_CXSCREEN);
    *h = GetSystemMetrics(SM_CYSCREEN);
}

double plat_now_seconds(void) {
    static LARGE_INTEGER freq;
    static int init;
    LARGE_INTEGER c;
    if (!init) { QueryPerformanceFrequency(&freq); init = 1; }
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)freq.QuadPart;
}

static LRESULT CALLBACK wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_TIMER:
        if (g_tick && g_cv) {
            if (g_tick(g_cv, g_f, g_o, g_ctx)) g_dirty = 1;
            InvalidateRect(h, 0, FALSE);
        }
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        if (g_cv && ensure_bgr(g_cv)) {
            /* dest = fitted client size; StretchDIBits scales the source */
            StretchDIBits(dc, 0, 0, g_dw, g_dh, 0, 0, g_cv->w, g_cv->h,
                          g_bgr, &g_bi, DIB_RGB_COLORS, SRCCOPY);
        }
        EndPaint(h, &ps);
        return 0;
    }
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE || wp == 'Q') PostQuitMessage(0);
        return 0;
    case WM_CLOSE:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(h, msg, wp, lp);
}

int plat_show(Canvas *c, TtFont *f, Opt *o, BsodTick tick, void *ctx) {
    static int registered;
    WNDCLASSA wc;
    HWND h;
    MSG msg;
    int aw, ah;
    float s;
    DWORD style;

    if (!registered) {
        memset(&wc, 0, sizeof(wc));
        wc.lpfnWndProc = wndproc;
        wc.hInstance = GetModuleHandle(0);
        wc.lpszClassName = "BsodEverywhere";
        wc.hCursor = LoadCursor(0, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
        if (!RegisterClassA(&wc)) return 0;
        registered = 1;
    }

    g_cv = c;
    g_f = f;
    g_o = o;
    g_tick = tick;
    g_ctx = ctx;
    g_dirty = 1;

    if (o->fullscreen) {
        /* ntoskrnl paints the whole framebuffer: borderless popup over the
         * full monitor, ignoring the taskbar work area. */
        plat_screen_full(&aw, &ah);
        g_dw = c->w;
        g_dh = c->h;
        style = WS_POPUP | WS_VISIBLE;
    } else {
        /* Fit the client area to the work area, preserving aspect ratio.
         * The canvas keeps its own resolution and the blit scales down only
         * if needed. */
        plat_screen_size(&aw, &ah);
        g_dw = c->w;
        g_dh = c->h;
        if (aw > 0 && ah > 0 && (c->w > aw || c->h > ah)) {
            s = (float)aw / c->w;
            if ((float)ah / c->h < s) s = (float)ah / c->h;
            g_dw = (int)(c->w * s + 0.5f);
            g_dh = (int)(c->h * s + 0.5f);
        }
        style = WS_OVERLAPPEDWINDOW | WS_VISIBLE;
    }

    memset(&g_bi, 0, sizeof(g_bi));
    g_bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    g_bi.bmiHeader.biWidth = c->w;
    g_bi.bmiHeader.biHeight = -c->h;    /* negative: top-down rows */
    g_bi.bmiHeader.biPlanes = 1;
    g_bi.bmiHeader.biBitCount = 24;
    g_bi.bmiHeader.biCompression = BI_RGB;

    h = CreateWindowExA(0, "BsodEverywhere", "Windows", style,
                        0, 0, g_dw, g_dh, 0, 0, GetModuleHandle(0), 0);
    if (!h) return 0;
    if (!o->fullscreen) {
        /* fit the client area exactly to the chosen (possibly scaled) size */
        RECT r = { 0, 0, g_dw, g_dh };
        AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
        SetWindowPos(h, 0, 0, 0, r.right - r.left, r.bottom - r.top, SWP_NOMOVE | SWP_NOZORDER);
    }
    ShowWindow(h, SW_SHOW);
    UpdateWindow(h);
    if (tick) SetTimer(h, 1, 50, NULL);

    while (GetMessage(&msg, 0, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    KillTimer(h, 1);
    return 1;
}

#endif /* _WIN32 */
