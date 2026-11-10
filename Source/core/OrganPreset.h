// BeTracks Keys — presets de órgano del Stage 4 (.ns4o).
//
// Un .ns4o es el bloque de órgano de un programa del Stage 4 reubicado al byte 49 (OpenNord,
// src/lib/ns4/preset.ts: shift 45). Las posiciones de cada campo (bits MSB-first) salen del mapa
// de parámetros de OpenNord (AGPL-3.0), volcado con tools/inspect/organmap.ts. Solo ajustes: no
// contiene audio.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace bk::organ
{
enum class Model : uint8_t { B3, Vox, Farf, Pipe1, Pipe2, B3Bass };
const char* modelName (Model m);

/** Vibrato del scanner (V1–V3) o chorus (C1–C3). */
enum class VibType : uint8_t { V1, V2, V3, C1, C2, C3 };
const char* vibName (VibType t);

struct Percussion
{
    bool on = false, harm3rd = false, decayFast = true, volSoft = false;
};

struct OrganParams
{
    Model model = Model::B3;
    std::array<float, 9> drawbars { 8, 8, 8, 0, 0, 0, 0, 0, 0 }; ///< 0..8
    Percussion percussion;
    bool vibOn = false;
    VibType vibType = VibType::C3;
    float volumeDb = 0;        ///< ≤ −120 = silencio
    int octaveShift = 0;       ///< −2..+2
    bool sustain = false;      ///< interruptor Sustain del órgano: las notas se apagan despacio (no viene en el preset)
    bool sustainPedal = false; ///< el órgano obedece al pedal de sustain
    float keyClick = 0.5f;     ///< 0..1 (ruedas fónicas)
};

struct OrganPreset
{
    std::string name;                         ///< del nombre del archivo
    std::array<bool, 2> layerOn {};           ///< capas A y B
    std::array<OrganParams, 2> layers {};
};

/** true si los bytes son un .ns4o (CBIN + etiqueta ns4o). */
bool looksLikeNs4o (const uint8_t* data, size_t size);
std::optional<OrganPreset> parseNs4o (const uint8_t* data, size_t size, std::string& error);
} // namespace bk::organ
