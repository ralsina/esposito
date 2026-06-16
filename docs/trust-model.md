# Trust Model

Esposito is a personal device. Its security model follows the same philosophy as Palm OS, classic Mac OS, and MS-DOS: applications are fully trusted, and physical access is root. This document describes what that means in practice, what is and is not protected, and how to recover from a misbehaving app.

## Guiding principles

1. **Physical access is root.** Anyone with the SD card or a USB cable can do anything, including replacing firmware. This is by design.
2. **The network is untrusted.** Data arriving over WiFi — including firmware update offers — is treated as hostile until verified.
3. **Apps are fully trusted.** Installing an app is equivalent to flashing firmware: the app can do anything the OS can do. Install apps only from sources you control.

## What apps can do

An app linked against the Esposito OS API has the full run of the device:

- Read and write any file on the SD card, including other apps' state and OS configuration.
- Allocate memory, spawn FreeRTOS tasks, and call the full OS API surface (`fopen`, `display_*`, `wifi_*`, `os_http_*`, etc.).
- Subscribe to and post events.
- Crash the device (e.g., dereference a null pointer, overflow a buffer, deadlock).

There is **no sandboxing**. This is a deliberate choice: on a 4 MB-flash 320×240 personal device, sandboxing would impose costs (permissions, brokers, context switches, larger firmware) that exceed the benefit for a single-user system. The cost is that you must trust every app you install, the same way you trust every program you run on a classic Mac OS or MS-DOS machine.

## What the OS hardens against

The OS does not attempt to protect itself from malicious apps, but it does harden against **non-malicious failure modes** — corrupted files, half-written downloads, buggy apps that hand the OS bad data.

### ELF loader robustness

*Status: implemented.*

The ELF loader in `main/elf_loader.c` bounds-checks every field it reads from a `program.elf` file before using it. A corrupted ELF (bad SD card sector, interrupted `cp`, half-written upload) produces a clean load failure rather than an out-of-bounds read, OOM, or crash. The device stays usable; only the bad app fails to launch.

### Network firmware updates (OTA)

*Status: implemented.*

Firmware delivered over the network is **cryptographically signed**. The device:

1. Asks the GitHub releases API for the latest (or latest pre-release) version.
2. Compares the offered version against the running version and refuses to downgrade over the network.
3. Downloads `firmware.bin` plus its detached ECDSA-P256 signature.
4. Verifies the signature against a public key compiled into the firmware before applying the update.

A network attacker who MITMs the connection, compromises the CDN, or steals a TLS certificate cannot install persistent firmware without the offline ECDSA private key.

### Manual firmware install via SD card

*Status: implemented.*

As an escape hatch and downgrade path, the user can copy any valid ESP32 image to `/sdcard/system/firmware.bin` and reboot. The OS applies it without signature verification. This is the **trusted physical-access path**: if you can write to the SD card, you can install any firmware, signed or not, newer or older. This is intentional — it is how recovery and downgrade work without depending on the signing key. This path will remain unsigned even after OTA signing lands.

### Network credentials

*Status: implemented.*

WiFi SSID and password are stored in the device's NVS flash partition, not on the SD card, and are not exposed through any OS API. Apps can ask whether WiFi is connected (`wifi_is_connected`), scan for networks (`wifi_scan`), and request connection/disconnection, but they cannot read the stored credentials. Anyone with physical access to the chip's flash can still extract them — physical access is root — but a casual app cannot exfiltrate them via `fopen("/sdcard/...")`.

## What is not protected

Be honest about the gaps:

- **App data on SD is world-readable and world-writable by every app.** App A can read, modify, or delete App B's saved state, configuration, and documents.
- **A stolen SD card reveals all app data.** Photos, documents, app settings, downloaded books — everything an app has written to SD is on the removable card in plain FAT.
- **Flash is not encrypted.** No secure boot, no flash encryption. Anyone with a flash dumper and physical access can read the firmware image, NVS contents (including WiFi creds), and any internal-storage data.
- **A buggy or hostile app can brick the device** until you remove it from the SD card via a PC. Crash-loop detection at boot (`boot.cpp` `boot_check_crash_loop`) drops into the launcher if the last auto-restored app caused three rapid reboots, but if the launcher itself is broken you'll need to fix the SD card from a PC.
- **No rate limiting or quota on OS APIs.** An app can spam the event queue, exhaust the app heap, or saturate WiFi in a tight loop.

## Recovery

If an app misbehaves:

1. **App auto-restored at boot crashes repeatedly:** The OS remembers the last app you launched and re-launches it on the next boot. If the device reboots three times within 15 seconds, the OS skips the auto-restore and drops you into the launcher instead (see `boot.cpp` `boot_check_crash_loop`). From there you can pick a different app or use the file manager (from a PC) to remove the bad app from the SD card.
2. **Crashes while running:** Reboot the device. The app is not re-launched automatically; you'll return to the launcher and can pick a different app.
3. **Corrupts SD data:** Restore from a backup. The OS does not keep app-level snapshots.
4. **Bricked firmware:** Copy a known-good `firmware.bin` to `/sdcard/system/firmware.bin`, insert the SD card, and reboot. The SD-card install path applies it without question.

## Summary table

| Asset / surface | Threat | Protection | Status |
|---|---|---|---|
| App-to-app data on SD | Malicious app | None (apps are trusted) | by design |
| OS from app misbehavior | Buggy app | ELF loader bounds-checks; crash-loop safe mode | implemented |
| Firmware over network | MITM / compromised CDN | ECDSA-P256 signature required | implemented |
| Firmware via SD | N/A | Unsigned by design (physical access = root) | implemented |
| WiFi credentials | App exfiltration | NVS storage; not exposed via API | implemented |
| WiFi credentials | Physical theft | None (no flash encryption) | by design |
| App data on SD | Physical theft | None (removable plain FAT) | by design |

## See also

- [docs/app-loading.md](app-loading.md) — how ELF binaries are loaded.
- [docs/http-access.md](http-access.md) — the network API surface available to apps.
- [README.md](../README.md) — overview and architecture.
