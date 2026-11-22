# Font metrics — where `cell` and `em` come from

Two different scales describe the same face. Confusing them is the root cause
of every "it still doesn't look right" round.

```c
/* BgpRasGetGlyphTextCellDimensions  (font index -> line box) */
cell = RoundUp(96 * size * (asc - desc) / (72 * upem)) + 2;

/* RaspScaleCoordinates  (point scaling, 26.6 fixed point) */
em_26_6 = (size * 96 << 6) / (72 * upem)  * unit;
```

so

```
em  = size * 96 / 72 = 1.333 * size
cell = em * (asc - desc) / upem
cell / em = (asc - desc) / upem          ->  (2210 + 514) / 2048 = 1.3298
em = 0.7519 * cell                        ->  the number the table does NOT store
```

## BgpFoDetermineFontInformation — `0x140A411BC`

The missing link between `cell` and `size`: it walks font **indices**, not
sizes, and stops at the first one whose text metrics reach the target height.

```c
int size_for_cell(int cell)
{
    int idx = 5;
    do {
        BgpFoGetTextMetrics(idx, &asc, &desc);   /* pixels at 96 dpi */
        height = RoundUp(96 * idx * (asc - desc) / (72 * upem)) + 2;
        ++idx;
    } while (height < cell);
    return idx - 1;            /* e.g. cell 56 -> size 31.6 -> em 42.1 */
}
```

`BgpFoGetTextMetrics` reports `asc`/`desc` in pixels (`size*96*asc/72/upem`),
so for `cell = 56` the chosen index gives `asc + |desc| ≈ 56`.

## Proof used by this repo

Measured advances of the boot face at em 42.1:

| string | width | limit (d6) | real screen |
|---|---|---|---|
| `Your PC ran into a problem and needs to restart.` | 890 | 980 | one line |
| `We're just collecting some error info, and then we'll restart for you.` | 1209 | 980 | breaks after `we'll` |
| `For more information … visit https://www.windows.com/stopcode` | 919 | 746 | URL drops to line 2 |

All three decisions match, which pins the font + scale pair. Any other boot
face (or `em = cell`) breaks at least one of them.
