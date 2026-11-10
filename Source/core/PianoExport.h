// BeTracks Keys — un piano Nord (.npno ya decodificado) como instrumento de BeTracks Pro (.sfgr).
//
// Lo que hace el piano de Keys (PianoEngine) y cómo queda en el archivo:
//  - cada tecla toca la grabación de su zona según la fuerza → una zona por TECLA y capa de velocidad, con
//    el tono de la grabación (raíz + 4) como nota central;
//  - nivel: la fuerza a 0,2 dB por paso es (velocidad/127)², que es justo la curva de BeTracks Pro con TOUCH en
//    2; cada grabación está normalizada a su pico, así que las zonas van a 0 dB;
//  - antes del bucle, la caída extra por semitono bajo la raíz; dentro, la del golpe → la caída por zona
//    (dB/s) del cambio 008 de BeTracks Pro; por eso la zona es por tecla;
//  - al soltar: el apagador de la tecla (T60) → el release de la zona (hasta −80 dB = T60 × 4/3); las teclas
//    sin apagador suenan con release largo y las apaga su caída;
//  - el ruido de soltar tecla → zonas de soltado, por capa, con su nivel relativo y una caída según lo que
//    se sostuvo la nota.
// Lo que no pasa: las grabaciones con el pedal pisado (String Res), el Soft Release y la elección del ruido
// según lo que suena en ese momento (BeTracks Pro lo elige por la fuerza).
#pragma once

#include "PianoSound.h"
#include "SampleEngine.h"
#include "SfgrWriter.h"

#include <string>
#include <vector>

namespace bk::pianoexport
{
struct Converted
{
    bk::nsmp::SampleSound sound;               ///< golpes (el PCM se MUEVE desde el piano) y zonas
    std::vector<bk::sfgr::ZoneExtra> extras;   ///< uno por zona
    int attackZones = 0, releaseZones = 0;
};

/** Pasa el piano a zonas de BeTracks Pro. Mueve el PCM fuera de `piano` (queda vacío). false con `error`. */
bool convert (PianoSound& piano, Converted& out, std::string& error);

/** El mando de ruido de soltar tecla de BeTracks Pro arranca en +12 dB (ganancia 3,98); las zonas de soltar tecla
 *  se escriben 12 dB más bajas para que, con el mando en su sitio, el ruido suene como en Keys. */
constexpr double kBeTracksNoiseBoostDb = 12.0;

/** Cuánto baja (dB) la nota `key` a esa fuerza en `seconds`, tocada en el piano de Keys sin ruido de soltar. */
double noteDropDb (const PianoSound& piano, int key, int velocity, double seconds);

/** dB por segundo de un factor por actualización de envolvente (22.050/s); 0 si no cae. */
double decayDbPerSecond (double perUpdate);
} // namespace bk::pianoexport
