# Third-party notices

## Navi-Link

Parts of the TFT navigation presentation and generated bitmap data are based
on resources and layout behavior from Navi-Link.

- Original upstream repository: https://github.com/shuhao1022/Navi-Link
- Reference fork used during development: https://github.com/zuo-qirun/Navi-Link
- Imported material: maneuver, traffic-light direction, camera, and lane PNG
  resources under `app/src/main/res`, plus layout dimensions and color values
  used as a visual reference.
- Import tool: `esp32_display/tools/import_navilink_assets.ps1`
- Generated output: `esp32_display/src/NaviLinkIcons.cpp`

As checked on 2026-07-11, neither referenced Navi-Link repository contained a
`LICENSE` file or a GitHub-recognized license declaration. This notice is
attribution and source disclosure only; it does not claim that the Navi-Link
material is covered by this repository's license, and it does not grant any
additional rights to that material. Copyright and other rights in Navi-Link
remain with its respective authors and contributors.

## Refined Now Playing

The optional TFT `Refined` music page adapts the composition, cover-derived
background, lyric hierarchy, 500 ms line motion, and staggered easing of
[solstice23/refined-now-playing-netease](https://github.com/solstice23/refined-now-playing-netease)
at commit `f3b3e2b38809e711d5c60cb2b7f8ce1eda318bda` to the 320x240 embedded display.

Refined Now Playing is licensed under the MIT License. Copyright (c) 2022
solstice23. The original license and source repository remain the authoritative
license notice for that project.
