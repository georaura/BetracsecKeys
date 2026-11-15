// BeTracks Keys — carpeta visible de la app en Música (Muestras, exportaciones a BeTracks Pro).
//
// La app se llamó «Betracsec Keys» y su carpeta en Música tenía ese nombre. Las instalaciones nuevas usan
// «BeTracks Keys». Si ya existe la carpeta antigua (y no la nueva) se sigue usando: los ajustes del
// muestreador y las librerías guardan rutas completas, y renombrarla las rompería.
#pragma once

#include <juce_core/juce_core.h>

namespace bk
{
inline juce::File keysMusicFolder()
{
    const auto music = juce::File::getSpecialLocation (juce::File::userMusicDirectory);
    const auto now = music.getChildFile ("BeTracks Keys");
    const auto before = music.getChildFile ("Betracsec Keys");
    return (! now.exists() && before.isDirectory()) ? before : now;
}
} // namespace bk
