/* Fallback backend: no window at all (macOS, exotic unices).
 * The program still renders --render out.ppm everywhere. */
#if !defined(_WIN32) && !defined(__linux__) && !defined(__unix__)
#include "bsod.h"
#include <time.h>
int plat_show(Canvas *c, TtFont *f, Opt *o, BsodTick tick, void *ctx) {
    (void)c; (void)f; (void)o; (void)tick; (void)ctx;
    return 0;
}
void plat_screen_size(int *w, int *h) { *w = 0; *h = 0; }
void plat_screen_full(int *w, int *h) { *w = 0; *h = 0; }
double plat_now_seconds(void) { return (double)time(NULL); }
#endif
