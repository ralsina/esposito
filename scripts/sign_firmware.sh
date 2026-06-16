#!/usr/bin/env bash
# Sign a firmware binary with the OTA release private key (ECDSA-P256).
#
# Usage:
#   sign_firmware.sh <firmware.bin> <private_key.pem>
#
# Produces <firmware.bin>.sig (DER-encoded ECDSA signature of SHA-256 digest)
# alongside the input.
#
# The matching PUBLIC key is embedded in firmware at main/ota_keys.h; only a
# firmware signed by the matching private key will be accepted by the OTA
# updater. See docs/trust-model.md.
#
# In CI, the private key comes from the OTA_SIGNING_PRIVATE_KEY GitHub Actions
# secret and is written to a temp file before this script runs.

set -euo pipefail

if [ "$#" -ne 2 ]; then
    echo "Usage: $0 <firmware.bin> <private_key.pem>" >&2
    exit 1
fi

firmware="$1"
keyfile="$2"

if [ ! -f "$firmware" ]; then
    echo "Firmware file not found: $firmware" >&2
    exit 1
fi
if [ ! -f "$keyfile" ]; then
    echo "Private key file not found: $keyfile" >&2
    exit 1
fi

out="${firmware}.sig"

# ECDSA-with-SHA-256 over the firmware, DER-encoded (the format mbedtls_pk_verify
# expects via mbedtls_ecdsa_read_signature).
openssl dgst -sha256 -sign "$keyfile" -out "$out" "$firmware"

# Also write a plain-hex sha256 alongside, useful for transparency / debugging.
openssl dgst -sha256 -r "$firmware" | awk '{print $1}' > "${firmware}.sha256"

echo "Signed $firmware"
echo "  signature: $out ($(wc -c < "$out") bytes)"
echo "  sha256:    ${firmware}.sha256"
