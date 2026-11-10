#!/bin/zsh
# Ejecuta un script de inspección (tools/inspect/<nombre>.ts) con el código de OpenNord.
set -euo pipefail
cd "$(dirname "$0")/../.."
export PATH="$PWD/tools/node/bin:$PATH"
NAME="$1"; shift
tools/esbuild/node_modules/.bin/esbuild "tools/inspect/$NAME.ts" --bundle --platform=node --format=esm \
  --tsconfig=vendor/opennord/tsconfig.json --log-level=error --outfile="build/inspect/$NAME.mjs"
node "build/inspect/$NAME.mjs" "$@"
