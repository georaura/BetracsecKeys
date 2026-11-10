#!/bin/zsh
# Exportación a BeTracks Pro (.sfgr): especificación, remuestreo, capas y recorte.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/tests
clang++ -std=c++20 -O2 -Wall -Wextra -o build/tests/sfgr_export_test tests/sfgr_export_test.cpp Source/core/SfgrWriter.cpp Source/core/SampleEngine.cpp Source/core/NordSample.cpp
build/tests/sfgr_export_test
clang++ -std=c++20 -O2 -Wall -Wextra -o build/tests/piano_export_test tests/piano_export_test.cpp Source/core/PianoExport.cpp Source/core/PianoSound.cpp Source/core/PianoEngine.cpp Source/core/Npno.cpp Source/core/SfgrWriter.cpp Source/core/SampleEngine.cpp Source/core/NordSample.cpp
build/tests/piano_export_test
