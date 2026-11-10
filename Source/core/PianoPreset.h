// BeTracks Keys — presets de piano del Stage 4 (.ns4n) y cómo encontrar el .npno que usan.
//
// Un .ns4n es el bloque de piano de un programa del Stage 4 reubicado al byte 49 (OpenNord,
// preset.ts: shift 180). Posiciones de los campos volcadas del mapa de OpenNord (AGPL-3.0) con
// tools/inspect/presetmap.ts p 180. El preset no lleva audio: nombra su piano con un id de 32 bits.
// Ese id no se puede calcular a partir de un .npno, así que se busca por nombre, con la regla de
// OpenNord (library/piano-match.ts).
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace bk::npno
{
struct PianoLayerPreset
{
    bool on = false;
    int pianoType = 0;           ///< 0 Grand, 1 Upright, 2 Electric, 3 Clav, 4 Digital, 5 Misc
    uint32_t modelId = 0;
    std::string modelName;       ///< nombre del panel ("White Grand XL"); vacío si el id no está en la tabla
    int touch = 1;               ///< 0 ligero, 1 normal, 2 duro (como el parámetro de Keys)
    bool softRelease = false, stringRes = false, pedalNoise = false;
    float volumeDb = 0;          ///< ≤ −120 = silencio
    int octaveShift = 0;
    int timbre = 0;              ///< valor crudo (0..3); su significado depende del tipo de piano. Keys no lo aplica
    int dynComp = 0;             ///< 0..3
};

struct PianoPreset
{
    std::array<PianoLayerPreset, 2> layers {};
};

const char* pianoLayerTypeName (int type);
/** Nombre del panel para un id de modelo, o nullptr. */
const char* pianoModelName (uint32_t id);

bool looksLikeNs4n (const uint8_t* data, size_t size);
std::optional<PianoPreset> parseNs4n (const uint8_t* data, size_t size, std::string& error);

/** Un .npno del usuario, tal como hace falta para buscarlo por nombre. */
struct LocalPiano
{
    std::string path;
    std::string fileName;        ///< sin la extensión .npno
    std::string headerName, headerVariant;
    int headerVersion = 0;       ///< versión de la cabecera ×100 (6.10 → 610); 0 si no se sabe
};

/** El piano local que usa un preset (por nombre del panel), o -1. Si hay varios, el de versión más alta. */
int findPianoByName (const std::string& panelName, const std::vector<LocalPiano>& locals);
} // namespace bk::npno
