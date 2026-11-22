/* Fallback backend: no window at all (macOS, exotic unices).
 * The program still renders --render out.ppm everywhere. */
#if !defined(_WIN32) && !defined(__linux__) && !defined(__unix__)
#include "bsod.h"
int plat_show(Canvas *c, const char *title) { (void)c; (void)title; return 0; }
#endif
