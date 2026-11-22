/* Layout — a faithful port of BgpFwDisplayBugCheckScreen + BcpGetComponentOffsets
 * + BcpDisplayCriticalString + BcpDisplayErrorInformation.
 *
 * The table below is ntoskrnl!qword_140429ED0 at 0x140429ED0 (5 rows x 18 DWORD,
 * each row stored as 9 QWORD). Row = display class returned by BcpGetDisplayType.
 * Fields (see docs/display_table.md):
 *   d0  small cell      d1  info first cell   d2  body cell       d3  ":(" cell
 *   d4  left margin     d5  top margin        d6  text box width  d7  UNUSED
 *   d8  text indent     d9  gap under ":("     d10 line extra      d11 line stride
 *   d12 UNUSED          d13 qr y offset       d14 info gap        d15 info y offset
 *   d16..d17 resolution thresholds
 *
 * d7 (440) and d12 (70) are never read: every function that references the
 * table was decompiled (9 of them) plus BcpGetCharacterMaxResourceProfile, the
 * only generic dword walker, which touches just d0 of each row.
 */
#include "bsod.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const unsigned TBL[5][18] = {
    /* <=640x480  */{ 0x12, 0x16, 0x1A, 0x078, 0x050, 0x02D, 0x1D6, 0x0B9, 0x07, 0x03, 0x01, 0x14, 0x1E, 0x28, 0x0A, 0x22, 0x000, 0x000 },
    /* >=641x481  */{ 0x12, 0x16, 0x20, 0x0A0, 0x069, 0x03C, 0x1FE, 0x0FA, 0x0A, 0x05, 0x02, 0x16, 0x28, 0x2C, 0x0F, 0x26, 0x281, 0x1E1 },
    /* >=1024x768 */{ 0x12, 0x16, 0x28, 0x0C8, 0x087, 0x04B, 0x35C, 0x136, 0x0C, 0x06, 0x02, 0x18, 0x32, 0x30, 0x11, 0x2A, 0x400, 0x300 },
    /* >=1920x1080*/{ 0x18, 0x1E, 0x38, 0x118, 0x0BE, 0x069, 0x3D4, 0x1B8, 0x11, 0x08, 0x03, 0x22, 0x46, 0x44, 0x14, 0x3C, 0x780, 0x438 },
    /* >=2560x1440*/{ 0x22, 0x28, 0x48, 0x168, 0x0F5, 0x087, 0x4E2, 0x262, 0x16, 0x0B, 0x04, 0x31, 0x5A, 0x62, 0x19, 0x56, 0xA00, 0x5A0 },
};

/* QR side length per display class, from the bitmap BGP blits
 * (bootres RCDATA qrcode1..5: 72 / 90 / 115 / 214 / 284 px, 25x25 modules). */
static const int QR_PX[5] = { 72, 90, 115, 214, 284 };

/* 25x25, version 2, ECC M, "https://www.windows.com/stopcode".
 * Sampled from the real bitmap — identical matrix at every size (md5 checked). */
static const char *QR25[25] = {
    "#######.##.##.##......###", "#.....#.#...#.#...######.", "#.###.#...###.#.###..#.#.",
    "#.###.#...##..###.##..#..", "#.###.#.##......#######.#", "#.....#.###...#.#...####.",
    "#######..#.####.#.#.#....", "........##.#..#.#...#....", "##...####.#....########..",
    "..###...#.###.###.###.##.", "......#.####.#.#...####..", "##.#...##..#.#####..#..#.",
    "#.#..##....##..#.##..####", "##...#.###..#.....##..##.", "#.########.######..####..",
    "...#.#..#..#..####..##.#.", "##..###..##...#....#.####", ".........###..##.........",
    "#######.#.#.#.#.#.#######", "#.....#.#.#.#.#...#.....#", "#.###.#....#.#.##.#.###.#",
    "#.###.#.#..#.##.#.#.###.#", "#.###.#.##....#...#.###.#", "#.....#.###.....#.#.....#",
    "#######.##.#.#.##.#######",
};

