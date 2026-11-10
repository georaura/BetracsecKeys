#!/bin/zsh
# Versión pública de BeTracks Keys para betracksec.com/keys.
#
#   ./build_public.sh
#
# Genera (en build-public/ y copia en website/keys/downloads/):
#   BeTracksKeys-<versión>.pkg          instalador: app + AU + VST3 (lo usa también Sparkle)
#   BeTracksKeys-<versión>.dmg          el .pkg + Léeme + licencia (el botón de la web)
#   BeTracksKeys-<versión>-source.zip   el código fuente (obligatorio por la AGPL)
#   website/keys/appcast.xml             feed de Sparkle, actualizado
#
# Para que otros Macs lo instalen sin avisos hacen falta (ver PUBLICAR.md):
#   - "Developer ID Application" (firma app y plugins) y "Developer ID Installer" (firma el .pkg)
#   - un perfil de notarización:  NOTARY_PROFILE=betracks ./build_public.sh
#   - la clave EdDSA de Sparkle en el llavero (cuenta "betracks-keys")
set -euo pipefail
cd "$(dirname "$0")"

NAME="BeTracks Keys"
EXE="BeTracksKeys"   # nombre de los archivos de descarga (.pkg, .dmg), igual que en Windows
SITE="https://betracksec.com/keys"
KEY_ACCOUNT="betracks-keys"
MIN_OS="12.0"
VERSION="$(cat VERSION)"
OUT="build-public"
CMAKE="$PWD/tools/cmake/CMake.app/Contents/bin/cmake"
SPARKLE_BIN="vendor/Sparkle/bin"

DEVID="$(security find-identity -v -p codesigning 2>/dev/null | awk -F'"' '/Developer ID Application/ {print $2; exit}')"
INSTALLER_ID="$(security find-identity -v -p basic 2>/dev/null | awk -F'"' '/Developer ID Installer/ {print $2; exit}')"
DEV="$(security find-identity -v -p codesigning 2>/dev/null | awk -F'"' '/Apple Development/ {print $2; exit}')"
if [ -n "$DEVID" ]; then SIGN="$DEVID"; TS=(--timestamp); else SIGN="${DEV:--}"; TS=(); fi
echo "Firma app/plugins: $SIGN"
echo "Firma instalador:  ${INSTALLER_ID:-(ninguna)}"

# Clave pública de Sparkle (la privada nunca sale del llavero).
ED_KEY="$("$SPARKLE_BIN/generate_keys" --account "$KEY_ACCOUNT" -p 2>/dev/null | grep -E '^[A-Za-z0-9+/]{43}=$' || true)"
if [ -z "$ED_KEY" ]; then
  echo "AVISO: no hay clave de Sparkle en el llavero (cuenta $KEY_ACCOUNT). Créala una vez con:"
  echo "       $SPARKLE_BIN/generate_keys --account $KEY_ACCOUNT"
  echo "       Sin ella el build sigue, pero la app no podrá comprobar actualizaciones."
fi

# ---------------------------------------------------------------- compilar (universal, con Sparkle)
"$CMAKE" -S . -B "$OUT/cmake" -G Xcode -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DBK_SPARKLE=ON >/dev/null
"$CMAKE" --build "$OUT/cmake" --config Release --target BetracsecKeys_AU BetracsecKeys_VST3 BetracsecKeys_Standalone -- -quiet
ART="$OUT/cmake/BetracsecKeys_artefacts/Release"

STAGE="$OUT/stage"
rm -rf "$STAGE"; mkdir -p "$STAGE/app" "$STAGE/au" "$STAGE/vst3"
APP="$STAGE/app/$NAME.app"
ditto "$ART/Standalone/$NAME.app" "$APP"
ditto "$ART/AU/$NAME.component" "$STAGE/au/$NAME.component"
ditto "$ART/VST3/$NAME.vst3" "$STAGE/vst3/$NAME.vst3"

PLIST="$APP/Contents/Info.plist"
plutil -replace LSApplicationCategoryType -string "public.app-category.music" "$PLIST"
plutil -replace LSMinimumSystemVersion -string "$MIN_OS" "$PLIST"
plutil -replace SUFeedURL -string "$SITE/appcast.xml" "$PLIST"
plutil -replace SUEnableAutomaticChecks -bool YES "$PLIST"
[ -n "$ED_KEY" ] && plutil -replace SUPublicEDKey -string "$ED_KEY" "$PLIST"

