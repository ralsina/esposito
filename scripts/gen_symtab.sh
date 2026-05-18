#!/bin/bash
# Generate OS symbol table linker script for app ELF builds
# Usage: gen_symtab.sh <firmware_elf> <output_ld>

FIRMWARE_ELF="${1:-build/esposito.elf}"
OUTPUT_LD="${2:-build/os_symbols.ld}"
SYMTAB_C="${3:-main/os_symtab.c}"

if [ ! -f "$FIRMWARE_ELF" ]; then
    echo "Error: firmware ELF not found: $FIRMWARE_ELF"
    echo "Usage: $0 [firmware_elf] [output_ld] [symtab_c]"
    exit 1
fi

TOOLCHAIN_PREFIX="${TOOLCHAIN_PREFIX:-xtensa-esp32-elf}"

# Extract symbols from os_symtab.c automatically
# This parses lines like: {"symbol_name", symbol_function},
SYMBOLS=()

if [ -f "$SYMTAB_C" ]; then
    echo "Extracting symbols from $SYMTAB_C..."
    while IFS= read -r line; do
        # Match lines with pattern: {"name", function},
        if [[ "$line" =~ \{\"([^\"]+)\",\s*([^\}]+)\} ]]; then
            symbol_name="${BASH_REMATCH[1]}"
            symbol_function="${BASH_REMATCH[2]}"
            # Clean up whitespace and trailing commas
            symbol_function=$(echo "$symbol_function" | tr -d '[:space:],')
            SYMBOLS+=("$symbol_name:$symbol_function")
        fi
    done < "$SYMTAB_C"

    if [ ${#SYMBOLS[@]} -eq 0 ]; then
        echo "Warning: No symbols found in $SYMTAB_C"
    fi
else
    echo "Warning: $SYMTAB_C not found, no symbols extracted"
fi

# Additional compiler runtime functions for floating point operations
# These are provided by the toolchain but not declared in headers
COMPILER_RUNTIME_SYMS=(
    __extendsfdf2
    __truncdfsf2
    __fixsfdi
    __fixunssfdi
    __floatdisf
    __floatsidf
    __adddf3
    __subdf3
    __muldf3
    __divdf3
    __addsf3
    __subsf3
    __mulsf3
    __divsf3
    __eqdf2
    __nedf2
    __gedf2
    __gtdf2
    __ledf2
    __ltdf2
)

# Add compiler runtime symbols
for sym in "${COMPILER_RUNTIME_SYMS[@]}"; do
    SYMBOLS+=("$sym")
done

echo "/* Auto-generated OS symbol table for app linking */" > "$OUTPUT_LD"
echo "/* Generated from: $FIRMWARE_ELF */" >> "$OUTPUT_LD"
echo "/* Symbols extracted from: $SYMTAB_C */" >> "$OUTPUT_LD"
echo "" >> "$OUTPUT_LD"

if [ ${#SYMBOLS[@]} -eq 0 ]; then
    echo "Error: No symbols to process. Check $SYMTAB_C"
    exit 1
fi

echo "Processing ${#SYMBOLS[@]} symbols..."

for sym_spec in "${SYMBOLS[@]}"; do
    export_name="$sym_spec"
    provider_name="$sym_spec"
    if [[ "$sym_spec" == *:* ]]; then
        export_name="${sym_spec%%:*}"
        provider_name="${sym_spec##*:}"
    fi

    addr=$("${TOOLCHAIN_PREFIX}-nm" "$FIRMWARE_ELF" 2>/dev/null | grep " [TDWAi] $provider_name$" | head -1 | awk '{print $1}')
    if [ -n "$addr" ]; then
        echo "PROVIDE($export_name = 0x$addr);" >> "$OUTPUT_LD"
    else
        echo "/* WARNING: $export_name ($provider_name) not found in firmware ELF */" >> "$OUTPUT_LD"
        echo "PROVIDE($export_name = 0);" >> "$OUTPUT_LD"
    fi
done

echo "" >> "$OUTPUT_LD"
echo "/* End of OS symbol table */" >> "$OUTPUT_LD"
echo "Generated $OUTPUT_LD with $(grep -c '^PROVIDE' "$OUTPUT_LD") symbols"
