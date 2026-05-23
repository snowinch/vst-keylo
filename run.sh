#!/bin/bash
set -e

STANDALONE="build/Keylo_artefacts/Debug/Standalone/Keylo.app"

if [ ! -d "$STANDALONE" ]; then
  echo "Building Keylo Standalone..."
  cmake --build build --target Keylo_Standalone
fi

echo "Launching Keylo..."
open "$STANDALONE"
