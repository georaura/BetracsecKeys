// BeTracks Keys — exportación a BeTracks Pro (.sfgr v1).
//
// BeTracks Pro (la app de escenario de BeTracks) toca instrumentos en su formato `.sfgr`: PCM de 16 bits a una
// sola frecuencia por archivo, todo en RAM, sin leer el disco mientras se toca. Keys es el sitio donde el
// usuario prepara sus sonidos (sus samples, lo que muestrea de sus instrumentos), y esto los escribe en ese
// formato. El diseño del archivo sigue la especificación pública de `.sfgr` v1 (cabecera de 160 bytes,
// zonas de 56, índice de muestras de 32, PCM16 intercalado, CRC32 IEEE); el código es de Keys.
//
// Lo que hace al exportar:
//  - pasa todas las muestras a 48 kHz (el `.sfgr` exige una frecuencia por archivo);
//  - lleva el golpe más fuerte a −1 dBFS (las diferencias entre capas se mantienen);
//  - recorta las colas por debajo de un umbral respecto al golpe más fuerte (menos RAM, carga más rápida);
//  - compensa las capas de velocidad: BeTracks Pro aplica (velocidad/127)^TOUCH encima de la muestra, así que una
//    capa grabada suave sonaría dos veces más suave; cada zona lleva el volumen que la deja en su nivel
//    natural a su velocidad de grabación (con TOUCH en su valor por defecto, 2);
//  - la caída del sonido de cada zona (bucles que se apagan, como los de un piano o un Nord Sample) va en el
//    bloque de modulación con registros de 16 bytes: BeTracks Pro la entiende desde su cambio 008, y una versión
//    anterior lee los 12 bytes de siempre y la ignora. Sin caída no se escribe bloque: el archivo es el de v1.
#pragma once

#include "SampleEngine.h"

#include <cstdint>
#include <string>
#include <vector>

namespace bk::sfgr
{
/** Lo que una zona lleva además de lo que dice el sample (pianos Nord, sobre todo). Todo en cero / −1 =
 *  la zona se escribe como siempre. */
struct ZoneExtra
{
    /** Caída del sonido en dB por segundo antes del bucle y dentro de él (BeTracks Pro con el cambio 008; una
     *  versión anterior lo ignora y el bucle no se apaga). */
    float preLoopDecayDbPerSec = 0, loopDecayDbPerSec = 0;
    float releaseSec = -1;            ///< tiempo hasta −80 dB al soltar; < 0 = el de ExportOptions
    float volumeDb = 0;               ///< se suma al volumen de la zona
    bool releaseTrigger = false;      ///< suena al SOLTAR la tecla (ruido del apagador)
    float rtDecayDbPerSec = 0;        ///< zonas de soltado: dB menos por segundo que se sostuvo la nota (0–25,5)
};

struct ExportOptions
{
    double trimDb = -70;          ///< colas por debajo de esto (respecto al golpe más fuerte); 0 = sin recorte
    float attackSec = 0.002f;
    float releaseSec = 0.3f;
    double touchCurve = 2.0;      ///< el TOUCH de BeTracks Pro con el que se compensan las capas
    uint32_t sampleRate = 48000;
    std::vector<ZoneExtra> zoneExtras; ///< vacío, o uno por zona
};

struct ExportResult
{
    bool ok = false;
    std::string error;
    uint64_t fileBytes = 0, ramBytes = 0, ramBytesBeforeTrim = 0;
    int zones = 0, samples = 0;
};

/** Lo que ocuparía (sin escribir nada): para enseñarlo antes de exportar. */
ExportResult estimate (const bk::nsmp::SampleSound& sound, const ExportOptions& options);

/** Escribe el `.sfgr`. `name` es el nombre que verá BeTracks Pro (hasta 63 bytes). */
ExportResult write (const std::string& path, const std::string& name, const bk::nsmp::SampleSound& sound,
                    const ExportOptions& options);

/** CRC32 IEEE 802.3 (el de `.sfgr`); encadenable con `seed`. */
uint32_t crc32 (const void* data, size_t len, uint32_t seed = 0);
} // namespace bk::sfgr
