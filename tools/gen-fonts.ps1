# Regenerate the four text-size Montserrat fonts with German umlauts.
# Mirrors lvgl/scripts/built_in_font/built_in_font_gen.py exactly (same font
# files, same FontAwesome symbol set, same flags) and adds the codepoints
# 0xC4,0xD6,0xDC,0xDF,0xE4,0xF6,0xFC (Ä Ö Ü ß ä ö ü) to the range.
# Sizes 24/32/48 stay LVGL built-ins (numbers/units only — no umlauts there).
$ErrorActionPreference = "Stop"

$repo = "C:\Users\Basti\Desktop\ugreen-idx6011-pro-nas-display"
$fontdir = Join-Path $repo "lvgl\scripts\built_in_font"
$outdir = Join-Path $repo "src\fonts"
New-Item -ItemType Directory -Force $outdir | Out-Null

$range = "0x20-0x7F,0xB0,0x2022,0xC4,0xD6,0xDC,0xDF,0xE4,0xF6,0xFC"
$faSyms = "61441,61448,61451,61452,61452,61453,61457,61459,61461,61465,61468,61473,61478,61479,61480,61502,61507,61512,61515,61516,61517,61521,61522,61523,61524,61543,61544,61550,61552,61553,61556,61559,61560,61561,61563,61587,61589,61636,61637,61639,61641,61664,61671,61674,61683,61724,61732,61787,61931,62016,62017,62018,62019,62020,62087,62099,62212,62189,62810,63426,63650"

Set-Location $fontdir
foreach ($size in 14, 16, 18, 20) {
    $out = Join-Path $outdir "lv_font_montserrat_$size.c"
    Write-Host "generating size $size ..."
    npx --yes lv_font_conv@1.5.3 --no-compress --no-prefilter --bpp 4 --size $size `
        --font Montserrat-Medium.ttf -r $range `
        --font "FontAwesome5-Solid+Brands+Regular.woff" -r $faSyms `
        --format lvgl -o $out --force-fast-kern-format
    if ($LASTEXITCODE -ne 0) { throw "lv_font_conv failed for size $size" }
}
Get-ChildItem $outdir
