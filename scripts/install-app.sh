#!/bin/bash
# Build and install PeerDen to /Applications
# Usage: ./scripts/install-app.sh  (run from project root)

set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

echo "Building..."
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

echo "Creating app bundle..."
APP_NAME="PeerDen.app"
APP_DIR="$APP_NAME/Contents"
MACOS_DIR="$APP_DIR/MacOS"

rm -rf "$APP_NAME"
mkdir -p "$MACOS_DIR"

cp build/peerdden "$MACOS_DIR/"
cp "$SCRIPT_DIR/Info.plist" "$APP_DIR/"

echo "Installing to /Applications..."
cp -R "$APP_NAME" /Applications/

echo "Done. Launching..."
open "/Applications/$APP_NAME"
