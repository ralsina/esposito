# HTTPS Memory Fix Implementation

## Problem
HTTPS requests were failing with error: `mbedtls_ssl_setup returned -0x008D` (MBEDTLS_ERR_SSL_ALLOC_FAILED)

This indicated memory allocation failure during TLS handshake.

## Root Cause
The default TLS configuration used too much memory:
- SSL input buffer: 16384 bytes (too large)
- SSL output buffer: 4096 bytes  
- Certificate bundle: 200 certificates (full Mozilla bundle)
- No dynamic buffer allocation

## Changes Made

### 1. Reduced TLS Buffer Sizes (`sdkconfig`)
```
CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN=4096   # was 16384
CONFIG_MBEDTLS_SSL_OUT_CONTENT_LEN=2048  # was 4096
```

### 2. Enabled Dynamic Buffer Allocation (`sdkconfig`)
```
CONFIG_MBEDTLS_DYNAMIC_BUFFER=y          # was not set
```

### 3. Reduced Certificate Bundle (`sdkconfig`)
```
CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_CMN=y  # use common subset
CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_MAX_CERTS=50   # was 200
```

### 4. Added HTTP Client Buffer Limits (`main/os_core.c`)
```c
esp_http_client_config_t config = {
    .url = url,
    .timeout_ms = timeout_ms > 0 ? timeout_ms : 5000,
    .event_handler = os_http_get_event_handler,
    .user_data = &ctx,
    .crt_bundle_attach = esp_crt_bundle_attach,
    .buffer_size = 1024,        // New: smaller receive buffer
    .buffer_size_tx = 512,      // New: smaller transmit buffer
};
```

### 5. Added Memory Debugging (`main/os_core.c`)
```c
// Log memory before HTTPS request
size_t free_heap = esp_get_free_heap_size();
size_t largest_free = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
ESP_LOGI(TAG, "HTTP: free_heap=%u, largest=%u, url=%s",
         (unsigned)free_heap, (unsigned)largest_free, url);
```

### 6. Enhanced Clock App Debugging (`apps/clock/app.c`)
- Changed weather URLs from HTTP to HTTPS
- Added detailed logging for connection attempts
- Added response data logging for debugging

## Testing Instructions

1. **Flash the updated firmware**:
   ```bash
   . /opt/esp-idf/export.sh
   idf.py flash
   ```

2. **Copy the updated clock app to SD card**:
   ```bash
   mkdir -p /sdcard/apps/clock
   cp build/apps/clock.elf /sdcard/apps/clock/program.elf
   cp apps/clock/manifest.cfg /sdcard/apps/clock/manifest.cfg
   ```

3. **Monitor serial output**:
   ```bash
   idf.py monitor
   ```

4. **Expected output**:
   ```
   HTTP: free_heap=<size>, largest=<size>, url=https://...
   Weather HTTPS attempt=1 result=<bytes> response_start=<data>
   ```

## Success Criteria
- No more `mbedtls_ssl_setup returned -0x008D` errors
- Weather requests succeed with HTTPS URLs
- Memory logging shows adequate free space
- Response data contains valid JSON

## Memory Impact
These changes reduce TLS memory usage by approximately:
- Buffer savings: ~14KB (16KB→4KB + 4KB→2KB)
- Certificate bundle: ~100KB (200→50 certs)
- Dynamic allocation: Only allocates what's needed

Total savings: ~114KB of RAM, which should resolve the allocation failures.

## Rollback if Needed
If HTTPS still fails, further reductions:
1. Try smaller buffers: IN_CONTENT_LEN=2048, OUT_CONTENT_LEN=1024
2. Reduce certificate count further: MAX_CERTS=25
3. Disable certificate verification for testing (not recommended for production)
