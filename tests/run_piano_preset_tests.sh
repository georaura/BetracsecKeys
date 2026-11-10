#!/bin/zsh
# Presets de piano (.ns4n) contra OpenNord: lectura de cada capa y el .npno que se elige por nombre.
#   tests/run_piano_preset_tests.sh "<carpeta con .ns4n>" "<carpeta de pianos .npno>"
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/tests build/inspect
tools/inspect/run.sh pianogolden "$1" "$2" > build/inspect/piano-golden.tsv
clang++ -std=c++20 -O2 -Wall -Wextra -o build/tests/piano_preset_test tests/piano_preset_test.cpp Source/core/PianoPreset.cpp Source/core/Npno.cpp
build/tests/piano_preset_test build/inspect/piano-golden.tsv "$2"
