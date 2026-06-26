---
title: Install Esposito OS
---

Flash your ESP32 directly from your browser — no toolchain, no terminal.
You need a USB cable and Chrome or Edge.

## Choose Your Board

<div class="board-selector">
  <input type="radio" name="board" id="board-cyd" value="cyd" checked hidden>
  <label for="board-cyd" class="board-option" onclick="switchBoard('cyd')">
    <strong>CYD 2-USB</strong>
    <small>ESP32, 320×240 ST7789, ~$10</small>
  </label>

  <input type="radio" name="board" id="board-guition" value="guition" hidden>
  <label for="board-guition" class="board-option" onclick="switchBoard('guition')">
    <strong>Guition JC4827W543</strong>
    <small>ESP32-S3, 480×272 NV3041A, ~$18</small>
  </label>
</div>

<div id="install-cyd" class="install-block">
  <esp-web-install-button manifest="/firmware/manifest.json">
    <button slot="activate" class="primary" style="font-size: 1.2rem; padding: 0.6rem 2rem;">
      🚀 Install on CYD (ESP32)
    </button>
    <span slot="unsupported">⚠️ Your browser doesn't support Web Serial. Please use <strong>Google Chrome</strong> or <strong>Microsoft Edge</strong> on desktop.</span>
    <span slot="not-allowed">🔒 Connect the ESP32 to this computer via USB first.</span>
  </esp-web-install-button>
</div>

<div id="install-guition" class="install-block" style="display:none">
  <esp-web-install-button manifest="/firmware/manifest.json">
    <button slot="activate" class="primary" style="font-size: 1.2rem; padding: 0.6rem 2rem;">
      🚀 Install on Guition (ESP32-S3)
    </button>
    <span slot="unsupported">⚠️ Your browser doesn't support Web Serial. Please use <strong>Google Chrome</strong> or <strong>Microsoft Edge</strong> on desktop.</span>
    <span slot="not-allowed">🔒 Connect the ESP32-S3 to this computer via USB first.</span>
  </esp-web-install-button>
</div>

<script type="module" src="https://unpkg.com/esp-web-tools@9/dist/web/install-button.js"></script>
<script>
function switchBoard(board) {
  document.getElementById('install-cyd').style.display = board === 'cyd' ? 'block' : 'none';
  document.getElementById('install-guition').style.display = board === 'guition' ? 'block' : 'none';
}
</script>

<h2 id="Prerequisites">Prerequisites</h2>

<div class="board-prereqs">
<div class="prereq-block">
<h3>CYD 2-USB</h3>
<ol>
  <li><strong>Hardware</strong>: A <a href="https://es.aliexpress.com/w/wholesale-2432s028.html">Cheap Yellow Display (CYD)</a> — the 2-USB variant with ST7789 touchscreen.</li>
  <li><strong>USB cable</strong>: USB-C or micro-USB (data-capable).</li>
  <li><strong>Drivers</strong>: Some CYD boards use a CH340 USB-serial chip; <a href="https://www.wch-ic.com/downloads/CH341SER_EXE.html">drivers</a> may be needed on Windows.</li>
</ol>
</div>

<div class="prereq-block">
<h3>Guition JC4827W543</h3>
<ol>
  <li><strong>Hardware</strong>: A Guition JC4827W543 (ESP32-S3, 480×272, NV3041A QSPI display).</li>
  <li><strong>USB cable</strong>: USB-C (data-capable).</li>
  <li><strong>Note</strong>: USB CDC is enabled — no extra drivers needed; serial shows up automatically.</li>
</ol>
</div>
</div>

## After Install

1. Download <a href="https://github.com/ralsina/esposito/releases/latest/download/sdcard-bundle.zip"><code>sdcard-bundle.zip</code></a> from the latest release
2. Format a microSD card as <strong>FAT32</strong>
3. Extract the zip to the root of the SD card — you'll get <code>apps/</code>, <code>fonts/</code>, and <code>books/</code> directories
4. Insert the SD card and power on

The bundle includes 8 apps (launcher, settings, reader, clock, calculator, snake, breakout, file manager), all system fonts, and 4 classic books to get you started.

<details>
<summary><strong>What if I don't have an SD card?</strong></summary>

The firmware runs without an SD card, but you'll have no apps, no fonts beyond the boot font, and no books.
</details>

## OTA Updates

Once Esposito OS is running, updates are delivered <strong>over WiFi</strong>:

1. Connect the device to WiFi via Settings
2. Open <strong>Settings → Update</strong> to check for a newer release
3. Firmware is downloaded, <strong>signature-verified</strong> against the embedded public key, and applied

No browser needed for updates after the initial install.

<h2 id="Troubleshooting">Troubleshooting</h2>

<details>
<summary><strong>The button doesn't do anything</strong></summary>

Make sure you're using Chrome or Edge (desktop). Check that the page is served over HTTPS. Try refreshing — the Web Serial prompt sometimes needs a second attempt.
</details>

<details>
<summary><strong>"No serial port selected" or connection fails</strong></summary>

- Use a <strong>data</strong> USB cable, not a charge-only one.
- On Linux, add your user to the <code>dialout</code> or <code>tty</code> group: <code>sudo usermod -aG dialout $USER</code> then log out/in.
- On macOS, close any other serial monitor (Arduino IDE, <code>idf.py monitor</code>, <code>screen</code>).
- Try a different USB port (avoid hubs).
</details>

<details>
<summary><strong>Flash fails partway through</strong></summary>

The flash is 4 MB; the full image is under 2 MB so there's room. If it fails:
1. Hold the <strong>BOOT</strong> button, press <strong>RST</strong>, then release BOOT (download mode)
2. Click the install button again
</details>

<details>
<summary><strong>Screen is blank / white after install</strong></summary>

You may have selected the wrong board, or your CYD is the 1-USB variant (different screen controller). Check the <a href="https://github.com/ralsina/esposito">README</a> for hardware details.
</details>

<details>
<summary><strong>Manual flash with esptool</strong></summary>

Download the four <code>.bin</code> files from the <a href="https://github.com/ralsina/esposito/releases/latest">latest release</a> and flash:

<div class="flash-command">
<strong>CYD (ESP32):</strong>
<pre><code>esptool.py --chip esp32 --port /dev/ttyUSB0 write_flash \
  0x1000 bootloader.bin \
  0x8000 partition-table.bin \
  0x10000 firmware.bin \
  0x210000 ota_data_initial.bin</code></pre>
</div>

<div class="flash-command">
<strong>Guition (ESP32-S3):</strong>
<pre><code>esptool.py --chip esp32s3 --port /dev/ttyUSB0 write_flash \
  0x0 bootloader.bin \
  0x8000 partition-table.bin \
  0x10000 firmware.bin \
  0x210000 ota_data_initial.bin</code></pre>
</div>
</details>
