/* Win32 window backend: one window, one StretchDIBits, no controls. */
#if defined(_WIN32)
#include "bsod.h"
#include <windows.h>
#include <string.h>

static Canvas *g_cv;
static BITMAPINFO g_bi;

static LRESULT CALLBACK wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        if (g_cv) {
            StretchDIBits(dc, 0, 0, g_cv->w, g_cv->h, 0, 0, g_cv->w, g_cv->h,
                          g_cv->px, &g_bi, DIB_RGB_COLORS, SRCCOPY);
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
