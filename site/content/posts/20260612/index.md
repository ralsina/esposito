---
title: ESP-Osito News for June 12, 2026
date: 2026-06-12 12:00:00 -03:00
---

## App Store

The **App Store** is a new app that lets you discover, install, update, and uninstall apps directly on the device. No more manual ELF copying to the SD card.

When you open it, the App Store fetches the catalog from `esposito.ralsina.me/apps/catalog.json` (cached for 24 hours) and shows available apps with status:

- **` `** (blank) = already installed
- **`+`** = new, not installed
- **`~`** = update available

{{% figure src="/posts/20260612/app_store1.png" caption="App Store: list of available apps" link="/posts/20260612/app_store1.png" %}}

Tap an app to see its detail page with description, version, homepage, and required capabilities. The bottom toolbar offers Install, Update, or Uninstall. If the app requires hardware you don't have (like WiFi or PSRAM), a warning is shown — but it doesn't block installation (you can add hardware later).

{{% figure src="/posts/20260612/app_store2.png" caption="App Store: detail view for cclock" link="/posts/20260612/app_store2.png" %}}

### How it works

- **`scripts/generate_catalog.py`** scans all `apps/*/manifest.cfg` files, builds `catalog.json`, and copies ELFs + manifests to `site/assets/apps/` for hosting
- The App Store downloads the catalog, parses it once to build an index (app id, name, requires, version, size), and re-reads it lazily from cache when showing detail pages — no persistent JSON buffer
- Installing downloads the ELF via the OS download system, then writes `manifest.cfg` directly from catalog metadata
- Uninstall removes the app directory from the SD card
- The catalog cache lives on the SD card and refreshes every 24 hours (same pattern as Bookshop)

### Capability System

Apps can declare `requires=keyboard,touch,wifi,psram` in their manifest. The firmware's `os_has_capability()` checks these at app load time, so an app needing WiFi simply won't start unless WiFi hardware is available. The App Store warns about missing capabilities but allows installation anyway — you might add a WiFi module later.

All 23 existing apps have updated manifests with proper `short_description`, `long_description`, `homepage`, and `version` fields.
