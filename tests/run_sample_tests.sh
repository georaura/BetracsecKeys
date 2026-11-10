#!/bin/zsh
# Nord Samples contra OpenNord (uno de cada 8 archivos) y lectura de todos los de la carpeta.
#   tests/run_sample_tests.sh "<carpeta con .nsmp3/.nsmp4>"
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/tests build/inspect
tools/inspect/run.sh samplegolden "$1" 8 > build/inspect/sample-golden.jsonl 2>/dev/null
clang++ -std=c++20 -O2 -Wall -Wextra -o build/tests/sample_test tests/sample_test.cpp Source/core/NordSample.cpp
build/tests/sample_test build/inspect/sample-golden.jsonl "$1"
