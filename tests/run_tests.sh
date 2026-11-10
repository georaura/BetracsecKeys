#!/bin/zsh
# Compila y ejecuta las pruebas del núcleo (sin JUCE).
#   tests/run_tests.sh                    → decodificador contra OpenNord (tests/golden) + motor
#   tests/run_tests.sh <piano.npno>…      → lee y decodifica pianos reales
set -euo pipefail
cd "$(dirname "$0")/.."
[ -f tests/golden/M4.txt ] || tools/golden/run.sh >/dev/null
mkdir -p build/tests
CORE=(Source/core/Npno.cpp Source/core/PianoSound.cpp Source/core/PianoEngine.cpp)
clang++ -std=c++20 -O2 -Wall -Wextra -o build/tests/decoder_test tests/decoder_test.cpp "${CORE[@]}"
build/tests/decoder_test "$@"
if [ $# -eq 0 ]; then
  clang++ -std=c++20 -O2 -Wall -Wextra -o build/tests/engine_test tests/engine_test.cpp "${CORE[@]}"
  build/tests/engine_test
fi
