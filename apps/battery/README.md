# Battery

Monitor battery voltage via the Guition board's voltage divider on GPIO5.

## Notes

Reads GPIO5 (ADC1_CH4) with a 33K/100K voltage divider (ratio 1.33). Uses the `adc_oneshot` API with curve-fitting calibration and 32-sample averaging.

**Status:** Not working properly IRL as far as we know. The ADC consistently reads ~3.19–3.27 V even with the battery disconnected and the 100K pulldown confirmed in place (86.5 KΩ to GND).

The likely cause is that **GPIO5 is already used as G0 (Green data line 0) for the RGB display** on this board — see the [JC4827W543-ESP32S3-Guide](https://github.com/OldNKrusty/JC4827W543-ESP32S3-Guide) which documents the pin mapping. The RGB LCD controller drives the pin, so it cannot float for ADC input. A different ADC-capable GPIO not claimed by the display would be needed.

Despite the hardware issue, the code serves as a useful reference for working with GPIO, ADC, and the `adc_oneshot` API on ESP32-S3.

## Reference

- [JC4827W543-ESP32S3-Guide](https://github.com/OldNKrusty/JC4827W543-ESP32S3-Guide) — vendor pin mappings, RGB timing, and GT911 touch config for this board.

## Build

```sh
bash scripts/build_app.sh apps/battery/app.c
```

Then copy to SD card:

```text
/sdcard/apps/battery/program.elf
```
