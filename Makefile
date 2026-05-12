# Esposito OS - Makefile
# Convenience targets for building, flashing, and monitoring

.PHONY: all build flash monitor clean help size test

# Default target
all: build

# Build the project
build:
	@echo "Building Esposito OS..."
	. /opt/esp-idf/export.sh && idf.py build

# Flash to device (assumes /dev/ttyUSB0)
flash: build
	@echo "Flashing Esposito OS to /dev/ttyUSB0..."
	. /opt/esp-idf/export.sh && idf.py -p /dev/ttyUSB0 flash

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

# Show binary size information
size: build
	@echo "Binary size information:"
	. /opt/esp-idf/export.sh && idf.py size

# Configuration menu
menuconfig:
	@echo "Opening configuration menu..."
	. /opt/esp-idf/export.sh && idf.py menuconfig

# Target-specific flash (alternative ports)
flash-ttyacm0: build
	. /opt/esp-idf/export.sh && idf.py -p /dev/ttyACM0 flash

# Build, copy to SD card, and flash a test app
# Usage: make test             (builds and flashes example_app)
#        make test APP=sd_test (builds and flashes a different app)
test:
	@echo "Building test app and flashing..."
	. /opt/esp-idf/export.sh && scripts/build_test.sh $(APP)

# Help target
help:
	@echo "Esposito OS - Available Targets:"
	@echo "================================="
	@echo "make build        - Build the project"
	@echo "make flash        - Flash to /dev/ttyUSB0"
	@echo "make monitor      - Monitor serial output"
	@echo "make flash-monitor- Flash and then monitor (separate terminals)"
	@echo "make clean        - Clean build files"
	@echo "make size         - Show binary size"
	@echo "make menuconfig   - Open ESP-IDF configuration menu"
	@echo "make test         - Build test app, copy to SD card, flash"
	@echo "make help         - Show this help message"
	@echo ""
	@echo "Quick Start:"
	@echo "1. Connect ESP32 CYD via USB"
	@echo "2. Run: make flash"
	@echo "3. In another terminal: make monitor"
	@echo "4. Press RESET button to see boot sequence"
