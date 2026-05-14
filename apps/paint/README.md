# Esposito Paint

A touch-first pixel paint app for Esposito OS on ESP32/CYD2USB.

The app uses a 16-color indexed canvas and is designed to be fast on-device, including live line/rectangle previews while dragging.

## Features

- Full-screen 320x240 16-color indexed drawing
- Pencil and eraser with continuous touch interpolation
- Line and rectangle tools with live drag preview overlay
- Single-step undo (full canvas snapshot)
- Clear canvas
- Save/load project to SD card

## UI Layout

- Top toolbar buttons:
  - `PEN` pencil
  - `ERS` eraser
  - `LIN` line
  - `REC` rectangle
  - `CLR` clear canvas
  - `UND` undo
  - `SAV` save
  - `LOD` load
  - `EXT` exit app
- Bottom bar: 16-color palette
- Middle: drawable canvas area

## Touch Workflow

- Pencil/Eraser:
  - press + drag to draw/erase
- Line/Rectangle:
  - press to set start point
  - drag to preview shape
  - release to commit shape

## Save Format

Projects are saved as `.pt16` files (default path: `/sdcard/paint_last.pt16`).

The file contains:

- magic header (`PT16`)
- width/height
- packed 4bpp indexed canvas payload

## Notes

- Preview rendering uses a lightweight overlay buffer so dragging does not require full-screen redraws.
- The app stores one undo snapshot, balancing responsiveness and RAM use on ESP32.