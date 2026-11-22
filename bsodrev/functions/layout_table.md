# Layout table readers

Table: `qword_140429ED0` (VA `0x140429ED0`), 5 rows × 18 DWORD, stored as
**9 QWORDs per row**. Machine data: [`../tables/display_table.csv`](../tables/display_table.csv).

## BcpGetDisplayType — `0x140349998`

Picks the row. Rows 1..4 each carry a `(w, h)` threshold in `d16/d17`; the
return value is the *count* of satisfied thresholds, so 1920×1080 → 3.

```c
int BcpGetDisplayType(int w, int h)
{
    int cls = 0;
    for (int row = 1; row < 5; ++row) {
        const uint32_t *r = row_of(row);          /* qword_140429ED0 + 9*row */
        if (w >= r[16] && h >= r[17])              /* d16, d17 */
            ++cls;
    }
    return cls;
}
```

## BcpGetComponentOffsets — `0x14034980C`

Turns the table row plus the measured text bottom into the four component
rects (progress, QR, info). `a5 = qr_side`, read from the QR bitmap header by
the caller, not from the table.

```c
void BcpGetComponentOffsets(int row, int bottom, int qr_side,
                            int *prog, int *qr, int *info, int *info2)
{
    const uint32_t *r = row_of(row);

    prog[0] = r[4] + r[8];              /* d4 + d8       -> x = 207          */
    prog[1] = bottom + r[11];           /* bottom + d11  -> 601              */

    int base = prog[1] + r[11];          /* bottom + 2*d11                     */

    qr[0] = r[4] + r[8];
    qr[1] = base + HIDWORD(q[r+6]) + ...;   /* d13 -> bottom + 136            */

    info[0] = r[4] + r[8] + qr_side + r[14];    /* d14 -> x = 441             */
    info[1] = base + r[15];                  /* d15 -> bottom + 128           */
    info[2] = r[11] + info[1];
}
```

Notes:

* `qr_side` is 72 / 90 / 115 / 214 / 284 by display class (from the bitmap),
  so `info x` is `207 + 214 + 20 = 441` at 1080p.
* the info "bottom" stored in `info[2]` is the stride, not a coordinate.
* `d7` (440) and `d12` (70) are never read here — see `docs/display_table.md`.
