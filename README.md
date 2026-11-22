# BSoD-everywhere

A **1:1 reproduction of the Windows 10 bugcheck screen** (the blue ":( " page),
written in plain **C11**, with **zero third-party dependencies**, a **software
rasterizer**, and a binary measured in **tens of kilobytes**.

Everything — coordinates, font sizes, line strides, wrapping, the progress line,
the QR code, the clear colour — was reverse engineered from
`ntoskrnl.exe` (`10.0.18362.2274`) instead of eyeballing a screenshot.
The whole process, including the IDA scripts and the decompiled functions, is in
[`docs/`](docs/).

```
   #####                                 size: ~43 KB (Linux, -O2)
   #   #    :(                            no libs beyond libc + libX11/libgdi32
   #####                                 same pixels on every OS
```

## Why "everywhere"

The renderer is a pure software pipeline:

```
TrueType parse -> outline flatten -> scanline coverage -> alpha blend -> RGB buffer
```

The framebuffer is *the* output. A window is only a place to blit it, so
Windows, Linux and macOS all produce **byte-identical frames** for the same
font and size. Anything that cannot open a window can still render:

```bash
bsod --size 1920x1080 --render out.ppm     # P6
bsod --size 1920x1080 --render out.bmp     # 24-bit BMP
```

| platform | window | headless |
|---|---|---|
| Windows  | Win32 + `StretchDIBits` | yes |
| Linux    | Xlib `XPutImage`         | yes |
| macOS    | *(not implemented)*      | yes |

## 1:1, and what that actually means

The layout comes from `ntoskrnl!qword_140429ED0`, a 5 × 18 DWORD table
(5 display classes, 18 fields each). Two things had to be decoded before the
numbers made sense:

1. **The index trap.** The code addresses it as `qword_140429ED0[9*row + k]`, so
   `qword[k]`'s *low* half is `dword[2k]` and its *high* half is `dword[2k+1]`.
   Reading it as a plain `dword` array shifts every field after the first.
2. **`cell` is not `em`.** The table stores **line-box heights**, not font sizes:

```
BgpRasGetGlyphTextCellDimensions :  cell = RoundUp(96*size*(asc-desc)/(72*upem)) + 2
RaspScaleCoordinates             :  em   = size*96/(72*upem)  (per font unit)
                                     =>
                        em = cell * upem / (asc - desc)
```

For the Windows boot face (`segoe_slboot.ttf`, upem 2048, asc 2210, desc −514)
that factor is **0.7519**. With it, the wrapping boundaries fall out by
themselves: line 1 fits in one line, line 2 breaks after `we'll`, and the URL in
the info block drops to its own line — exactly like the real screen.

At 1920×1080 (display class 3):

| component | value | source |
|---|---|---|
| background | `#0078D7` | `BgpBcInitializeCriticalMode`, `-16746281` |
| `:(` | (190,105), cell 280 → em 210.5 | `d4,d5,d3` |
| body text | x 207, right edge 1187, cell 56 → em 42.1, top 393, stride 59 | `d4+d8`, `+d6`, `d2` |
| progress | (207, body bottom + 34), cell 56 | `BcpGetComponentOffsets` |
| QR | (207, body bottom + 136), 214 px | `d11,d13` + `qrcode4.bmp` |
| info block | (441, body bottom + 128), cell 30 then 24, stride 34 | `d14,d15` |

`d7` (440) and `d12` (70) are **dead fields**: every function that touches the
table was decompiled (9 of them), and the only generic dword walker
(`BcpGetCharacterMaxResourceProfile`) reads just `d0` of each row.

The QR matrix is not generated at runtime — it is the real 25×25 module pattern
(version 2, ECC M, `https://www.windows.com/stopcode`), sampled from the
`bootres` bitmaps; the identical matrix appears at all five sizes.

## Fonts

The font is **not bundled** (it is Microsoft's). At runtime:

* Windows: `C:\Windows\Boot\Fonts\segoe_slboot.ttf` is found automatically —
  this is the exact face `BgpFoInitialize` loads from the boot loader blob
  (`'ttcf'`), and it is the only way to get 1:1.
* Linux/macOS: pass `--font segoe_slboot.ttf`. Otherwise a substitute
  (DejaVu Sans / Liberation / Arial) is used and the program says so: geometry
  stays correct, but advance widths differ, so **wrapping will differ**.

`segoen_slboot.ttf` in the same directory is the 0.75× pre-condensed variant used
for the boot *console*, not for this screen. Use `segoe_slboot.ttf`.

## Build

```bash
make          # -O2
make tiny     # -Os + strip, size first
make test     # renders three resolutions + a stop-code variant, no window
```

Windows (MSVC):

```bat
cl /O1 /Isrc\include src\*.c /Fe:bsod.exe /link user32.lib gdi32.lib
```

## Usage

```bash
bsod                                  # window at 1920x1080
bsod --size 1024x768                  # other display class
bsod --progress 37 --stop-code IRQL_NOT_LESS_OR_EQUAL
bsod --driver nvlddmkm.sys --params 0x0,0x2,0x1,0xFFFFF80100000000
bsod --no-qr --dark --insider         # variants
bsod --dump-layout --render out.ppm   # print every computed coordinate
```

`Esc` / `q` closes the window.

## Size

| build | bytes |
|---|---|
| Linux `-O2`, unstripped | ~43 KB |
| Linux `make tiny` | ~20 KB |
| Windows MSVC `/O1` | see the CI artifact |

No runtime dependencies beyond `libc` (`libm`), and `libX11` on Linux or
`user32/gdi32` on Windows — both already part of the OS.

## Known gaps

* No TrueType hinting. `Rasp*` in ntoskrnl has a grid-fitting stage we have not
  finished decoding, so edges can differ by ~1 px at small sizes. Positions,
  strides and wrapping are unaffected.
* macOS has no window backend (headless only).
* `d7`/`d12` are documented as unused rather than guessed.

## License

MIT — see [LICENSE](LICENSE).

---

## 中文速览

* **是什么**：用纯 C11 复刻 Windows 10 蓝屏页，坐标/字号/行距/换行/进度行/二维码全部来自
  对 `ntoskrnl.exe` 的反汇编，不是照截图猜。
* **为什么"everywhere"**：渲染是纯软件的（TTF 解析 → 轮廓扁平化 → 扫描线覆盖率 → 混合），
  窗口只是把同一块 framebuffer 贴出去，所以三个平台像素一致；没有窗口也能 `--render` 出图。
* **关键结论**：表里的 `24/30/56/280` 是**行盒高度**不是字号；`em = cell × upem/(asc−desc)`，
  对 boot 字体等于 `0.7519×cell`。`d7`/`d12` 经全量 xref 证实无人读取。
* **字体不打包**：Windows 上自动用 `C:\Windows\Boot\Fonts\segoe_slboot.ttf`（这才是
  `BgpFoInitialize` 真正加载的那一版），其它平台请 `--font` 指过去，否则换行会不同。
* 逆向全过程、显示表字段语义、栅格器对照见 [`docs/`](docs/)。
