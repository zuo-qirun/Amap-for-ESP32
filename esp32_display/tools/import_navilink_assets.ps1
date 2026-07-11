param(
    [Parameter(Mandatory = $true)]
    [string]$NaviLinkRoot,
    [string]$OutputPath = (Join-Path $PSScriptRoot "..\src\NaviLinkIcons.cpp")
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

function Add-Bitmap {
    param(
        [string]$Symbol,
        [string]$Path,
        [int]$Width,
        [int]$Height
    )

    $source = [System.Drawing.Bitmap]::FromFile($Path)
    $scaled = New-Object System.Drawing.Bitmap($Width, $Height, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [System.Drawing.Graphics]::FromImage($scaled)
    $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $graphics.DrawImage($source, 0, 0, $Width, $Height)
    $graphics.Dispose()
    $source.Dispose()

    # The source icons are tinted monochrome PNGs. Keep every alpha level so
    # the ESP32 renderer preserves the anti-aliased outline after scaling.
    [byte[]]$bytes = New-Object byte[] ($Width * $Height)
    for ($y = 0; $y -lt $Height; $y++) {
        for ($x = 0; $x -lt $Width; $x++) {
            $bytes[$y * $Width + $x] = $scaled.GetPixel($x, $y).A
        }
    }
    $scaled.Dispose()

    $script:Lines += "const uint8_t $Symbol[] PROGMEM = {"
    for ($offset = 0; $offset -lt $bytes.Length; $offset += 12) {
        $count = [Math]::Min(12, $bytes.Length - $offset)
        $row = for ($index = 0; $index -lt $count; $index++) { "0x{0:X2}" -f $bytes[$offset + $index] }
        $script:Lines += "  " + ($row -join ", ") + ","
    }
    $script:Lines += "};"
    $script:Lines += "const Bitmap ${Symbol}Bitmap = {$Symbol, $Width, $Height};"
    $script:Lines += ""
}

$source = Join-Path $NaviLinkRoot "app\src\main\res"
$mipmap = Join-Path $source "mipmap-xhdpi"
$drawable = Join-Path $source "drawable"
$script:Lines = @(
    "// Generated from Navi-Link Android res assets. Do not edit by hand.",
    "#include `"NaviLinkIcons.h`"",
    "",
    "#include <pgmspace.h>",
    "",
    "namespace NaviLinkIcons {",
    "namespace {"
)

for ($icon = 2; $icon -le 20; $icon++) {
    Add-Bitmap "kTurn$icon" (Join-Path $mipmap "sou${icon}_night_a530.png") 60 60
}
Add-Bitmap "kLightLeft" (Join-Path $mipmap "light_left.png") 22 22
Add-Bitmap "kLightRight" (Join-Path $mipmap "light_right.png") 22 22
Add-Bitmap "kLightStraight" (Join-Path $mipmap "light_straight.png") 22 22
Add-Bitmap "kLightUTurn" (Join-Path $mipmap "light_u_turn.png") 22 22

$cameraIcons = @(
    "bicycle", "bus", "byfoot", "default", "etc", "hov", "lamp", "light", "park",
    "phone", "press", "railway", "recycle", "reverse", "safe", "sonar", "space", "tail",
    "urgen"
)
foreach ($camera in $cameraIcons) {
    Add-Bitmap ("kCamera" + ($camera.Substring(0, 1).ToUpper() + $camera.Substring(1))) (Join-Path $drawable "camera_${camera}.png") 38 38
}

$laneCodes = @()
Get-ChildItem $drawable -Filter "lane_pdf_*.png" | Sort-Object Name | ForEach-Object {
    if ($_.BaseName -match "lane_pdf_(\d+)$") {
        $code = [int]$Matches[1]
        $laneCodes += $code
        Add-Bitmap "kLane$code" $_.FullName 36 36
    }
}

$script:Lines += "}  // namespace"
$script:Lines += ""
$script:Lines += "const Bitmap& turnBitmap(int icon) {"
$script:Lines += "  switch (icon) {"
for ($icon = 2; $icon -le 20; $icon++) {
    $script:Lines += "    case ${icon}: return kTurn${icon}Bitmap;"
}
$script:Lines += "    default: return kTurn20Bitmap;"
$script:Lines += "  }"
$script:Lines += "}"
$script:Lines += ""
$script:Lines += "const Bitmap& trafficDirectionBitmap(int direction) {"
$script:Lines += "  if (direction == 1 || direction == 5 || direction == 6) return kLightLeftBitmap;"
$script:Lines += "  if (direction == 3 || direction == 8) return kLightUTurnBitmap;"
$script:Lines += "  if (direction == 2 || direction == 7) return kLightRightBitmap;"
$script:Lines += "  return kLightStraightBitmap;"
$script:Lines += "}"
$script:Lines += ""
$script:Lines += "const Bitmap& cameraBitmap(int type) {"
$script:Lines += "  switch (type) {"
$script:Lines += "    case 6: case 20: return kCameraBicycleBitmap;"
$script:Lines += "    case 4: case 16: return kCameraBusBitmap;"
$script:Lines += "    case 13: case 1015: return kCameraByfootBitmap;"
$script:Lines += "    case 11: case 1099: return kCameraEtcBitmap;"
$script:Lines += "    case 29: case 1029: return kCameraHovBitmap;"
$script:Lines += "    case 22: case 1001: return kCameraLampBitmap;"
$script:Lines += "    case 2: case 15: return kCameraLightBitmap;"
$script:Lines += "    case 21: case 1017: return kCameraParkBitmap;"
$script:Lines += "    case 19: case 1005: return kCameraPhoneBitmap;"
$script:Lines += "    case 12: case 1030: return kCameraPressBitmap;"
$script:Lines += "    case 26: case 1024: return kCameraRailwayBitmap;"
$script:Lines += "    case 30: case 1012: return kCameraRecycleBitmap;"
$script:Lines += "    case 25: case 1016: return kCameraReverseBitmap;"
$script:Lines += "    case 18: case 1002: return kCameraSafeBitmap;"
$script:Lines += "    case 24: case 1021: return kCameraSonarBitmap;"
$script:Lines += "    case 28: case 1028: return kCameraSpaceBitmap;"
$script:Lines += "    case 5: return kCameraUrgenBitmap;"
$script:Lines += "    case 27: case 1011: return kCameraTailBitmap;"
$script:Lines += "    default: return kCameraDefaultBitmap;"
$script:Lines += "  }"
$script:Lines += "}"
$script:Lines += ""
$script:Lines += "const Bitmap* laneBitmap(int code) {"
$script:Lines += "  switch (code) {"
foreach ($code in $laneCodes) {
    $script:Lines += "    case ${code}: return &kLane${code}Bitmap;"
}
$script:Lines += "    default: return nullptr;"
$script:Lines += "  }"
$script:Lines += "}"
$script:Lines += "}  // namespace NaviLinkIcons"

$directory = Split-Path -Parent $OutputPath
New-Item -ItemType Directory -Force -Path $directory | Out-Null
[System.IO.File]::WriteAllLines($OutputPath, $script:Lines, [System.Text.UTF8Encoding]::new($false))
Write-Host "Generated $OutputPath from $NaviLinkRoot"
