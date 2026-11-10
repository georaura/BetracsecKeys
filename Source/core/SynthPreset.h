// BeTracks Keys — presets de synth del Stage 4 (.ns4y).
//
// Un .ns4y es el bloque de synth de un programa del Stage 4 reubicado al byte 49 (OpenNord, preset.ts:
// shift 327), con hasta tres capas (A, B, C). Las posiciones de los campos salen del mapa de OpenNord
// (Source/core/SynthFields.h, generado). Aquí se leen los valores crudos (0..127 casi todos); las
// escalas a Hz, segundos o semitonos están en SynthEngine (synth-scaling.ts de OpenNord).
#pragma once

#include "SynthFields.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace bk::synth
{
struct SynthLayer
{
    std::array<int64_t, kFieldCount> raw {};
    int64_t operator[] (Field f) const { return raw[size_t (f)]; }
    bool on() const { return raw[F_on] == 1; }
    /** Afinación gruesa (semitonos, con signo de 6 bits) y fina (cents, con signo de 7 bits). */
    int coarse() const { const auto v = int (raw[F_coarse]); return v >= 32 ? v - 64 : v; }
    int fineCents() const { const auto v = int (raw[F_fine]); return v >= 64 ? v - 128 : v; }
    /** Cantidad de la envolvente del oscilador, −63..63 (con signo de 7 bits). */
    int oscEnvAmount() const { const auto v = int (raw[F_oscEnvAmt]); return v >= 64 ? v - 128 : v; }
    int octave() const { const auto v = int (raw[F_octave]); return v >= 14 ? v - 16 : (v <= 2 ? v : 0); }
    bool usesSample() const { return raw[F_source] == 0; }
    uint32_t sampleId() const { return uint32_t (raw[F_sampleId]); }
};

struct SynthPreset
{
    std::array<SynthLayer, 3> layers {};
};

bool looksLikeNs4y (const uint8_t* data, size_t size);
std::optional<SynthPreset> parseNs4y (const uint8_t* data, size_t size, std::string& error);

/** Nombre de la onda analógica ("Pure Saw") por categoría y onda, o nullptr. */
const char* analogWaveName (int category, int wave);
const char* analogCategoryName (int category);
/** Nombre del archivo de sample de fábrica para un id (sin extensión), o nullptr. */
const char* factorySampleName (uint32_t id);
/**
 * El id de 32 bits con que un programa o preset del Stage 4 nombra un piano o un sample: MurmurHash2
 * (m = 0x5bd1e995, r = 24, semilla 0, bloques de 4 bytes big-endian) del nombre de la librería, que es el
 * nombre del archivo de fábrica sin extensión (OpenNord clavia/sound-id.ts). Un 0 se guarda como 1.
 */
uint32_t soundId (const std::string& libraryName);
} // namespace bk::synth
