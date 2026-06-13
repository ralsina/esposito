---
title: ESP-Osito News for June 10, 2026
date: 2026-06-10 12:00:00 -03:00
---

## ui2 Widget Library

The original `libs/ui/` widget toolkit has been superseded by **ui2** (`libs/ui2/`),
a ground-up rewrite with a proper widget hierarchy and event routing model.

Every ui2 widget shares a common base (`ui2_widget_t`) with a virtual method table
for draw, key handling, touch handling, focus, and destruction. This lets widgets
compose naturally — layouts contain lists, tabviews contain layouts, and events
bubble through the tree automatically.

The library provides:

- **Widget base** — position, visibility, enabled state, children array, virtual dispatch
- **Layout** — horizontal and vertical container that forwards events to its children
- **Screen** — full-screen widget with focus management between children
- **Button** — pressable with callback on activation, configurable colors
- **Label** — static text widget
- **List** — scrollable item list with selection, border, title, scrollbar, and callbacks
- **Text Input** — single-line text editor with mask support, configurable via named fields
- **Tab View** — multi-tab container with left-aligned tab strip, divider, and content area
- **Progress Bar** — horizontal progress indicator
- **On-Screen Keyboard** — character-entry overlay for devices without a physical keyboard

Tab view handles keyboard (W/S tab navigation, Enter to activate, A/Esc to return to tabs)
and touch (tap a tab to switch, tap content to activate the focused item). The settings app,
file manager, launcher, and clock all use it.

The old `libs/ui/` is retained for reference but all active apps now depend on `ui2`.

## Lucide Icon Support

The ui2 library bundles a curated set of **Lucide** icons embedded directly in every font
`.fpack` bundle. No separate icon font or runtime loading required.

{{% figure src="/posts/20260610/reader2.png" caption="Icons!" link="/posts/20260610/reader2.png" %}}

Icons are available as C preprocessor constants in `libs/ui2/lucide_icons.h`, using proper
3-byte UTF-8 encoding for the E000-FFFF Private Use Area codepoints. Available icons include:

- **Navigation** — big arrows (filled triangles), chevrons, thin arrows, home, menu
- **Files** — folder, folder-plus, file-plus, file-minus, external-link, book-open
- **Actions** — search, copy, download, upload, save, edit-2, trash
- **Status** — check, x, check-circle, x-circle
- **System** — settings

Apps use them by including the header and passing the constant to display functions:
`display_draw_scaled_text_bg(1, 1, ICON_FOLDER, TEXT_COLOR_WHITE, TEXT_COLOR_BLACK, 2)`.

