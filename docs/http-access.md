# HTTP/S Access Patterns

This document explains the supported ways for apps to access HTTP and HTTPS data in Esposito, and when to use each API.

## Summary

Esposito provides four HTTP-related APIs in `os_core.h`:

- `os_http_get(url, out, out_size, timeout_ms)`
- `os_http_post(url, post_data, extra_headers, ca_pem, out, out_size, timeout_ms)`
- `os_http_download(url, path, progress_cb)`
- `os_download_via_os(url, path, expected_size)`

The first three perform the transfer in the currently running app context.
`os_download_via_os` delegates the transfer to the OS runtime.
The `expected_size` argument is optional metadata; pass `0` when unknown.

## 1) `os_http_get`: small responses in memory

Use for JSON APIs, metadata, and short text replies.

- Returns number of bytes written to `out` on success.
- Returns negative value on failure.
- `out` is caller-provided memory.
- `timeout_ms <= 0` uses default behavior.

Typical use:

```c
char response[2048];
int n = os_http_get("https://example.com/api", response, sizeof(response), 10000);
if (n > 0) {
    // response contains n bytes (NUL-terminated when possible)
} else {
    // handle error
}
```

When not to use:

- Large payloads that can fragment app heap.
- File downloads that should survive app lifecycle transitions.

## 2) `os_http_post`: request/response APIs

Use for REST-style POST endpoints.

- Supports custom headers via `extra_headers`.
- Supports explicit CA bundle/pem via `ca_pem` when needed.
- Returns response length on success, negative value on failure.

Typical use:

```c
const char *headers[] = {
    "Content-Type: application/json",
    NULL
};
char out[4096];
int n = os_http_post(
    "https://example.com/submit",
    "{\"value\":123}",
    headers,
    NULL,
    out,
    sizeof(out),
    15000
);
```

When not to use:

- Large binary uploads/downloads.
- Workloads that need OS-owned memory headroom.

## 3) `os_http_download`: direct file download from app context

Use when you want a file on SD and can afford app-context memory pressure.

- Streams to file path on SD (or mounted FS).
- Optional progress callback receives percent/status hints.
- Returns downloaded byte count on success.

Typical use:

```c
static void progress(int percent, const char *status) {
    (void)status;
    // update UI
}

int bytes = os_http_download(
    "https://example.com/file.bin",
    "/sdcard/data/file.bin",
    progress
);
```

Notes:

- Better than `os_http_get` for big payloads because data is streamed.
- Still runs while your app is loaded, so TLS and app memory share limits.

## 4) `os_download_via_os`: recommended for large HTTPS downloads

This is the preferred path for larger files and fragile HTTPS conditions.

What happens:

1. App calls `os_download_via_os(url, path, expected_size)`.
2. OS queues the request.
3. OS unloads the app and releases app heap.
4. OS performs the download with maximum available memory.
5. OS relaunches requesting app and stores result in app config keys.

Return value from `os_download_via_os`:

- `true`: request queued successfully.
- `false`: invalid args, no current app, or another OS download active.

Result handoff keys (in requesting app config scope):

- `os/download_result` (int)
  - `> 0`: downloaded byte count
  - `< 0`: failure code
- `os/download_path` (string)
  - Set on successful download

Typical app pattern:

```c
if (!os_download_via_os(url, path, known_size_or_zero)) {
    // queue failed immediately
    return;
}

// return to event loop; app will be relaunched later
```

On relaunch:

```c
int result = appcfg_get_int("os/download_result", 0);
if (result > 0) {
    char saved_path[320];
    appcfg_get_string("os/download_path", "", saved_path, sizeof(saved_path));
    // success
} else if (result < 0) {
    // failed
}
config_delete("os/download_result");
config_delete("os/download_path");
```

## Choosing the right API

Use this rule of thumb:

- Small in-memory response: `os_http_get`
- POST with response body: `os_http_post`
- File download while app stays active: `os_http_download`
- Large/critical HTTPS file transfer: `os_download_via_os`

## HTTPS and time sync

HTTPS certificate validation depends on system time.
If time is not synchronized yet, TLS may fail or become flaky.

Recommendations:

- Prefer waiting for time sync before critical HTTPS operations.
- Retry operations after WiFi/NTP is ready.
- For large HTTPS files, prefer OS-delegated downloads.

## Reliability guidance for apps

- Keep response buffers conservative for GET/POST.
- Prefer file streaming over large in-memory payloads.
- Avoid starting multiple heavy network operations at once.
- Use clear UI states: cache parse vs queued OS download vs completed.
- Always handle and clear persisted download result keys after relaunch.

## Related docs

- [App Loading](app-loading.md)
- [App Entry Point](app.c.md)