/* text resources, UTF-16 literals at ntoskrnl 0x3a03e0 .. 0x3a0748 */
static const char *S_MAIN1 = "Your PC ran into a problem and needs to restart.";
static const char *S_MAIN2 = "We're just collecting some error info, and then we'll restart for you.";
static const char *S_INFO1 = "For more information about this issue and possible fixes, visit ";
static const char *S_INFO2 = "https://www.windows.com/stopcode";
static const char *S_INFO3 = "If you call a support person, give them this info:";
static const char *S_STOP  = "Stop Code:";
static const char *S_FAIL  = "What failed:";
static const char *S_PCT   = "% complete";

#define BG_BLUE   0x0078D7u    /* -16746281 as ARGB 0xFF0078D7 */
#define BG_GREEN  0x246F24u    /* -14389468 (Insider build)     */
#define FG_WHITE  0xFFFFFFu
#define FG_BLACK  0x000000u

static int adv_ch(TtFont *f, unsigned char ch, float em) {
    int gid = tt_gid(f, ch);
    if (!gid) return (int)(em * 0.30f + 0.5f);
    return (int)((float)tt_adv(f, gid) * em / (float)f->upem + 0.5f);
}

/* Greedy wrap, the way BcpDisplayCriticalString does it: fill the line, break
 * at the last space that precedes the overflow, never split a word. */
static int draw_para(Canvas *c, TtFont *f, const char *s, float cell, int stride,
                     int x, int top, int right, unsigned fg) {
    float em = tt_em_from_cell(f, cell);
    int asc = (int)(tt_asc_px(f, em) + 0.5f);
    int len = (int)strlen(s), pos = 0, line_top = top, lines = 0;
    int limit = right - x;
    char buf[512];

    while (pos < len) {
        int end, cut, draw_end, wpx = 0;

        while (pos < len && s[pos] == ' ') pos++;
        if (pos >= len) break;

        end = pos;
        while (end < len) {
            int a = adv_ch(f, (unsigned char)s[end], em);
            if (wpx + a > limit && end > pos) break;
            wpx += a;
            end++;
        }
        if (end == pos) end = pos + 1;

        cut = end;
        if (end < len) {
            int k = end;
            while (k > pos && s[k] != ' ') k--;
            if (k > pos) cut = k;
        }
        draw_end = cut;
        while (draw_end > pos && s[draw_end - 1] == ' ') draw_end--;
        if (draw_end - pos > 511) draw_end = pos + 511;

        memcpy(buf, s + pos, (size_t)(draw_end - pos));
        buf[draw_end - pos] = 0;
        tx_draw(c, f, buf, em, x, line_top + asc, fg);

        line_top += stride;
        lines++;
        pos = cut;
        if (lines > 64) break;
    }
    if (lines == 0) return top;
    return line_top - stride + (int)cell;   /* bottom of the last line box */
}

static void draw_qr(Canvas *c, int x, int y, int size, unsigned bg) {
    int margin = (int)(size * 0.08f + 0.5f);
    float pitch = (float)(size - 2 * margin) / 25.0f;
    int i, j;

    cv_rect(c, x, y, size, size, 0xFFFFFFu);
    for (j = 0; j < 25; j++) {
        for (i = 0; i < 25; i++) {
            int x0, x1, y0, y1;
            if (QR25[j][i] != '#') continue;
            x0 = x + margin + (int)(pitch * i + 0.5f);
            x1 = x + margin + (int)(pitch * (i + 1) + 0.5f);
            y0 = y + margin + (int)(pitch * j + 0.5f);
            y1 = y + margin + (int)(pitch * (j + 1) + 0.5f);
            if (x1 <= x0) x1 = x0 + 1;
            if (y1 <= y0) y1 = y0 + 1;
            cv_rect(c, x0, y0, x1 - x0, y1 - y0, bg);
        }
    }
}

