/* CLI. Default action: draw one bugcheck frame and either show it in a window
 * or write it to a file (--render out.ppm / out.bmp).
 *
 * 1:1 rendering needs the Windows boot face:
 *   C:\Windows\Boot\Fonts\segoe_slboot.ttf      (wide, the one BGP uses)
 *   C:\Windows\Boot\Fonts\segoen_slboot.ttf     (0.75 pre-condensed, console only)
 * Any other font changes advances, so wrapping will differ. That is announced.
 */
#include "bsod.h"
#include "bugcodes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#if defined(_WIN32)
#define SEP "\\"
static const char *FONT_DEFAULT[] = {
    "C:\\Windows\\Boot\\Fonts\\segoe_slboot.ttf",
    "C:\\Windows\\Fonts\\segoeuisl.ttf",
    0,
};
#else
#define SEP "/"
static const char *FONT_DEFAULT[] = {
    "segoe_slboot.ttf",
    "/usr/share/fonts/truetype/segoe/segoe_slboot.ttf",
    "/usr/share/fonts/truetype/msttcorefonts/segoe_slboot.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/TTF/DejaVuSans.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
    "/System/Library/Fonts/Supplemental/Arial.ttf",
    "/System/Library/Fonts/Helvetica.ttc",
    0,
};
#endif

static void usage(void) {
    printf(
"BSoD-everywhere — 1:1 Windows 10 bugcheck screen, KB-sized, no deps\n"
"\n"
"usage: bsod [options]\n"
"  --size WxH        canvas size; default = real screen size in a window, 1920x1080 offscreen\n"
"  --render PATH     write PATH (.ppm or .bmp) instead of opening a window\n"
"  --window          force the window backend\n"
"  --font PATH       font file (default: the Windows boot face)\n"
"  -f, --full-screen borderless full-monitor window\n"
"  --progress N      0..100 static progress, default 0\n"
"  --complete-seconds S  animate: reach 100%% after S total seconds\n"
"  --per-percent S       animate: spend S seconds on every 1%% step\n"
"  --stop-code S     name or hex code (0xXXXXXXXX / XXXXXXXX); unknown hex falls back\n"
"                    to the 0xXXXXXXXX form the kernel uses\n"
"  --driver S        adds a \"What failed:\" line\n"
"  --params A,B,C    adds up to 8 parameter lines\n"
"  --no-qr --dark --insider\n"
"  --dump-layout     print every computed coordinate and exit after rendering\n"
"  --help\n");
}

