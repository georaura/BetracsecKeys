// BeTracks Keys — exportar a BeTracks Pro lo que Keys sintetiza (órgano y synth): Keys toca su propio motor nota a
// nota, sin tiempo real ni hardware, recorta cada toma, busca el bucle de la parte sostenida y escribe un `.sfgr`
// con Source/core/SfgrWriter. Lo que es "en vivo" (drawbars, rueda, rotary rápido/lento) queda fijo en la muestra.
#pragma once

#include "PluginProcessor.h"

namespace bk::selfsample
{
/** Qué se exporta: el órgano, el synth o la performance FM, con los ajustes que tienen ahora en Keys. */
enum class Source { Organ, Synth, Fm };

/** Pregunta las opciones, el destino y exporta en segundo plano. */
void exportToBeTracks (KeysProcessor& proc, Source source);

} // namespace bk::selfsample
