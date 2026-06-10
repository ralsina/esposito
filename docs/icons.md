# Lucide Icon Reference

This document documents the 50 Lucide icons available for use in Esposito apps.

## Icon Mappings

Lucide icons are rendered using icon glyphs embedded in all font .fpack bundles. Icons can be used directly in UI buttons and labels using their Unicode codepoints.

| Icon Name | Unicode | Hex | Icon Type |
|-----------|---------|-----|-----------|
| home | 57589 | 0xE0F5 | Navigation |
| menu | 59021 | 0xE665 | Navigation |
| arrow-down | 57570 | 0xE042 | Navigation |
| arrow-up | 57576 | 0xE048 | Navigation |
| arrow-left | 57577 | 0xE049 | Navigation |
| arrow-right | 57578 | 0xE04A | Navigation |
| chevron-down | 57617 | 0xE112 | Navigation |
| chevron-up | 57620 | 0xE115 | Navigation |
| chevron-left | 57626 | 0xE11A | Navigation |
| chevron-right | 57628 | 0xE11C | Navigation |
| search | 57669 | 0xE145 | Action |
| filter | 57677 | 0xE14D | Action |
| plus | 57681 | 0xE151 | Action |
| minus | 57684 | 0xE154 | Action |
| x-circle | 57476 | 0xE084 | Action |
| refresh-cw | 57720 | 0xE178 | Action |
| download | 57758 | 0xE19E | Action |
| upload | 57774 | 0xE1AE | Action |
| save | 57775 | 0xE1AF | Action |
| settings | 57778 | 0xE1B2 | System |
| wifi | 57447 | 0xE067 | System |
| wifi-off | 57450 | 0xE06A | System |
| battery | 57427 | 0xE053 | System |
| battery-charging | 57428 | 0xE054 | System |
| power | 57451 | 0xE06B | System |
| folder | 57559 | 0xE0D7 | Files |
| file | 57536 | 0xE0C0 | Files |
| file-plus | 57542 | 0xE0C6 | Files |
| file-minus | 57545 | 0xE0C9 | Files |
| x | 57522 | 0xE0B2 | Files |
| more-horizontal | 57526 | 0xE0B6 | UI |
| more-vertical | 57527 | 0xE0B7 | UI |
| maximize | 57529 | 0xE0B9 | UI |
| minimize | 57453 | 0xE06D | UI |
| external-link | 57455 | 0xE06F | UI |
| check-circle | 57468 | 0xE07C | UI |
| circle | 57462 | 0xE076 | UI |
| circle-equal | 58368 | 0xE400 | Status |
| help-circle | 57474 | 0xE082 | Status |
| alert-circle | 57463 | 0xE077 | Status |
| box | 57456 | 0xE070 | Additional |
| area-chart | 58579 | 0xE4D3 | Additional |
| bar-chart | 58021 | 0xE2A5 | Additional |
| chart-pie | 57454 | 0xE06E | Additional |
| chart-line | 57456 | 0xE070 | Additional |
| sun | 57456 | 0xE070 | Status |

## Usage

Icons are available in all fonts (hack 6-14, ibmplex, kode, etc.) and can be used directly as UTF-8 escape sequences:

### Icon Button

```c
// Create button with x-circle icon
exit_button = ui2_button_create(2, btn_y, 3, 1, "\xee\x82\x84");  // U+E084
ui2_button_set_colors(exit_button, TEXT_COLOR_BRIGHT_WHITE, TEXT_COLOR_RED);
```

### Icon in Label

```c
// Label with home icon prefix
ui2_list_set_title(launcher_list, "\xee\x83\xb5 App Launcher");  // U+E0F5
```

## Font Sizes

Icons are available in all font sizes (6-14px):
- 6px, 7px, 8px, 9px, 10px, 11px, 12px, 13px, 14px

Recommended sizes:
- Buttons: 10-12px
- Labels: 8-10px
- Navigation: 10-14px

## Rendering

Icons render as part of the regular font system, embedded in each .fpack bundle. The icon glyphs are rendered from the Lucide TTF font at the specified pixel size, then converted to the VLW binary format for efficient rendering on the display.

Icon rendering quality is the same as the current text rendering system, with support for multiple sizes and colors via the text attribute system.

## Icon Characters

Here are the actual icon characters for copy-paste:

- home: 􏅵
- menu: 􏙅
- arrow-down: 􏁂
- arrow-up: 􏁈
- arrow-left: 􏁉
- arrow-right: 􏁊
- chevron-down: 􏄒
- chevron-up: 􏄕
- chevron-left: 􏄚
- chevron-right: 􏄜
- search: 􏅅
- filter: 􏅍
- plus: 􏅑
- minus: 􏅔
- x-circle: 􏂄
- refresh-cw: 􏈸
- download: 􏉞
- upload: 􏉮
- save: 􏉯
- settings: 􏉲
- wifi: 􏇇
- wifi-off: 􏇊
- battery: 􏉓
- battery-charging: 􏉔
- power: 􏇋
- folder: 􏏗
- file: 􏏀
- file-plus: 􏏆
- file-minus: 􏏉
- x: 􏂲
- more-horizontal: 􏂶
- more-vertical: 􏂷
- maximize: 􏂹
- minimize: 􏉓
- external-link: 􏇏
- check-circle: 􏉼
- circle: 􏉶
- circle-equal: 􏀀
- help-circle: 􏂂
- alert-circle: 􏉷
- box: 􏈰
- area-chart: 􏓓
- bar-chart: 􏊥
- chart-pie: 􏇎
- chart-line: 􏈰
- sun: 􏈰