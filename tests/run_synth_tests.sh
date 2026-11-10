#!/bin/zsh
# Presets de synth (.ns4y) contra OpenNord.
#   tests/run_synth_tests.sh "<carpeta con .ns4y>" ["<carpeta de samples>"]
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/tests build/inspect
tools/inspect/run.sh synthgolden "$1" > build/inspect/synth-golden.tsv
clang++ -std=c++20 -O2 -Wall -Wextra -o build/tests/synth_preset_test tests/synth_preset_test.cpp Source/core/SynthPreset.cpp
build/tests/synth_preset_test build/inspect/synth-golden.tsv ${2:+"$2"}
clang++ -std=c++20 -O2 -Wall -Wextra -o build/tests/synth_engine_test tests/synth_engine_test.cpp Source/core/SynthEngine.cpp Source/core/SynthPreset.cpp Source/core/SampleEngine.cpp Source/core/NordSample.cpp
build/tests/synth_engine_test "$1" ${2:+"$2"}
