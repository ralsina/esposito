---
title: ESP-Osito News for June 11, 2026
date: 2026-06-11 12:00:00 -03:00
---

## Bookshop: Integrated Book Discovery and Download

The **Bookshop** app is now tightly integrated with the **Reader**, allowing you to discover, download, and read books in a single unified workflow:

- **Browse Catalog** — Bookshop fetches a fresh catalog from the network (or uses a 24-hour cache), displaying up to 100 freely available books with titles, authors, and file sizes
- **Download with Progress** — When you select a book, Bookshop passes the expected file size from metadata to the OS download system, enabling accurate progress bars and reliable resume-on-failure
- **Return to Reading** — Exiting Bookshop automatically returns you to the Reader instead of the launcher, thanks to the new app stack navigation
- **Larger Library** — The catalog parser capacity has been expanded from 50 to 100 books, supporting much larger project catalogs

The typical flow is: open Reader → discover you're out of books → launch Bookshop → find a new book → download with reliable progress → exit Bookshop → back to Reader, ready to read

Because this involves downloading the books, the OS download system has been significantly hardened to handle large files and unstable network conditions:

- **Reconnect and Resume**: When a download fails mid-transfer due to TLS read errors, the OS now reconnects with HTTP Range headers to resume from the last successfully received byte, rather than starting over.
- **Multiple Retry Attempts**: Downloads attempt up to 4 connection cycles before giving up, with 1-second delays between attempts.
- **Expected Size Metadata**: Apps can now pass an optional file size hint when queuing OS downloads via `os_download_via_os(url, path, expected_size)`. This enables accurate progress percentage display even when the HTTP server omits Content-Length headers.
- **Fallback Progress Reporting**: When total size is unknown, the UI shows bytes downloaded (KB) instead of a stuck percentage, keeping users informed of progress.

This makes large book downloads much more reliable, especially over WiFi connections that drop or stall mid-transfer.

## App Stack Navigation

Apps now maintain a navigation stack, so exiting an app returns you to the previous app instead of always going back to the launcher:

```
launcher → reader → bookshop → (exit) → reader → (exit) → launcher
```

The **Bookshop** app can now be launched from the **Reader** to browse and download additional books, and exiting Bookshop automatically returns you to the reader. This creates a natural, Palm OS-like workflow where apps remember what came before them.

Explicit loads to launcher still clear the stack (so launcher serves as a root navigation point).

## New Documentation

Two new developer guides have been added:

**[HTTP/S Access Patterns](https://github.com/ralsina/esposito/blob/main/docs/http-access.md)** documents the four supported HTTP APIs and when to use each:

- `os_http_get()` — small in-memory responses
- `os_http_post()` — REST-style POST with custom headers
- `os_http_download()` — streaming file download (app context)
- `os_download_via_os()` — **recommended for large files** (OS-delegated with full memory headroom)

The guide includes practical C examples, reliability best practices, and guidance on HTTPS/NTP time synchronization for certificate validation.

**[App Loading](https://github.com/ralsina/esposito/blob/main/docs/app-loading.md)** explains how ELF binaries are loaded at runtime, how the symbol table works for app-to-OS communication, and how to structure app projects with dependencies and linker scripts.

## Shell View Command

The Shell app now supports a `view` command to quickly display file contents on screen:

```
> view /sdcard/notes.txt
```

This is useful for inspecting app output, configuration files, and download results without leaving the shell.
