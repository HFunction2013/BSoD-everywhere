# Text engine

Three functions draw every string on the screen. All of them go through
`BgpRasPrintGlyph`, so geometry decisions made here are what the eye sees.

## BcpDisplayCriticalString — `0x140349024` (the body text)

```c
void BcpDisplayCriticalString(int row, int *cursor_y, const wchar_t *text,
                              int cell, int flags)
{
    const uint32_t *r = row_of(row);
    int left  = r[4] + r[8];       /* d4 + d8 = 207 */
    int right = left + r[6];       /* d6      -> 1187 */
    int x = left;
    int y = *cursor_y;

    for (each word in text) {
        int w = BgpRasGetGlyphAdvanceWidth(...);   /* summed per glyph */
        if (x + w > right) {                       /* break at the last space */
            x = left;
            y += line_height + r[10];              /* d10 = 3 extra       */
        }
        draw word at (x, y) with cell
        x += w;
    }
    *cursor_y = y + line_height;                   /* EC8 = y + v14       */
}
```

`line_height` (`v14`) is the **max glyph bitmap height on that line**
(`if (v33 > v14) v14 = v33`), not the cell. For the four body lines it lands
on ≈ 56 = cell, which is why the C port can use the cell directly.

* one cell for the whole paragraph: the caller passes `d2` (body) or `d1/d0`
  (info first / following paragraphs).
* the `:(" ` line is *not* this function: the face is drawn separately from
  `BgpFwDisplayBugCheckScreen` with cell `d3`; the body then starts at
  `d5 + d3 + d9 = 105 + 280 + 8 = 393`.

## BcpDisplayErrorInformation — `0x140349380` (info block)

```c
void BcpDisplayErrorInformation(int row, int *info_rect, ...)
{
    BcpTextBoxLeftEdgeOverride = info_rect[0];       /* 441 */

    draw "For more information about this issue and possible fixes, visit "
       + "https://www.windows.com/stopcode"   with cell d1 (30), stride d11 (34)
    y = last_bottom + d11;
    draw "If you call a support person, give them this info:"  cell d0 (24)
    y = last_bottom + d11;
    draw "What failed: " + module            (only when a module is named)
    y = last_bottom + d11;
    draw "Stop Code: "   + bugcheck name     cell d0 (24)
}
```

* the URL is *not* a new paragraph — it is appended to the same string, so it
  wraps only when the measure exceeds `right`, which is exactly how the real
  screen puts it on a second line.
* `BcpTextBoxLeftEdgeOverride` is a global; that is the mechanism by which the
  same `BcpDisplayCriticalString` left-aligns at 441 instead of 207.

## BcpDisplayProgress — `0x140349608` (progress line)

```c
void BcpDisplayProgress(int row, int *cursor_y, int percent)
{
    const uint32_t *r = row_of(row);
    snprintf(s, "%d%% complete", percent);
    /* note: cell is d2 (body), not d1 */
    BcpDisplayCriticalString(row, cursor_y, s, r[2], 0);
}
```

Position comes from `BcpGetComponentOffsets` (`bottom + d11 = 601`), and the
C port draws `%d%%` with `%` as its own glyph, so `0% complete` is 12 glyphs.
