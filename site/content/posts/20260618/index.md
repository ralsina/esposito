---
title: ESP-Osito News for June 18, 2026
date: 2026-06-18 18:00:00 -03:00
---

## One-Click Browser Installation

You can now flash Esposito OS directly from your browser. No ESP-IDF install,
no toolchain, no terminal. Just:

1. Plug your Cheap Yellow Display into a USB cable
2. Open Chrome or Edge on your desktop
3. Go to **[esposito.ralsina.me/install/](/install/)**
4. Click **"Connect & Install"**

The browser handles everything — bootloader, partition table, and firmware —
using [ESP Web Tools](https://esphome.github.io/esp-web-tools/). The binaries
are served from the same domain, so there are no CORS issues.

## SD Card Bundle

Each release now includes a **`sdcard-bundle.zip`** with everything you need
to get a fully working device:

- **8 apps**: launcher, settings, reader, clock, calculator, snake, breakout,
  file manager
- **54 font packs**: every font in every size
- **4 free books**: Alice in Wonderland, Sherlock Holmes, The Great Gatsby,
  and The Picture of Dorian Gray

Just format a microSD as FAT32, extract the zip to the root, and insert it.

## App Store Updates on Release

The on-device App Store now ships with every release. When we tag a new
version, all 18 launcher apps are rebuilt and the catalog is regenerated
automatically. This means the apps you download from the device always
match the firmware version you're running.

## One-Command Releases

The whole release pipeline — version bump, CI build, signing, asset upload,
site update, and production deploy — now runs from a single command:

```bash
scripts/release.sh 0.5.4
```

It even waits for GitHub Actions to finish, downloads the artifacts, and
deploys the site via rsync. No more manual steps.
