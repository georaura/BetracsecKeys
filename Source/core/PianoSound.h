// BeTracks Keys — un piano .npno cargado y decodificado en memoria, listo para el hilo de audio.
#pragma once

#include "Npno.h"

#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace bk
{
/** Un golpe (capa) decodificado: PCM de 16 bits normalizado a su pico, canales intercalados.
 *  `pcm` vacío = lista no cargada (ver LoadOptions). */
struct StrokeAudio
{
    std::vector<int16_t> pcm;
    uint32_t frames = 0;
    int zone = 0, flags = 0;
    int floor = 0, partner = 0;
    bool loop = false;
    uint32_t loopStart = 0, loopEnd = 0;
    double loopDecay = 1.0;
    std::array<double, 14> preLoop {};
    int damper = 0, damperSoft = 0;
};

/** Qué listas de grabaciones se cargan además de las normales (pedal arriba). */
struct LoadOptions
{
    bool pedalDown = true;   ///< String Res: las grabaciones con el pedal pisado (pianos Med/Lrg/XL)
    bool releases = true;    ///< sonidos de soltar tecla
    bool operator== (const LoadOptions&) const = default;
};

struct PianoSound
{
    std::string path;
    npno::Piano meta;        ///< nombre, zonas, golpes (sin audio)
    int channels = 1;
    std::vector<StrokeAudio> strokes;
    LoadOptions options;     ///< con qué opciones se cargó
    bool hasPedalDown = false, hasReleases = false; ///< listas cargadas y presentes en el archivo

    /** Bytes de audio en memoria. */
    size_t memoryBytes() const;
    /** "Nombre (variante)". */
    std::string displayName() const;
};

/**
 * Lee y decodifica un .npno, en varios hilos. `progress` (0–1) y `cancel` son opcionales.
 * nullptr con `error` si no se puede.
 */
std::unique_ptr<PianoSound> loadPiano (const std::string& path, std::string& error,
                                       std::atomic<float>* progress = nullptr,
                                       const std::atomic<bool>* cancel = nullptr,
                                       LoadOptions options = {});
} // namespace bk
