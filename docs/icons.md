# Lucide Icon Reference

This document documents the 30 Lucide icons available for use in Esposito apps.

## Icon Mappings

Lucide icons are rendered using icon glyphs embedded in all font .fpack bundles. Icons can be used directly in UI buttons and labels using their Unicode codepoints.

| Icon Name | Unicode | Hex | UTF-8 Escape | Icon Type |
|-----------|---------|-----|--------------|-----------|
| chevron-up | 57488 | 0xE070 | `\xe0\x70` | Navigation |
| chevron-down | 57485 | 0xE06D | `\xe0\x6d` | Navigation |
| chevron-left | 57486 | 0xE06E | `\xe0\x6e` | Navigation |
| chevron-right | 57487 | 0xE06F | `\xe0\x6f` | Navigation |
| arrow-left | 57480 | 0xE048 | `\xe0\x48` | Navigation |
| arrow-right | 57481 | 0xE049 | `\xe0\x49` | Navigation |
| arrow-up | 57482 | 0xE04A | `\xe0\x4a` | Navigation |
| arrow-down | 57474 | 0xE042 | `\xe0\x42` | Navigation |
| home | 57589 | 0xE0F5 | `\xe0\xf5` | Navigation |
| search | 57681 | 0xE151 | `\xe1\x51` | Action |
| book-open | 57471 | 0xE05F | `\xe0\x5f` | Files |
| folder | 57495 | 0xE0D7 | `\xe0\xd7` | Files |
| external-link | 57465 | 0xE0B9 | `\xe0\xb9` | Files |
| file-plus | 57481 | 0xE0C9 | `\xe0\xc9` | Files |
| folder-plus | 57497 | 0xE0D9 | `\xe0\xd9` | Files |
| file-minus | 57478 | 0xE0C6 | `\xe0\xc6` | Files |
| copy | 57470 | 0xE09E | `\xe0\x9e` | Action |
| trash-2 | 57774 | 0xE18E | `\xe1\x8e` | Action |
| trash | 57773 | 0xE18D | `\xe1\x8d` | Action |
| download | 57682 | 0xE0B2 | `\xe0\xb2` | Action |
| upload | 57758 | 0xE19E | `\xe1\x9e` | Action |
| edit-2 | 57679 | 0xE12F | `\xe1\x2f` | Action |
| menu | 57621 | 0xE115 | `\xe1\x15` | Navigation |
| settings | 57772 | 0xE154 | `\xe1\x54` | System |
| save | 57725 | 0xE14D | `\xe1\x4d` | Action |
| check-circle | 57468 | 0xE07C | `\xe0\x7c` | Status |
| x-circle | 57476 | 0xE084 | `\xe0\x84` | Status |

## Usage Examples

### In C code:

```c
// Create a button with a chevron-up icon
ui2_button_t *btn = ui2_button_create(x, y, width, height, "\xe0\x70");

// Create a button with a search icon (magnifying glass)
ui2_button_t *btn = ui2_button_create(x, y, width, height, "\xe1\x51");

// Create a button with an x-circle icon
ui2_button_t *btn = ui2_button_create(x, y, width, height, "\xe0\x84");
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
- UTF-8 escape sequences must be exact (e.g., `\xe0\x70` not `\xEE\x84\x95`)