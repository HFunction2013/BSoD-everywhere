# Entry point, font blob and QR

## BgpFwDisplayBugCheckScreen — `0x140349DF0`

Draws the whole screen, in this order:

```c
bg = (a1 == 456) ? 0x246F24 /* insider */ : 0x0078D7;   /* -16746281 / -14389468 */
fill_screen(bg);

row = BcpGetDisplayType(width, height);        /* 1920x1080 -> 3 */
r   = row_of(row);

/* 1. the sad face, cell d3, at (d4, d5) */
draw_string(":(", x = r[4], y = r[5], cell = r[3]);      /* (190,105) cell 280 */

/* 2. body, two sentences, cell d2, starting one cell + d9 lower */
y = r[5] + r[3] + r[9];                                  /* 105+280+8 = 393 */
BcpDisplayCriticalString(row, &y, S_MAIN1, r[2], 0);
BcpDisplayCriticalString(row, &y, S_MAIN2, r[2], 0);

/* 3. components below the measured text bottom */
qr_side = *(u32*)(qr_bitmap + 4);                        /* 72/90/115/214/284 */
BcpGetComponentOffsets(row, bottom, qr_side, prog, qr, info, &stride);
BcpDisplayProgress(row, &prog_y, percent);
BgpGxDrawRectangle(qr_bitmap, qr_rect);                  /* blit, not modules */
BcpDisplayErrorInformation(row, info, ...);
```

Order matters for the "1:1" feel: the progress line sits `d11` under the body,
the QR `d11` under the progress line, and the info block starts `d15` under the
QR's top edge (`d15` < `2*d11 + d13`, hence the 8 px difference).

## Font: the loader blob, not a resource

```c
/* BgpFwLibraryInitialize */
memmove(dst, *(a1 + 72), *(a1 + 80));     /* blob handed over by winload */
BgpFoInitialize(dst + 26, 1);             /* first 4 bytes must be 'ttcf' */
BgpGxProcessQrCodeBitmap(*(a1 + 184), &qword_14042C098);
```

`BgpFoInitialize` (0x140A402C8) validates `'ttcf'`, version `0x00010000` /
`0x00020000`, and reads the sub-font count at +8 — a real TrueType Collection.
That is why `ntoskrnl` contains **no** font name string: the face lives in
`C:\Windows\Boot\Fonts\*_boot.ttf` and is packed into the boot blob at
`Boot\Fonts` time.

Faces measured on disk (see `bsodrev/assets/fonts.md`):

* `segoe_slboot.ttf` — "Segoe Boot Semilight", upem 2048, asc 2210, desc −514,
  `A` advance 636 → the one the bugcheck screen uses
* `segoen_slboot.ttf` — same headers, every advance ×0.75 (`A` = 477) → console

## QR: a bitmap, not a generated code

```c
BgpGxProcessQrCodeBitmap(blob_qr, &g_qr);   /* header + 25x25 modules */
BgpGxDrawRectangle(&g_qr, &rect);           /* blit into the frame    */
```

The five bitmaps in `bootres.dll` (RCDATA 1/1033, a WIM) are `qrcode1..5`,
same 25×25 matrix (md5 identical), version 2 / ECC M /
`https://www.windows.com/stopcode`. The matrix is embedded verbatim in
`src/layout.c`; see `bsodrev/assets/qr_matrix.txt` for the extraction recipe.
