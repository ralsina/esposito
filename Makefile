# Esposito OS - Makefile
# Convenience targets for building, flashing, and monitoring

.PHONY: all build stub flash flash-stub monitor clean help size menuconfig deploy-all check emulate

# Default target
all: build

# Build the project
build:
	@echo "Building Esposito OS..."
	. /opt/esp-idf/export.sh && idf.py build

# Build the update stub
stub:
	@echo "Building update stub..."
	. /opt/esp-idf/export.sh && $(MAKE) -C stub build

# Flash main firmware + stub to device (assumes /dev/ttyUSB0)
flash: build stub
	@echo "Flashing Esposito OS + update stub to /dev/ttyUSB0..."
	. /opt/esp-idf/export.sh && python -m esptool --chip esp32 -b 460800 \
		--before default_reset --after hard_reset \
		write_flash --flash-mode dio --flash-size 4MB --flash-freq 40m \
		0x1000 build/bootloader/bootloader.bin \
		0x8000 build/partition_table/partition-table.bin \
		0x10000 build/esposito.bin \
		0x210000 build/ota_data_initial.bin \
		0x3a0000 stub/build/esposito_stub.bin

# Flash only the stub binary (for quick stub updates)
flash-stub: stub
	@echo "Flashing update stub only to /dev/ttyUSB0..."
	. /opt/esp-idf/export.sh && python -m esptool --chip esp32 -b 460800 \
		--before default_reset --after hard_reset \
		write_flash 0x3a0000 stub/build/esposito_stub.bin

# Monitor serial output
monitor:
	@echo "Starting serial monitor on /dev/ttyUSB0..."
	@echo "Press Ctrl+C to exit"
	@echo "Press RESET button to see boot sequence"
	@echo ""
	stty -F /dev/ttyUSB0 115200 raw -echo -echoe -echok
	cat /dev/ttyUSB0

# Flash and monitor (run in separate terminals)
flash-monitor: flash
	@echo "Flashing complete!"
	@echo "Run 'make monitor' in a separate terminal to see output"

# Clean build files
clean:
	@echo "Cleaning build files..."
	. /opt/esp-idf/export.sh && idf.py fullclean
	$(MAKE) -C stub clean

# Show binary size information
size: build
	@echo "Binary size information:"
	. /opt/esp-idf/export.sh && idf.py size

# Configuration menu
menuconfig:
	@echo "Opening configuration menu..."
	. /opt/esp-idf/export.sh && idf.py menuconfig

# Build firmware + stub + all apps, copy to SD card, flash.
# (Despite the historical name, this is a deploy target, not a test.)
deploy-all: build stub
	@echo "Building firmware + stub + apps and flashing..."
	. /opt/esp-idf/export.sh && scripts/build_test.sh

# Run host-side tests (currently just the reader app's tokenizer suite).
check:
	@echo "Running host-side tests..."
	bash apps/reader/tests/run_tests.sh

# Build the Linux/SDL2 desktop emulator (see linux/README for usage).
emulate:
	@echo "Building Linux emulator..."
	$(MAKE) -C linux

# Help target
help:
	@echo "Esposito OS - Available Targets:"
	@echo "================================="
	@echo "make build         - Build firmware"
	@echo "make stub          - Build update stub"
	@echo "make flash         - Build + flash firmware, bootloader, partition table, otadata, and stub"
	@echo "make flash-stub    - Flash only the update stub (quick update)"
	@echo "make monitor       - Monitor serial output"
	@echo "make flash-monitor - Flash and then monitor (separate terminals)"
	@echo "make clean         - Clean build files"
	@echo "make size          - Show binary size"
	@echo "make menuconfig    - Open ESP-IDF configuration menu"
	@echo "make deploy-all    - Build firmware + stub + all apps, copy to SD card, flash"
	@echo "make check         - Run host-side tests (reader tokenizer suite)"
	@echo "make emulate       - Build the Linux/SDL2 desktop emulator"
	@echo "make help          - Show this help message"
	@echo ""
	@echo "Releases are produced by the .github/workflows/release.yml workflow"
	@echo "on tag push (vX.Y.Z); see docs/trust-model.md for the signing details."
	@echo ""
	@echo "Flash layout:"
	@echo "  0x0000  Bootloader"
	@echo "  0x0800  Partition table"
	@echo "  0x1000  Bootloader (from idf.py)"
	@echo "  0x8000  Partition table"
	@echo "  0x10000 Factory (main firmware)"
	@echo "  0x210000 OTA data"
	@echo "  0x212000 Storage (SPIFFS)"
	@echo "  0x21A000 Font cache"
	@echo "  0x24A000 App code (dynamic apps)"
	@echo "  0x3A0000 Update stub"
