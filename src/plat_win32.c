/* Win32 window backend: one window, one StretchDIBits, no controls. */
#if defined(_WIN32)
#include "bsod.h"
#include <windows.h>
#include <stdlib.h>
#include <string.h>

static Canvas *g_cv;
static BITMAPINFO g_bi;
static unsigned char *g_bgr;              /* DIB copy: 24bpp BI_RGB is BGR */
static int g_bgr_w, g_bgr_h;

/* A 24-bit BI_RGB DIB stores pixels as B,G,R in memory; our canvas keeps
 * R,G,B. Feeding the canvas straight to StretchDIBits swaps red and blue,
 * which turns #0078D7 into #D77800 (orange). */
static int ensure_bgr(const Canvas *c) {
    int i, n;
    if (g_bgr && g_bgr_w == c->w && g_bgr_h == c->h) return 1;
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
    return 1;
}

static LRESULT CALLBACK wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        if (g_cv && ensure_bgr(g_cv)) {
            StretchDIBits(dc, 0, 0, g_cv->w, g_cv->h, 0, 0, g_cv->w, g_cv->h,
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

int plat_show(Canvas *c, const char *title) {
    static int registered;
    WNDCLASSA wc;
    HWND h;
    MSG msg;

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

    memset(&g_bi, 0, sizeof(g_bi));
    g_bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    g_bi.bmiHeader.biWidth = c->w;
    g_bi.bmiHeader.biHeight = -c->h;    /* negative: top-down rows */
    g_bi.bmiHeader.biPlanes = 1;
    g_bi.bmiHeader.biBitCount = 24;
    g_bi.bmiHeader.biCompression = BI_RGB;
    g_cv = c;

    h = CreateWindowExA(0, "BsodEverywhere", title, WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                        0, 0, c->w, c->h, 0, 0, GetModuleHandle(0), 0);
    if (!h) return 0;
    /* fit the client area exactly to the canvas */
    {
        RECT r = { 0, 0, c->w, c->h };
        AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
        SetWindowPos(h, 0, 0, 0, r.right - r.left, r.bottom - r.top, SWP_NOMOVE | SWP_NOZORDER);
    }
    ShowWindow(h, SW_SHOW);
    UpdateWindow(h);
    while (GetMessage(&msg, 0, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 1;
}

#endif /* _WIN32 */
