# Image Viewer

A JPEG image viewer for Esposito OS. Opens `.jpg` and `.jpeg` files from the SD card and displays them fit to the 320x240 screen.

This app is hidden from the launcher (`launcher=no`); open images via the file manager's "open with" menu.

## Build

```sh
bash scripts/build_app.sh apps/image_viewer/app.c
```

Then copy to SD card:

```text
/sdcard/apps/image_viewer/program.elf
```

And make sure the manifest is present:

```text
/sdcard/apps/image_viewer/manifest.cfg
```
