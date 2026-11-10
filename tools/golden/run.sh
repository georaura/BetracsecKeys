#!/bin/zsh
# Genera los pianos sintéticos de referencia con el código de OpenNord (vendor/opennord).
set -euo pipefail
cd "$(dirname "$0")/../.."
export PATH="$PWD/tools/node/bin:$PATH"
OUT="${1:-tests/golden}"
tools/esbuild/node_modules/.bin/esbuild tools/golden/make-golden.ts --bundle --platform=node --format=esm \
  --tsconfig=vendor/opennord/tsconfig.json --log-level=warning --outfile=build/golden/make-golden.mjs
node build/golden/make-golden.mjs "$OUT"
