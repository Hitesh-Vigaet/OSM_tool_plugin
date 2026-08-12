#!/usr/bin/env bash
set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd "$SCRIPT_DIR"

echo "Downloading libosmium (header-only)..."
if [ ! -d "libosmium" ]; then
    git clone --depth 1 https://github.com/osmcode/libosmium.git libosmium
else
    echo "libosmium directory already exists."
fi

echo "Downloading protozero (header-only)..."
if [ ! -d "protozero" ]; then
    git clone --depth 1 https://github.com/mapbox/protozero.git protozero
else
    echo "protozero directory already exists."
fi

echo "ThirdParty dependencies successfully fetched!"
