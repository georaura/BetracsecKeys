#!/bin/zsh
# Compila BeTracks Keys para desarrollo (solo esta arquitectura) e instala el AU y el VST3 en
# ~/Library/Audio/Plug-Ins para probarlos en tu DAW.
#
#   ./build_app.sh            → compila e instala
#   ./build_app.sh --open     → además abre la app Standalone
set -euo pipefail
cd "$(dirname "$0")"
CMAKE="$PWD/tools/cmake/CMake.app/Contents/bin/cmake"
BUILD=build/dev
ART="$BUILD/BetracsecKeys_artefacts/Release"

"$CMAKE" -S . -B "$BUILD" -G Xcode -DCMAKE_OSX_ARCHITECTURES="$(uname -m)" >/dev/null
"$CMAKE" --build "$BUILD" --config Release --target BetracsecKeys_AU BetracsecKeys_VST3 BetracsecKeys_Standalone -- -quiet

IDENTITY="$(security find-identity -v -p codesigning 2>/dev/null | awk -F'"' '/Apple Development/ {print $2; exit}')"
SIGN="${IDENTITY:--}"
for b in "$ART/AU/BeTracks Keys.component" "$ART/VST3/BeTracks Keys.vst3" "$ART/Standalone/BeTracks Keys.app"; do
  codesign --force --options runtime --sign "$SIGN" "$b" 2>/dev/null || codesign --force --sign - "$b"
done

mkdir -p ~/Library/Audio/Plug-Ins/Components ~/Library/Audio/Plug-Ins/VST3
# Las copias con el nombre de antes (Betracsec Keys) tienen los mismos códigos: dos a la vez confunden al DAW.
rm -rf ~/Library/Audio/Plug-Ins/Components/"Betracsec Keys.component" ~/Library/Audio/Plug-Ins/VST3/"Betracsec Keys.vst3"
rm -rf ~/Library/Audio/Plug-Ins/Components/"BeTracks Keys.component" ~/Library/Audio/Plug-Ins/VST3/"BeTracks Keys.vst3"
ditto "$ART/AU/BeTracks Keys.component" ~/Library/Audio/Plug-Ins/Components/"BeTracks Keys.component"
ditto "$ART/VST3/BeTracks Keys.vst3" ~/Library/Audio/Plug-Ins/VST3/"BeTracks Keys.vst3"
# Que Logic/GarageBand vuelvan a leer los AU.
killall -9 AudioComponentRegistrar 2>/dev/null || true

echo "App:  $PWD/$ART/Standalone/BeTracks Keys.app"
echo "AU y VST3 instalados en ~/Library/Audio/Plug-Ins (versión $(cat VERSION))"
[ "${1:-}" = "--open" ] && open "$ART/Standalone/BeTracks Keys.app"
exit 0
