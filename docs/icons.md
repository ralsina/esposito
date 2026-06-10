# Lucide Icon Reference

This document documents the 30 Lucide icons available for use in Esposito apps.

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
| chevron-up | 57488 | 0xE070 | `\xee\x81\xb0` | Navigation |
| chevron-down | 57485 | 0xE06D | `\xee\x81\xad` | Navigation |
| chevron-left | 57486 | 0xE06E | `\xee\x81\xae` | Navigation |
| chevron-right | 57487 | 0xE06F | `\xee\x81\xaf` | Navigation |
| arrow-left | 57480 | 0xE048 | `\xee\x81\x88` | Navigation |
| arrow-right | 57481 | 0xE049 | `\xee\x81\x89` | Navigation |
| arrow-up | 57482 | 0xE04A | `\xee\x81\x8a` | Navigation |
| arrow-down | 57474 | 0xE042 | `\xee\x81\x82` | Navigation |
| home | 57589 | 0xE0F5 | `\xee\x83\xb5` | Navigation |
| search | 57681 | 0xE151 | `\xee\x85\x91` | Action |
| book-open | 57471 | 0xE05F | `\xee\x81\x9f` | Files |
| folder | 57495 | 0xE0D7 | `\xee\x83\x97` | Files |
| external-link | 57465 | 0xE0B9 | `\xee\x82\xb9` | Files |
| file-plus | 57481 | 0xE0C9 | `\xee\x83\x89` | Files |
| folder-plus | 57497 | 0xE0D9 | `\xee\x83\x99` | Files |
| file-minus | 57478 | 0xE0C6 | `\xee\x83\x86` | Files |
| copy | 57470 | 0xE09E | `\xee\x82\x9e` | Action |
| trash-2 | 57774 | 0xE18E | `\xee\x86\x8e` | Action |
| trash | 57773 | 0xE18D | `\xee\x86\x8d` | Action |
| download | 57682 | 0xE0B2 | `\xee\x82\xb2` | Action |
| upload | 57758 | 0xE19E | `\xee\x86\x9e` | Action |
| edit-2 | 57679 | 0xE12F | `\xee\x84\xaf` | Action |
| menu | 57621 | 0xE115 | `\xee\x84\x95` | Navigation |
| settings | 57772 | 0xE154 | `\xee\x85\x94` | System |
| save | 57725 | 0xE14D | `\xee\x85\x8d` | Action |
| check-circle | 57468 | 0xE07C | `\xee\x81\xbc` | Status |
| x-circle | 57476 | 0xE084 | `\xee\x82\x84` | Status |

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
- All fonts support these 30 Lucide icons
- Icons render as regular text characters - no special handling required
- **UTF-8 escape sequences must be 3 bytes for codepoints in E000-FFFF range**
- Use `chr(cp).encode('utf-8')` to get proper UTF-8 encoding