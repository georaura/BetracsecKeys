# Funciones compartidas por build_app.sh y build_public.sh (se carga con `source`).

SPARKLE_FW="vendor/Sparkle/Sparkle.framework"

# embed_sparkle <App.app> <identidad> [opciones extra de codesign…]
# Copia Sparkle.framework en Contents/Frameworks y firma sus piezas de dentro hacia fuera
# (orden que exige la notarización).
embed_sparkle() {
  local app="$1" id="$2"; shift 2
  local fw="$app/Contents/Frameworks/Sparkle.framework"
  mkdir -p "$app/Contents/Frameworks"
  ditto "$SPARKLE_FW" "$fw"
  local v="$fw/Versions/B"
  codesign --force --options runtime "$@" --sign "$id" "$v/XPCServices/Installer.xpc"
  codesign --force --options runtime "$@" --preserve-metadata=entitlements --sign "$id" "$v/XPCServices/Downloader.xpc"
  codesign --force --options runtime "$@" --sign "$id" "$v/Autoupdate"
  codesign --force --options runtime "$@" --sign "$id" "$v/Updater.app"
  codesign --force --options runtime "$@" --sign "$id" "$fw"
}
