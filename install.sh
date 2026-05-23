#!/bin/bash
set -e

echo "Building Keylo..."
cmake --build build --target Keylo_VST3 --target Keylo_AU

echo "Installing VST3..."
cp -r build/Keylo_artefacts/Debug/VST3/Keylo.vst3 ~/Library/Audio/Plug-Ins/VST3/

echo "Installing AU..."
cp -r build/Keylo_artefacts/Debug/AU/Keylo.component ~/Library/Audio/Plug-Ins/Components/

echo "Done. Restart your DAW and rescan plugins."
