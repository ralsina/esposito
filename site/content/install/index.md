---
title: Install Esposito OS
---

Flash your ESP32 CYD directly from your browser — no toolchain, no terminal.
You need a USB cable and Chrome or Edge.

<esp-web-install-button manifest="/firmware/manifest.json">
    <button slot="activate" class="primary" style="font-size: 1.2rem; padding: 0.6rem 2rem;">
        🚀 Connect &amp; Install
    </button>
    <span slot="unsupported">⚠️ Your browser doesn't support Web Serial. Please use <strong>Google Chrome</strong> or <strong>Microsoft Edge</strong> on desktop.</span>
    <span slot="not-allowed">🔒 Connect the ESP32 to this computer via USB first.</span>
</esp-web-install-button>

<script type="module" src="https://unpkg.com/esp-web-tools@9/dist/web/install-button.js"></script>

<h2 id="Prerequisites">Prerequisites</h2>

1. **Hardware**: A [Cheap Yellow Display (CYD)](https://es.aliexpress.com/w/wholesale-2432s028.html) — the 2-USB variant with ST7789 touchscreen. (~$10)
2. **USB cable**: A data-capable USB-C or micro-USB cable (many cables are charge-only).
3. **Browser**: Google Chrome or Microsoft Edge (desktop). The Web Serial API is required; Firefox and Safari don't support it yet.
4. **Drivers** (some platforms): If your CYD has a CH340 USB-to-serial chip, you may need drivers:
   - **Windows**: Drivers from [WCH](https://www.wch-ic.com/downloads/CH341SER_EXE.html)
   - **macOS**: Recent versions include CH340 drivers; if not, install from WCH.
   - **Linux**: Usually built into the kernel already.

## After Install

1. Download [`sdcard-bundle.zip`](https://github.com/ralsina/esposito/releases/latest/download/sdcard-bundle.zip) from the latest release
2. Format a microSD card as **FAT32**
3. Extract the zip to the root of the SD card — you'll get `apps/`, `fonts/`, and `books/` directories
4. Insert the SD card into the CYD and power on — Esposito boots in a few seconds and shows the launcher

The bundle includes 8 apps (launcher, settings, reader, clock, calculator, snake, breakout, file manager), all system fonts, and 4 classic books to get you started. You can later download more apps and books!

<details>
<summary><strong>What if I don't have an SD card?</strong></summary>

The firmware runs without an SD card, but you'll have no apps, no fonts beyond the boot font, and no books. The SD card is essential for the full experience.

</details>

## OTA Updates

Once Esposito OS is running, updates are delivered **over WiFi**:

1. Connect the device to WiFi via Settings
2. Open **Settings → Update** to check for a newer release
3. Firmware is downloaded, **signature-verified** against the embedded public key, and applied

No browser needed for updates after the initial install.

<h2 id="Troubleshooting">Troubleshooting</h2>

<details>
<summary><strong>The button doesn't do anything</strong></summary>

Make sure you're using Chrome or Edge (desktop). Check that the page is served over HTTPS. Try refreshing — the Web Serial prompt sometimes needs a second attempt.
</details>

<details>
<summary><strong>"No serial port selected" or connection fails</strong></summary>

- Use a **data** USB cable, not a charge-only one.
- On Linux, add your user to the `dialout` or `tty` group: `sudo usermod -aG dialout $USER` then log out/in.
- On macOS, close any other serial monitor (Arduino IDE, `idf.py monitor`, `screen`).
- Try a different USB port (avoid hubs).
</details>

<details>
<summary><strong>Flash fails partway through</strong></summary>

The ESP32 flash is 4 MB; the full image is under 2 MB so there's room. If it fails:
1. Hold the **BOOT** button on the CYD, press **RST**, then release BOOT (puts it in download mode)
2. Click the install button again
</details>

<details>
<summary><strong>Screen is blank / white after install</strong></summary>

You may have the original CYD (1-USB) instead of the 2-USB variant — they use different screen controllers. Check the [README](https://github.com/ralsina/esposito) for hardware details.
</details>

<details>
<summary><strong>I want to flash manually with esptool / idf.py</strong></summary>

Download the four `.bin` files from the [latest release](https://github.com/ralsina/esposito/releases/latest) and flash at the offsets above:

```bash
esptool.py --port /dev/ttyUSB0 write_flash \
  0x1000 bootloader.bin \
  0x8000 partition-table.bin \
  0x10000 firmware.bin \
  0x210000 ota_data_initial.bin
```
</details>
