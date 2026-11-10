#!/bin/zsh
# Motor FM-X contra una librería de MONTAGE (.X7L / .X7U).
#   tests/run_fmx_tests.sh "<librería>"
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/tests
clang++ -std=c++20 -O2 -Wall -Wextra -o build/tests/fmx_test tests/fmx_test.cpp Source/core/FmxEngine.cpp Source/core/Ysfc.cpp
build/tests/fmx_test "$1"
clang++ -std=c++20 -O2 -Wall -Wextra -o build/tests/awm_test tests/awm_test.cpp Source/core/AwmEngine.cpp Source/core/FmxEngine.cpp Source/core/Ysfc.cpp
build/tests/awm_test "$1"
