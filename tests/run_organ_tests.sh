#!/bin/zsh
# Lector de presets de órgano (.ns4o) contra OpenNord, sobre una carpeta del usuario (presets de fábrica:
# no se guardan en el proyecto).
#   tests/run_organ_tests.sh "<carpeta con .ns4o>"
set -euo pipefail
cd "$(dirname "$0")/.."
DIR="${1:?carpeta con presets .ns4o}"
mkdir -p build/tests build/inspect
tools/inspect/run.sh organgolden "$DIR" > build/inspect/organ-golden.tsv
clang++ -std=c++20 -O2 -Wall -Wextra -o build/tests/organ_preset_test tests/organ_preset_test.cpp Source/core/OrganPreset.cpp
build/tests/organ_preset_test build/inspect/organ-golden.tsv

# Motor de órgano contra OpenNord (no necesita presets).
tools/inspect/run.sh organrender build/inspect/organ >/dev/null
clang++ -std=c++20 -O2 -Wall -Wextra -o build/tests/organ_engine_test tests/organ_engine_test.cpp Source/core/OrganEngine.cpp Source/core/OrganPreset.cpp
build/tests/organ_engine_test
