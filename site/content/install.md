---
title: Install Esposito OS
---

<section class="hero-section" style="position: relative;">
    <h2 style="color: var(--b16-base0D);">⚡ Install Esposito OS</h2>
    <p>Flash your ESP32 CYD (Cheap Yellow Display) directly from your browser.
    No toolchain, no drivers, no terminal — just a USB cable and Chrome or Edge.</p>

  <div class="hero-actions">
      <a href="#Prerequisites" class="primary contrast" role="button">Prerequisites</span>
      <a href="#Troubleshooting" role="button" class="contrast">Troubleshooting</a>
      <a href="https://github.com/ralsina/esposito" role="button" class="contrast">GitHub</a>
  </div>
</section>

## Browser Install

<esp-web-install-button manifest="https://github.com/ralsina/esposito/releases/latest/download/manifest.json">
    <button slot="activate" class="primary" style="font-size: 1.2rem; padding: 0.6rem 2rem;">
        🚀 Connect &amp; Install
    </button>
    <span slot="unsupported">
        <p>⚠️ Your browser doesn't support Web Serial. Please use <strong>Google Chrome</strong> or <strong>Microsoft Edge</strong> on desktop. (ChromeOS, Linux, macOS, and Windows all work.)</p>
    </span>
    <span slot="not-allowed">
        <p>🔒 You need to connect the ESP32 to <em>this</em> computer via USB first.</p>
    </span>
</esp-web-install-button>

<script type="module" src="https://unpkg.com/esp-web-tools@9/dist/web/install-button.js?module"></script>

The button above connects to your ESP32 over USB (Web Serial), flashes the complete firmware image — bootloader, partition table, Esposito OS, and OTA data — and you're ready to go.

<h2 id="Prerequisites">Prerequisites</h2>

1. **Hardware**: A [Cheap Yellow Display (CYD)](https://es.aliexpress.com/w/wholesale-2432s028.html) — the 2-USB variant with ILI9341 touchscreen. (~$10)
2. **USB cable**: A data-capable USB-C or micro-USB cable (many cables are charge-only).
3. **Browser**: Google Chrome or Microsoft Edge (desktop). The Web Serial API is required; Firefox and Safari don't support it yet.
4. **Drivers** (some platforms): If your CYD has a CH340 USB-to-serial chip, you may need drivers:
   - **Windows**: Drivers from [WCH](https://www.wch-ic.com/downloads/CH341SER_EXE.html)
   - **macOS**: Recent versions include CH340 drivers; if not, install from WCH.
   - **Linux**: Usually built into the kernel already.

## What Gets Installed

| Component | Offset | Size |
|-----------|--------|------|
| Bootloader | `0x1000` | ~40 KB |
| Partition table | `0x8000` | 3 KB |
| **Esposito OS** (factory app) | `0x10000` | ~2 MB |
| OTA data (initial) | `0x210000` | 8 KB |

The full image is generated automatically by our [release workflow](https://github.com/ralsina/esposito/actions) for every tagged release.

## After Install

1. Put apps on an SD card (FAT32) under `/apps/<name>/program.elf`
2. Insert the SD card into the CYD
3. Power on — Esposito boots in under a second and shows the launcher

For app files and the catalog, see the [app store](/) or [GitHub](https://github.com/ralsina/esposito/tree/main/apps).

## OTA Updates

Once Esposito OS is running, updates are delivered **over WiFi**:

1. Connect the device to WiFi via Settings
2. The OS checks GitHub Releases on boot
3. Firmware is downloaded, **signature-verified** against the embedded public key, and applied

No browser needed for updates after the initial install.

<h2 id="Troubleshooting">Troubleshooting</h2>

<details>
<summary><strong>The button doesn't do anything</strong></summary>

Make sure you're using Chrome or Edge (desktop). Check that the page is served over HTTPS (this site is). Try refreshing — the Web Serial prompt sometimes needs a second attempt.
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

You may have the original CYD (1-USB) instead of the 2-USB variant — they use different screen controllers. Check the [hardware notes](https://github.com/ralsina/esposito/blob/main/docs/trust-model.md) and the README.
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
