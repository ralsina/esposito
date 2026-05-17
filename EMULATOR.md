# Emulator Guide

This document describes how to run and validate Esposito OS in emulator mode.

## Scope

Current emulator support is centered on Wokwi and targets:

- Firmware boot and launcher startup
- Touch-based app navigation
- Basic app workflows (Settings, Clock, Reader)

Keyboard emulation is planned but not yet included in repository assets.

## Prerequisites

- ESP-IDF installed and working
- Wokwi CLI (`wokwi-cli`) or Wokwi Web IDE access
- Repository checked out locally

## Install Wokwi CLI

Official installation methods (Wokwi CI docs):

Linux/macOS:

```bash
curl -L https://wokwi.com/ci/install.sh | sh
```

Windows PowerShell:

```powershell
iwr https://wokwi.com/ci/install.ps1 -useb | iex
```

Alternative method:

- Download the latest binary from Wokwi CLI GitHub releases and place it in your PATH.

Optional for ESP-IDF users:

```bash
pip install idf-wokwi
```

## Build

From repository root:

```bash
idf.py build
```

Expected artifact:

```text
build/esposito.elf
```

## Run with Wokwi CLI

```bash
cd wokwi
wokwi-cli --help
```

For command-specific usage, see the Wokwi CLI usage docs.

Configuration is read from:

- `wokwi/diagram.json`
- `wokwi/.wokwi.build.toml`

The firmware path is configured as `../build/esposito.elf`.

## Run with Wokwi Web IDE

1. Create a new Wokwi project.
2. Replace project diagram with contents of `wokwi/diagram.json`.
3. Build local firmware with `idf.py build`.
4. Upload `build/esposito.elf`.

## Validation Checklist

Run this checklist when emulator support is changed.

1. Boot: launcher appears within 10 seconds.
2. Touch: launcher touch opens an app.
3. Settings: open Settings and change default font.
4. Relayout: Settings remains usable after font switch.
5. Networking: open Clock and trigger weather refresh.
6. Stability: open/close several apps without reset.

## Known Limitations

- BBQ20 keyboard is not currently emulated in repo baseline.
- SD card behavior may differ from physical hardware.
- Timing and touch precision can differ from real device.

## Roadmap

1. Add custom Wokwi part files for BBQ20 keyboard.
2. Wire keyboard I2C lines in `wokwi/diagram.json`.
3. Extend checklist with keyboard-driven test cases.
4. Add CI-friendly emulator smoke checks where feasible.
