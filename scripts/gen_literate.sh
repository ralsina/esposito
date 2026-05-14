#!/bin/bash
# Generate literate documentation from app source files using crycco

set -e

if [ $# -eq 0 ]; then
    echo "Usage: $0 <app_name> [app_name2 ...]"
    echo ""
    echo "Examples:"
    echo "  $0 snake"
    echo "  $0 snake reader paint"
    exit 1
fi

repo_root="$(cd "$(dirname "$0")/.." && pwd)"

for app_name in "$@"; do
    app_file="$repo_root/apps/$app_name/app.c"
    
    if [ ! -f "$app_file" ]; then
        echo "Error: app file not found: $app_file"
        exit 1
    fi
    
    echo "Generating literate docs for: $app_name"
    
    # Generate markdown in docs/ folder
    crycco --mode markdown "$app_file"
    
    # Move from docs/app.md to apps/{app_name}/app.md
    if [ -f "$repo_root/docs/app.md" ]; then
        mv "$repo_root/docs/app.md" "$repo_root/apps/$app_name/app.md"
        echo "  -> apps/$app_name/app.md"
    else
        echo "Warning: crycco output not found at docs/app.md"
    fi
done

echo "Done!"
