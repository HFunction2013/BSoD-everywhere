# Fonts used, and how to verify a candidate face

No font file is redistributed here (they are Microsoft's). The renderer loads
one from disk, so 1:1 output needs the real boot face:

```
C:\Windows\Boot\Fonts\segoe_slboot.ttf    <- bugcheck screen (wide)
C:\Windows\Boot\Fonts\segoen_slboot.ttf   <- console variant (0.75 advances)
```

## Headers that matter (read with `ttdump` or any TTF parser)

| file | upem | ascender | descender | `A` advance | cell/em |
|---|---|---|---|---|---|
| `segoe_slboot.ttf` | 2048 | 2210 | −514 | 636 | 1.3298 |
| `segoen_slboot.ttf` | 2048 | 2210 | −514 | 477 | 1.3298 |

Same headers, different advances — that is why `segoen` looks like "the same
font squeezed to 75 %". Using `segoen` with `em = cell` reproduces the widths
but not the line boxes, so it is the wrong choice; using `segoe_sl` with
`em = 0.7519*cell` is right on both counts.

## Acceptance test for any candidate face

Run

```bash
bsod --size 1920x1080 --dump-layout --render /tmp/x.bmp --font <face>.ttf
```

and require all three:

* `Your PC ran into a problem and needs to restart.` ≤ 980 px (one line),
* the second sentence breaks after `we'll`,
* the info block's URL drops to its own line (limit 746 px).

Other boot faces (`wgl4_boot.ttf`, `chs/cht/jpn/kor_boot.ttf`,
`segmono_boot.ttf`) fail at least one of them.