# ---------------------------------------------------------------- firma (de dentro hacia fuera)
source scripts/sparkle.sh
embed_sparkle "$APP" "$SIGN" "${TS[@]}"
codesign --force --options runtime "${TS[@]}" --sign "$SIGN" "$APP"
codesign --force --options runtime "${TS[@]}" --sign "$SIGN" "$STAGE/au/$NAME.component"
codesign --force --options runtime "${TS[@]}" --sign "$SIGN" "$STAGE/vst3/$NAME.vst3"
for b in "$APP" "$STAGE/au/$NAME.component" "$STAGE/vst3/$NAME.vst3"; do codesign --verify --deep --strict "$b"; done
lipo -archs "$APP/Contents/MacOS/$NAME"

# ---------------------------------------------------------------- instalador .pkg
PKGS="$OUT/pkgs"; rm -rf "$PKGS"; mkdir -p "$PKGS" "$OUT/scripts-au"
cat > "$OUT/scripts-au/postinstall" <<'SH'
#!/bin/sh
# Que Logic, GarageBand y MainStage vuelvan a leer la lista de Audio Units.
killall -9 AudioComponentRegistrar 2>/dev/null || true
exit 0
SH
chmod +x "$OUT/scripts-au/postinstall"

component_pkg() { # <carpeta raíz> <destino> <identificador> <salida> [scripts]
  local root="$1" dest="$2" id="$3" out="$4"
  local -a extra=()
  [ -n "${5:-}" ] && extra=(--scripts "$5")
  local plist="$PKGS/$id.plist"
  pkgbuild --analyze --root "$root" "$plist" >/dev/null
  # Sin "relocatable": si no, el instalador puede sobrescribir una copia que esté en otra carpeta.
  python3 -I -c "import plistlib,sys; p=sys.argv[1]; d=plistlib.load(open(p,'rb')); [c.__setitem__('BundleIsRelocatable', False) for c in d]; plistlib.dump(d, open(p,'wb'))" "$plist"
  pkgbuild --root "$root" --component-plist "$plist" --install-location "$dest" --identifier "$id" \
           --version "$VERSION" "${extra[@]}" "$out" >/dev/null
}
component_pkg "$STAGE/app"  /Applications                       com.betracsec.keys.app  "$PKGS/app.pkg"
component_pkg "$STAGE/au"   /Library/Audio/Plug-Ins/Components  com.betracsec.keys.au   "$PKGS/au.pkg" "$OUT/scripts-au"
component_pkg "$STAGE/vst3" /Library/Audio/Plug-Ins/VST3        com.betracsec.keys.vst3 "$PKGS/vst3.pkg"

RES="$OUT/pkg-resources"; rm -rf "$RES"; mkdir -p "$RES"
cp installer/welcome.html installer/conclusion.html "$RES/"
cp LICENSE "$RES/LICENSE.txt"
sed "s/@VERSION@/$VERSION/g" installer/distribution.xml > "$OUT/distribution.xml"
PKG_NAME="$EXE-$VERSION.pkg"
PKG="$OUT/$PKG_NAME"
rm -f "$PKG"
productbuild --distribution "$OUT/distribution.xml" --package-path "$PKGS" --resources "$RES" \
  ${INSTALLER_ID:+--sign "$INSTALLER_ID" --timestamp} "$PKG" >/dev/null

NOTARIZED=0
notarize() { xcrun notarytool submit "$1" --keychain-profile "$NOTARY_PROFILE" --wait && xcrun stapler staple "$1"; }
if [ -n "$DEVID" ] && [ -n "$INSTALLER_ID" ] && [ -n "${NOTARY_PROFILE:-}" ]; then
  echo "Notarizando el instalador (puede tardar unos minutos)…"
  notarize "$PKG"
  NOTARIZED=1
fi

# ---------------------------------------------------------------- DMG
DMG_NAME="$EXE-$VERSION.dmg"
DMG="$OUT/$DMG_NAME"
D="$OUT/dmg"; rm -rf "$D" "$DMG"; mkdir -p "$D"
cp "$PKG" "$D/Instalar $NAME.pkg"
cp LICENSE "$D/Licencia (AGPL-3.0).txt"
cat > "$D/Léeme - Read me.txt" <<TXT
$NAME $VERSION — $SITE

