# Esposito OS Documentation

Architecture and design documents for Esposito OS.

## Architecture & Overview

- [architecture.html](architecture.html) - Interactive HTML visualization of the OS architecture, components, and data flow
- [trust-model.md](trust-model.md) - Security model: physical access is root, the network is untrusted, and apps are fully trusted. What is and isn't protected, and how to recover from a misbehaving app
- [app-loading.md](app-loading.md) - The full app lifecycle pipeline: compilation to ELF, dynamic loading and relocation from the SD card, the per-app heap, the symbol table, the manifest system, and app switching

## App Development

- [sdk.md](sdk.md) - Building Esposito apps with the standalone SDK (no firmware checkout required): setup, app structure, capabilities, and the available OS API surface
- [http-access.md](http-access.md) - The four HTTP/HTTPS APIs (`os_http_get`, `os_http_post`, `os_http_download`, `os_download_via_os`), what each does, and when to use which
- [app.c.md](app.c.md) - A literate-programming walkthrough of the snake game's source code ("Snakes and Ositos") — a beginner-friendly tour of how an Esposito app is structured

## Reference

- [icons.md](icons.md) - The 34 Lucide icons available in apps, with Unicode codepoints, hex values, and UTF-8 escape sequences for use in C code
