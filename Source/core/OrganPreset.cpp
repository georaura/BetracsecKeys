#include "OrganPreset.h"

#include <cmath>
#include <cstring>

namespace bk::organ
{
const char* modelName (Model m)
{
    switch (m)
    {
        case Model::B3:     return "B3";
        case Model::Vox:    return "Vox";
        case Model::Farf:   return "Farfisa";
        case Model::Pipe1:  return "Pipe 1";
        case Model::Pipe2:  return "Pipe 2";
        case Model::B3Bass: return "B3 Bass";
    }
    return "B3";
}

const char* vibName (VibType t)
{
    static const char* names[] = { "V1", "V2", "V3", "C1", "C2", "C3" };
    return names[int (t)];
}

namespace
{
/** Bits [beg, end] (MSB primero) como entero sin signo; -1 si se salen del archivo. */
int field (const uint8_t* d, size_t size, int beg, int end)
{
    if (beg < 0 || size_t (end >> 3) >= size) return -1;
    int v = 0;
    for (int bit = beg; bit <= end; ++bit)
        v = (v << 1) | ((d[bit >> 3] >> (7 - (bit & 7))) & 1);
    return v;
}

/** Posiciones de cada campo en la capa A; la capa B está 248 bits más allá (salvo el on/off). */
struct Layout
{
    int on, volBeg, volEnd, octBeg, octEnd, sustain, modelBeg, modelEnd, vib, perc, perc3rd, percFast, percSoft;
    int drawbarBeg[9];
};
constexpr int kLayerB = 248;
constexpr Layout kLayerA {
    394, 398, 404, 540, 543, 544, 545, 547, 760, 761, 762, 763, 764,
    { 568, 587, 606, 625, 644, 663, 682, 701, 720 },
};
} // namespace

bool looksLikeNs4o (const uint8_t* d, size_t size)
{
    return size >= 12 && std::memcmp (d, "CBIN", 4) == 0 && std::memcmp (d + 8, "ns4o", 4) == 0;
}

std::optional<OrganPreset> parseNs4o (const uint8_t* d, size_t size, std::string& error)
{
    if (! looksLikeNs4o (d, size)) { error = "No es un preset de órgano compatible (.ns4o)."; return std::nullopt; }
    OrganPreset preset;
    for (int L = 0; L < 2; ++L)
    {
        const int o = L * kLayerB;
        const auto& a = kLayerA;
        const int on = field (d, size, L == 0 ? a.on : 393, L == 0 ? a.on : 393);
        const int model = field (d, size, a.modelBeg + o, a.modelEnd + o);
        if (on < 0 || model < 0) { if (L == 0) { error = "El preset está incompleto."; return std::nullopt; } break; }
        preset.layerOn[size_t (L)] = on == 1;
        auto& p = preset.layers[size_t (L)];
        p.model = model <= 5 ? Model (model) : Model::B3;
        for (int i = 0; i < 9; ++i)
        {
            const int raw = field (d, size, a.drawbarBeg[i] + o, a.drawbarBeg[i] + 3 + o);
            float v = float (raw < 0 ? 0 : std::min (raw, 8));
            if (p.model == Model::Farf) v = v >= 4 ? 8.0f : 0.0f; // pestañas: arriba o abajo
            if (p.model == Model::Vox && i == 8 && raw >= 0 && raw <= 4) v = float (raw + 1); // selector 1–5 (0..4 → 1..5), no es un nivel
            p.drawbars[size_t (i)] = v;
        }
        const int vol = field (d, size, a.volBeg + o, a.volEnd + o);
        p.volumeDb = vol <= 0 ? -120.0f : float (40.0 * std::log10 (vol / 127.0));
        const int oct = field (d, size, a.octBeg + o, a.octEnd + o);
        p.octaveShift = oct >= 14 ? oct - 16 : (oct <= 2 ? oct : 0);
        p.sustainPedal = field (d, size, a.sustain + o, a.sustain + o) == 1;
        p.vibOn = field (d, size, a.vib + o, a.vib + o) == 1;
        p.percussion.on = field (d, size, a.perc + o, a.perc + o) == 1;
        p.percussion.harm3rd = field (d, size, a.perc3rd + o, a.perc3rd + o) == 1;
        p.percussion.decayFast = field (d, size, a.percFast + o, a.percFast + o) == 1;
        p.percussion.volSoft = field (d, size, a.percSoft + o, a.percSoft + o) == 1;
        // El tipo de vibrato (V1–C3) y el rotary no están en el preset: son ajustes globales del programa.
    }
    if (! preset.layerOn[0] && ! preset.layerOn[1]) preset.layerOn[0] = true;
    return preset;
}
} // namespace bk::organ
