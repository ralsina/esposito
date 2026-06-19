# Lucide Icon Reference

This document documents the 34 Lucide icons available for use in Esposito apps.

## Important: UTF-8 Encoding

**ALL icons MUST use proper 3-byte UTF-8 encoding.** Codepoints in the E000-FFFF range (like Lucide icons) require 3 bytes in UTF-8, not 2 bytes.

For example:
- ❌ WRONG: `\xe0\xc9` (2 bytes - invalid UTF-8)
- ✅ CORRECT: `\xee\x83\x89` (3 bytes - proper UTF-8)

To get the correct UTF-8 encoding:
```python
cp = 0xE0C9  # file-plus codepoint
utf8_bytes = chr(cp).encode('utf-8')
hex_str = ''.join(f'\\x{b:02x}' for b in utf8_bytes)
# Result: \xee\x83\x89
```

## Icon Mappings

Lucide icons are rendered using icon glyphs embedded in all font .fpack bundles. Icons can be used directly in UI buttons and labels using their Unicode codepoints.

| Icon Name | Unicode | Hex | UTF-8 Escape | Icon Type |
|-----------|---------|-----|--------------|-----------|
| arrow-big-up | 57828 | 0xE1E4 | `\xee\x87\xa4` | Navigation |
| arrow-big-down | 57825 | 0xE1E1 | `\xee\x87\xa1` | Navigation |
| arrow-big-right | 57827 | 0xE1E3 | `\xee\x87\xa3` | Navigation |
| arrow-big-left | 57826 | 0xE1E2 | `\xee\x87\xa2` | Navigation |
| arrow-down-to-line | 58453 | 0xE455 | `\xee\x91\x95` | Navigation |
| chevron-up | 57456 | 0xE070 | `\xee\x81\xb0` | Navigation |
| chevron-down | 57453 | 0xE06D | `\xee\x81\xad` | Navigation |
| chevron-left | 57454 | 0xE06E | `\xee\x81\xae` | Navigation |
| chevron-right | 57455 | 0xE06F | `\xee\x81\xaf` | Navigation |
| arrow-up | 57418 | 0xE04A | `\xee\x81\x8a` | Navigation |
| arrow-down | 57410 | 0xE042 | `\xee\x81\x82` | Navigation |
| arrow-left | 57416 | 0xE048 | `\xee\x81\x88` | Navigation |
| arrow-right | 57417 | 0xE049 | `\xee\x81\x89` | Navigation |
| home | 57589 | 0xE0F5 | `\xee\x83\xb5` | Navigation |
| menu | 57621 | 0xE115 | `\xee\x84\x95` | Navigation |
| folder | 57559 | 0xE0D7 | `\xee\x83\x97` | Files |
| folder-plus | 57561 | 0xE0D9 | `\xee\x83\x99` | Files |
| file-plus | 57545 | 0xE0C9 | `\xee\x83\x89` | Files |
| file-minus | 57542 | 0xE0C6 | `\xee\x83\x86` | Files |
| external-link | 57529 | 0xE0B9 | `\xee\x82\xb9` | Files |
| book-open | 57439 | 0xE05F | `\xee\x81\x9f` | Files |
| search | 57681 | 0xE151 | `\xee\x85\x91` | Action |
| copy | 57502 | 0xE09E | `\xee\x82\x9e` | Action |
| download | 57522 | 0xE0B2 | `\xee\x82\xb2` | Action |
| upload | 57758 | 0xE19E | `\xee\x86\x9e` | Action |
| save | 57677 | 0xE14D | `\xee\x85\x8d` | Action |
| edit-2 | 57647 | 0xE12F | `\xee\x84\xaf` | Action |
| trash | 57741 | 0xE18D | `\xee\x86\x8d` | Action |
| trash-2 | 57742 | 0xE18E | `\xee\x86\x8e` | Action |
| check | 57452 | 0xE06C | `\xee\x81\xac` | Status |
| x | 57778 | 0xE1B2 | `\xee\x86\xb2` | Status |
| check-circle | 57468 | 0xE07C | `\xee\x81\xbc` | Status |
| x-circle | 57476 | 0xE084 | `\xee\x82\x84` | Status |
| settings | 57684 | 0xE154 | `\xee\x85\x94` | System |

## Usage Examples

### In C code:

```c
// Create a button with a chevron-up icon
ui2_button_t *btn = ui2_button_create(x, y, width, height, "\xee\x81\xb0");

// Create a button with a search icon (magnifying glass)
ui2_button_t *btn = ui2_button_create(x, y, width, height, "\xee\x85\x91");

// Create a button with an x-circle icon
ui2_button_t *btn = ui2_button_create(x, y, width, height, "\xee\x82\x84");
```

### Common Use Cases

- **Navigation buttons**: chevron-up, chevron-down, chevron-left, chevron-right
- **Exit/close buttons**: x-circle
- **Confirmation buttons**: check-circle
- **Search functionality**: search
- **File operations**: file-plus, folder-plus, copy, trash-2, download, upload
- **Settings**: settings
- **Home screen**: home
- **Menu**: menu

## Notes

- Icons are embedded in all font .fpack bundles (sizes 6-14px)
- All fonts support these 34 Lucide icons
- Icons render as regular text characters - no special handling required
- **UTF-8 escape sequences must be 3 bytes for codepoints in E000-FFFF range**
- Use `chr(cp).encode('utf-8')` to get proper UTF-8 encoding