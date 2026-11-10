// BeTracks Keys — «Exportar a BeTracks Pro (.sfgr)…»: el sample de la sección SAMPLES (un Nord Sample o una
// librería muestreada con Keys) pasa al formato de BeTracks Pro. Calcula la RAM con cada recorte, pregunta dónde
// guardar y exporta en segundo plano. El trabajo de verdad está en Source/core/SfgrWriter. Los pianos Nord
// pasan por el mismo camino, convertidos antes por Source/core/PianoExport.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace bk::sfgrui
{
/** `source`: el .nsmp3/.nsmp4/.bkm que suena en SAMPLES; ataque y liberación actuales de la sección. */
void exportToBeTracks (const juce::File& source, float attackSec, float releaseSec);

/** Un piano Nord (.npno) a BeTracks Pro: zonas por tecla con la caída de cada nota, el apagador de cada tecla y
 *  los ruidos de soltar tecla (Source/core/PianoExport). Sin las grabaciones con el pedal pisado. */
void exportPianoToBeTracks (const juce::File& npno);
} // namespace bk::sfgrui