static int parse_size(const char *s, int *w, int *h) {
    char buf[64];
    char *x;
    strncpy(buf, s, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    x = strchr(buf, 'x');
    if (!x) x = strchr(buf, 'X');
    if (!x) return 0;
    *x++ = 0;
    *w = atoi(buf);
    *h = atoi(x);
    return *w > 0 && *h > 0;
}

/* A stop code is accepted as hex when it is an optional 0x prefix followed by
 * 1..8 hex digits. Anything else is treated as a literal friendly name. */
static int parse_hex_code(const char *s, uint32_t *out) {
    const char *p = s;
    const char *q;
    unsigned long v;
    if (!s || !*s) return 0;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;
    if (!*p) return 0;
    for (q = p; *q; q++)
        if (!isxdigit((unsigned char)*q)) return 0;
    v = strtoul(p, 0, 16);
    if (v > 0xFFFFFFFFul) return 0;
    *out = (uint32_t)v;
    return 1;
}

static int load_any(TtFont *f, const char *path) {
    if (path && tt_open(f, path)) return 1;
    {
        int i;
        for (i = 0; FONT_DEFAULT[i]; i++)
            if (tt_open(f, FONT_DEFAULT[i])) return 1;
    }
    return 0;
}

/* ---------------------------------------------------------------- animation */
typedef struct {
    TtFont *font;
    Opt *opt;
    double t0;
} Anim;

static int anim_tick(Canvas *c, TtFont *f, Opt *o, void *ctx) {
    Anim *a = (Anim *)ctx;
    double elapsed = plat_now_seconds() - a->t0;
    double pct;

    if (o->complete_secs > 0.0)
        pct = elapsed / o->complete_secs * 100.0;
    else if (o->per_percent > 0.0)
        pct = elapsed / o->per_percent;
    else
        pct = (double)o->progress;

    if (pct < 0.0) pct = 0.0;
    if (pct > 100.0) pct = 100.0;
    o->progress = (int)(pct + 0.5);

    tx_reset_cache();
    bsod_draw(c, f, o);
    (void)a;
    return 1;
}

int main(int argc, char **argv) {
    Opt o;
    TtFont font;
    Canvas cv;
    const char *font_path = 0, *render = 0, *param_list = 0;
    static char hex_name[16];
    int i, want_window = 0, is_boot = 0, explicit_size = 0;
    int animated;
    Anim anim;

    memset(&o, 0, sizeof(o));
    o.w = 1920;
    o.h = 1080;
    o.progress = 0;
    o.stop_code = "CRITICAL_PROCESS_DIED";

    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--help") || !strcmp(a, "-h")) { usage(); return 0; }
        else if (!strcmp(a, "--size") && i + 1 < argc) { explicit_size = 1; if (!parse_size(argv[++i], &o.w, &o.h)) { fprintf(stderr, "bad --size\n"); return 2; } }
        else if (!strcmp(a, "--render") && i + 1 < argc) render = argv[++i];
        else if (!strcmp(a, "--font") && i + 1 < argc) font_path = argv[++i];
        else if (!strcmp(a, "--progress") && i + 1 < argc) o.progress = atoi(argv[++i]);
        else if (!strcmp(a, "--complete-seconds") && i + 1 < argc) o.complete_secs = atof(argv[++i]);
        else if (!strcmp(a, "--per-percent") && i + 1 < argc) o.per_percent = atof(argv[++i]);
        else if (!strcmp(a, "--full-screen") || !strcmp(a, "-f")) o.fullscreen = 1;
        else if (!strcmp(a, "--stop-code") && i + 1 < argc) {
            const char *v = argv[++i];
            if (parse_hex_code(v, &o.stop_code_val)) {
                const char *name = bugcode_lookup(o.stop_code_val);
                o.stop_code_hex = 1;
                if (name) o.stop_code = name;
                else {
                    snprintf(hex_name, sizeof(hex_name), "0x%08x", o.stop_code_val);
                    o.stop_code = hex_name;
                }
            } else {
                o.stop_code = v;
            }
        }
        else if (!strcmp(a, "--driver") && i + 1 < argc) o.driver = argv[++i];
        else if (!strcmp(a, "--params") && i + 1 < argc) param_list = argv[++i];
        else if (!strcmp(a, "--no-qr")) o.no_qr = 1;
        else if (!strcmp(a, "--dark")) o.dark = 1;
        else if (!strcmp(a, "--insider")) o.insider = 1;
        else if (!strcmp(a, "--dump-layout")) o.dump = 1;
        else if (!strcmp(a, "--window")) want_window = 1;
        else { fprintf(stderr, "unknown option: %s\n", a); usage(); return 2; }
    }
    if (o.progress < 0) o.progress = 0;
    if (o.progress > 100) o.progress = 100;
    if (o.complete_secs < 0.0) o.complete_secs = 0.0;
    if (o.per_percent < 0.0) o.per_percent = 0.0;

    if (param_list) {
        char *copy = (char *)malloc(strlen(param_list) + 1), *p;
        strcpy(copy, param_list);
        p = copy;
        while (*p && o.nparams < 8) {
            char *comma = strchr(p, ',');
            if (comma) *comma = 0;
            o.params[o.nparams++] = p;
            if (!comma) break;
            p = comma + 1;
        }
        /* leak is fine: process ends right after the frame */
    }

    if (!render && !explicit_size) {
        int sw, sh;
        if (o.fullscreen) plat_screen_full(&sw, &sh);
        else plat_screen_size(&sw, &sh);
        if (sw > 0 && sh > 0) { o.w = sw; o.h = sh; }
    }

    if (!load_any(&font, font_path)) {
        fprintf(stderr,
            "no usable font found.\n"
            "1:1 output needs the Windows boot face; copy segoe_slboot.ttf here and pass --font path.\n"
            "On Windows it is at C:\\Windows\\Boot\\Fonts\\segoe_slboot.ttf\n");
        return 3;
    }
#if defined(_WIN32)
    is_boot = !strcmp(FONT_DEFAULT[0], (font_path ? font_path : FONT_DEFAULT[0]));
#else
    is_boot = 0;
#endif
    if (!is_boot && !(font_path && strstr(font_path, "slboot")))
        fprintf(stderr, "note: font is not segoe_slboot.ttf — advances differ, wrapping may differ.\n");

    cv_init(&cv, o.w, o.h);
    if (!cv.px) { fprintf(stderr, "out of memory\n"); return 4; }
    bsod_draw(&cv, &font, &o);

    if (render) {
        if (!out_write(&cv, render)) { fprintf(stderr, "cannot write %s\n", render); return 5; }
        printf("rendered %s (%dx%d)\n", render, o.w, o.h);
        cv_free(&cv);
        tt_close(&font);
        return 0;
    }

    animated = (o.complete_secs > 0.0 || o.per_percent > 0.0);
    anim.font = &font;
    anim.opt = &o;
    anim.t0 = plat_now_seconds();

    if (!plat_show(&cv, &font, &o, animated ? anim_tick : 0, &anim)) {
        if (!want_window) {
            if (!out_write(&cv, "bsod.ppm")) { cv_free(&cv); tt_close(&font); return 5; }
            printf("no window backend, wrote bsod.ppm\n");
        }
    }
    cv_free(&cv);
    tt_close(&font);
    return 0;
}
