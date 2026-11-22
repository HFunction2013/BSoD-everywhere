# Pull the bugcheck QR bitmap + logo bitmaps out of bootres.dll.
#
#   pwsh -File extract_boot_bitmaps.ps1 -WorkDir C:\tmp\bootres
#
# bootres.dll RCDATA 1/1033 is a WIM (MSWIM magic), dism is the only WIM
# reader guaranteed on a stock Windows box (no 7z / wimlib here).
# Result: qrcode1..5.bmp and winlogo1..5.bmp, 24-bit, 54-byte header.
# The five qrcode bitmaps are the same 25x25 matrix at 72/90/115/214/284 px;
# that matrix is what src/layout.c embeds (see bsodrev/assets/qr_matrix.txt).

param(
    [string]$WorkDir = "$env:TEMP\bootres",
    [string]$BootresDll = "$env:SystemRoot\System32\bootres.dll"
)

$ErrorActionPreference = "Stop"
New-Item -ItemType Directory -Force -Path $WorkDir | Out-Null
$wim = Join-Path $WorkDir "bootres.wim"
$img = Join-Path $WorkDir "bootres.img"

# 1. carve the RCDATA resource out of bootres.dll
$bytes = [IO.File]::ReadAllBytes($BootresDll)
# the WIM header starts with the MSWIM magic; find it and copy to EOF
$magic = [Text.Encoding]::ASCII.GetBytes("MSWIM")
$start = -1
for ($i = 0; $i -lt $bytes.Length - 4; $i++) {
    if ($bytes[$i] -eq $magic[0] -and $bytes[$i+1] -eq $magic[1] -and
        $bytes[$i+2] -eq $magic[2] -and $bytes[$i+3] -eq $magic[3]) { $start = $i; break }
}
if ($start -lt 0) { throw "MSWIM magic not found in $BootresDll" }
[IO.File]::WriteAllBytes($wim, $bytes[$start..($bytes.Length - 1)])

# 2. unpack index 1
dism /Apply-Image /ImageFile:$wim /Index:1 /ApplyDir:$img /Quiet
if ($LASTEXITCODE -ne 0) { throw "dism failed with $LASTEXITCODE" }

# 3. collect the bitmaps
Get-ChildItem -Path $img -Recurse -Filter *.bmp |
    Copy-Item -Destination $WorkDir -Force
Get-ChildItem -Path $WorkDir -Filter *.bmp | Select-Object Name, Length | Format-Table -AutoSize

# 4. (optional) sample the QR module grid with Python + Pillow:
#    python - <<'EOF'
#    from PIL import Image
#    im = Image.open("qrcode4.bmp").convert("L")   # 214 px, class 3
#    ...
#    EOF