int bsod_draw(Canvas *c, TtFont *f, const Opt *o) {
    int row = 0, i, j;
    const unsigned *r;
    unsigned bgc, fg = FG_WHITE;
    int left, right, y, bottom, qr_size, qr_x, qr_y, info_x, info_y, prog_y;
    float em_face, em_body, em_info;
    char tmp[160];

    /* BcpGetDisplayType: 1920x1080 -> row 3 */
    for (i = 1; i < 5; i++) {
        if (o->w < (int)TBL[i][16] || o->h < (int)TBL[i][17]) break;
        row = i;
    }
    r = TBL[row];

    if (o->dark) { bgc = FG_BLACK; fg = FG_WHITE; }
    else bgc = o->insider ? BG_GREEN : BG_BLUE;
    cv_clear(c, bgc);

    left = (int)(r[4] + r[8]);          /* 207 */
    right = (int)(r[4] + r[8] + r[6]);  /* 1187 */
    em_face = tt_em_from_cell(f, (float)r[3]);
    em_body = tt_em_from_cell(f, (float)r[2]);
    em_info = tt_em_from_cell(f, (float)r[0]);

    /* ":(" — BcpDisplayCriticalString(&qword_140373770, d3) at (d4, d5) */
    tx_draw(c, f, ":(", em_face, (int)r[4], (int)r[5] + (int)tt_asc_px(f, em_face), fg);
    bottom = (int)r[5] + (int)r[3];

    /* body — y = bottom + d9 ; stride = cell + d10 */
    y = bottom + (int)r[9];
    bottom = draw_para(c, f, S_MAIN1, (float)r[2], (int)(r[2] + r[10]), left, y, right, fg);
    bottom = draw_para(c, f, S_MAIN2, (float)r[2], (int)(r[2] + r[10]), left, bottom + (int)r[10], right, fg);

    /* BcpGetComponentOffsets */
    prog_y = bottom + (int)r[11];
    qr_size = QR_PX[row];
    qr_x = left;
    qr_y = bottom + 2 * (int)r[11] + (int)r[13];
    info_x = left + qr_size + (int)r[14];
    info_y = bottom + 2 * (int)r[11] + (int)r[15];

    /* progress: cell = d2 (same as body), x = d4+d8 */
    sprintf(tmp, "%d", o->progress);
    strcat(tmp, S_PCT);
    tx_draw(c, f, tmp, em_body, left, prog_y + (int)tt_asc_px(f, em_body), fg);

    if (!o->no_qr) draw_qr(c, qr_x, qr_y, qr_size, bgc);

    /* info block: x pinned by BcpTextBoxLeftEdgeOverride = info_x,
     * first paragraph cell d1, rest cell d0, stride d11 everywhere */
    {
        char big[320];
        int b1;
        strcpy(big, S_INFO1);
        strcat(big, S_INFO2);
        b1 = draw_para(c, f, big, (float)r[1], (int)r[11], info_x, info_y, right, fg);
        b1 = draw_para(c, f, S_INFO3, (float)r[0], (int)r[11], info_x, b1 + (int)r[11], right, fg);
        sprintf(tmp, "%s %s", S_STOP, o->stop_code ? o->stop_code : "CRITICAL_PROCESS_DIED");
        b1 = draw_para(c, f, tmp, (float)r[0], (int)r[11], info_x, b1 + (int)r[11], right, fg);
        if (o->driver) {
            sprintf(tmp, "%s %s", S_FAIL, o->driver);
            b1 = draw_para(c, f, tmp, (float)r[0], (int)r[11], info_x, b1 + (int)r[11], right, fg);
        }
        for (j = 0; j < o->nparams; j++)
            b1 = draw_para(c, f, o->params[j], (float)r[0], (int)r[11], info_x, b1 + (int)r[11], right, fg);
    }

    if (o->dump) {
        printf("resolution     : %dx%d\n", o->w, o->h);
        printf("display class  : %d\n", row);
        printf("font           : upem=%u asc=%d desc=%d  cell/em=%.4f\n", f->upem, f->asc, f->desc,
               (float)(f->asc - f->desc) / (float)f->upem);
        printf("sad face       : (%d,%d) cell=%u em=%.1f\n", (int)r[4], (int)r[5], r[3], em_face);
        printf("body           : x=%d right=%d cell=%u em=%.1f top=%d stride=%u\n",
               left, right, r[2], em_body, (int)r[5] + (int)r[3] + (int)r[9], r[2] + r[10]);
        printf("progress       : (%d,%d) cell=%u\n", left, prog_y, r[2]);
        printf("qr             : (%d,%d) size=%d   [d7=%u d12=%u are dead fields]\n",
               qr_x, qr_y, qr_size, r[7], r[12]);
        printf("info           : (%d,%d) cell1=%u cell0=%u stride=%u\n", info_x, info_y, r[1], r[0], r[11]);
        printf("info em        : %.1f / %.1f\n", tt_em_from_cell(f, (float)r[1]), em_info);
    }
    return 1;
}
