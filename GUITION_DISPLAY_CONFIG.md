# JC4827W543 Display Configuration

## Hardware

- **Board**: Guition JC4827W543
- **MCU**: ESP32-S3-WROOM-1-N4R8
- **Display**: 4.3" TFT LCD, 480x272
- **Display Driver IC**: NV3041A
- **Interface**: QSPI (Quad SPI)
- **Flash**: 4MB
- **PSRAM**: 8MB OPI

## Arduino IDE Board Settings

| Setting | Value |
|---|---|
| Board | ESP32S3 Dev Module |
| USB CDC On Boot | Enabled |
| PSRAM | OPI PSRAM |
| Flash Size | 4MB |
| CPU Frequency | 240MHz |

## Required Library

- **GFX Library for Arduino** by Moon On Our Nation, version 1.6.1+
- Install via Arduino Library Manager

## Display Wiring

### QSPI Data Bus

| Signal | GPIO |
|---|---|
| CS | 45 |
| SCK | 47 |
| D0 | 21 |
| D1 | 48 |
| D2 | 40 |
| D3 | 39 |

### Backlight

| Signal | GPIO |
|---|---|
| BL | 1 |

## Code Configuration

```cpp
#include <Arduino_GFX_Library.h>

#define TFT_BL 1

Arduino_DataBus *bus = new Arduino_ESP32QSPI(
    45 /* cs */, 47 /* sck */, 21 /* d0 */, 48 /* d1 */, 40 /* d2 */, 39 /* d3 */);
Arduino_GFX *gfx = new Arduino_NV3041A(bus, GFX_NOT_DEFINED /* RST */, 0 /* rotation */, true /* IPS */);

void setup(void)
{
    gfx->begin();
    gfx->fillScreen(0x0000); // BLACK

    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);
}
```

## Common Pitfalls

- The vendor demo code uses `Arduino_ESP32RGBPanel` / `Arduino_RPi_DPI_RGBPanel` with ILI6485 driver. This is **wrong** for the NV3041A variant. The correct bus is `Arduino_ESP32QSPI` and driver is `Arduino_NV3041A`.
- `DF_GFX_BL` is not defined for this board — use GPIO 1 directly for backlight.
- The old bundled `Arduino_GFX-master` library in the vendor repo does not support QSPI or NV3041A. Use the Library Manager version instead.
- Without **USB CDC On Boot: Enabled**, there is no serial output on ESP32-S3 boards that only have native USB.
- Without correct **Flash Size (4MB)**, the board will not boot (CRC/assert errors).
- RGB565 color constants (RED, GREEN, BLUE, etc.) are not defined in newer library versions — use hex values: `RED=0xF800`, `GREEN=0x07E0`, `BLUE=0x001F`, `BLACK=0x0000`, `WHITE=0xFFFF`.