ES
1. Abre "Instalar $NAME.pkg". Instala la app en Aplicaciones y los plugins AU y VST3
   (puedes elegir cuáles en "Personalizar").
2. Abre BeTracks Keys (o búscalo en tu DAW entre los instrumentos de BeTracks).
3. Biblioteca → Añadir carpeta… y elige la carpeta con tus pianos .npno.
   No incluye sonidos: la Nord Piano Library se descarga gratis en nordkeyboards.com.
Requisitos: macOS $MIN_OS o posterior, Apple silicon o Intel.

EN
1. Open "Instalar $NAME.pkg". It installs the app and the AU and VST3 plug-ins.
2. Open BeTracks Keys (or find it under BeTracks in your DAW).
3. Library (Biblioteca) → Add folder… and pick the folder with your .npno pianos.
   No sounds are included: the Nord Piano Library is a free download at nordkeyboards.com.
Requirements: macOS $MIN_OS or later, Apple silicon or Intel.

Nord y Nord Piano Library son marcas de Clavia DMI AB. BeTracks Keys no está afiliada a Clavia.
Código fuente (AGPL-3.0): https://github.com/georaura/BetracsecKeys
TXT
hdiutil create -volname "$NAME $VERSION" -srcfolder "$D" -ov -format UDZO "$DMG" >/dev/null
rm -rf "$D"
codesign --force "${TS[@]}" --sign "$SIGN" "$DMG"
[ "$NOTARIZED" = 1 ] && { echo "Notarizando el DMG…"; notarize "$DMG"; }

# ---------------------------------------------------------------- código fuente (AGPL)
SRC_NAME="$EXE-$VERSION-source.zip"
SRC="$OUT/$SRC_NAME"
rm -f "$SRC"
zip -qr "$SRC" CMakeLists.txt VERSION LICENSE README.md PUBLICAR.md compile_flags.txt \
  build_app.sh build_public.sh Source tests/*.cpp tests/run_tests.sh tools/golden installer scripts assets \
  vendor/opennord vendor/opennord.COMMIT vendor/JUCE.VERSION \
  -x '*.DS_Store' 'vendor/opennord/node_modules/*'

# ---------------------------------------------------------------- web: descargas + appcast
WEB="website/keys"
mkdir -p "$WEB/downloads"
cp "$PKG" "$DMG" "$SRC" "$WEB/downloads/"
if [ -n "$ED_KEY" ]; then
  SIG_ATTRS="$("$SPARKLE_BIN/sign_update" --account "$KEY_ACCOUNT" "$PKG")"
  NOTES="release-notes/$VERSION.html"
  python3 scripts/appcast.py "$WEB/appcast.xml" "$NAME" "$VERSION" "$SITE/downloads/$PKG_NAME" "$SIG_ATTRS" \
    "$( [ -f "$NOTES" ] && cat "$NOTES" || echo "<p>$NAME $VERSION</p>")" "$SITE/" "$MIN_OS"
fi
sed -i '' -E "s#downloads/$EXE-[0-9.]+\.dmg#downloads/$DMG_NAME#g; s#downloads/$EXE-[0-9.]+-source\.zip#downloads/$SRC_NAME#g; s#<small>v[0-9.]+#<small>v$VERSION#g" "$WEB/index.html"

echo
echo "Instalador: $(pwd)/$PKG"
echo "DMG:        $(pwd)/$DMG"
echo "Código:     $(pwd)/$SRC"
echo "Web:        $(pwd)/$WEB/  (sube su contenido a public_html/keys/)"
if [ "$NOTARIZED" = 1 ]; then
  echo "Estado:     firmado con Developer ID y notarizado. Listo para publicar."
else
  echo "AVISO:      NO está listo para publicar:"
  [ -z "$DEVID" ] && echo "            - falta el certificado Developer ID Application"
  [ -z "$INSTALLER_ID" ] && echo "            - falta el certificado Developer ID Installer"
  [ -z "${NOTARY_PROFILE:-}" ] && echo "            - falta NOTARY_PROFILE (perfil de notarización)"
  [ -z "$ED_KEY" ] && echo "            - falta la clave de Sparkle (cuenta $KEY_ACCOUNT)"
  echo "            Sirve para probar en tus Macs; en otros, Gatekeeper lo bloqueará."
fi
