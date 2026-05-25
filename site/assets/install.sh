#!/bin/bash

# Nicolino Installation Script
# Downloads the latest release binary and installs it to ~/.local/bin

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Determine system architecture
ARCH=$(uname -m)
case "$ARCH" in
    x86_64)
        ARCH="amd64"
        ;;
    aarch64|arm64)
        ARCH="arm64"
        ;;
    *)
        echo -e "${RED}Error: Unsupported architecture: $ARCH${NC}"
        echo "Supported architectures: x86_64, aarch64/arm64"
        exit 1
        ;;
esac

# Determine OS
OS=$(uname -s)
case "$OS" in
    Linux)
        OS="linux"
        ;;
    Darwin)
        OS="darwin"
        ;;
    *)
        echo -e "${RED}Error: Unsupported OS: $OS${NC}"
        echo "Supported operating systems: Linux, macOS"
        exit 1
        ;;
esac

# Get latest release URL
echo "Fetching latest release information..."
LATEST_URL=$(curl -s "https://api.github.com/repos/ralsina/nicolino/releases/latest" | \
    grep "browser_download_url.*${OS}-${ARCH}" | \
    cut -d '"' -f 4 | \
    head -n 1)

if [ -z "$LATEST_URL" ]; then
    echo -e "${RED}Error: Could not find a release for $OS-$ARCH${NC}"
    exit 1
fi

# Extract version from URL
VERSION=$(echo "$LATEST_URL" | grep -oP 'nicolino-\K[0-9.]+' || echo "latest")
echo -e "${GREEN}Found version: $VERSION${NC}"

# Create temp directory
TEMP_DIR=$(mktemp -d)
trap 'rm -rf "$TEMP_DIR"' EXIT

# Download the binary
echo "Downloading from: $LATEST_URL"
curl -L -o "$TEMP_DIR/nicolino" "$LATEST_URL"

# Make it executable
chmod +x "$TEMP_DIR/nicolino"

# Create ~/.local/bin if it doesn't exist
INSTALL_DIR="$HOME/.local/bin"
mkdir -p "$INSTALL_DIR"

# Install the binary
echo "Installing to $INSTALL_DIR"
cp "$TEMP_DIR/nicolino" "$INSTALL_DIR/nicolino"

# Verify installation
if command -v nicolino &> /dev/null; then
    echo -e "${GREEN}✓ Successfully installed nicolino!${NC}"
    "$INSTALL_DIR/nicolino" --version
    echo ""
    echo -e "${YELLOW}⚠️  Performance Warning:${NC}"
    echo "This binary is built without optimizations for faster compilation."
    echo "Some tasks like image processing may be significantly slower."
    echo ""
    echo "For optimal performance, build from source:"
    echo "  git clone https://github.com/ralsina/nicolino"
    echo "  cd nicolino"
    echo "  shards build"
    echo "  cp ./bin/nicolino ~/.local/bin/"
else
    echo -e "${YELLOW}Warning: $INSTALL_DIR may not be in your PATH${NC}"
    echo "Add the following to your ~/.bashrc or ~/.zshrc:"
    echo "  export PATH=\"\$HOME/.local/bin:\$PATH\""
    echo "Then run: source ~/.bashrc (or source ~/.zshrc)"
fi

echo -e "${GREEN}Installation complete!${NC}"
