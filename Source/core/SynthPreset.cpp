#include "SynthPreset.h"
#include "SampleNames.h"

#include <algorithm>
#include <cstring>
#include <iterator>

namespace bk::synth
{
bool looksLikeNs4y (const uint8_t* d, size_t size)
{
    return size >= 12 && std::memcmp (d, "CBIN", 4) == 0 && std::memcmp (d + 8, "ns4y", 4) == 0;
}

std::optional<SynthPreset> parseNs4y (const uint8_t* d, size_t size, std::string& error)
{
    if (! looksLikeNs4y (d, size)) { error = "No es un preset de synth compatible (.ns4y)."; return std::nullopt; }
    SynthPreset p;
    for (int L = 0; L < 3; ++L)
        for (int f = 0; f < kFieldCount; ++f)
        {
            const int beg = kFields[f].beg[L], end = kFields[f].end[L];
            int64_t v = -1;
            if (beg >= 0 && size_t (end >> 3) < size)
            {
                v = 0;
                for (int bit = beg; bit <= end; ++bit) v = (v << 1) | ((d[bit >> 3] >> (7 - (bit & 7))) & 1);
            }
            p.layers[size_t (L)].raw[size_t (f)] = v;
        }
    if (! p.layers[0].on() && ! p.layers[1].on() && ! p.layers[2].on()) p.layers[0].raw[F_on] = 1;
    return p;
}

// Nombres de las ondas analógicas (OpenNord synth-analog.generated.ts, de ns4decode, MIT).
const char* analogCategoryName (int c)
{
    static const char* n[] = { "Pure", "Sub Osc", "Sync", "Shape", "Shape Sine", "Multi", "Super", "Misc" };
    return c >= 0 && c < 8 ? n[c] : "Analog";
}

const char* analogWaveName (int c, int w)
{
    static const char* pure[] = { "Pure Sine", "Pure Triangle", "Pure Saw", "Pure Square", "Pure Pulse 33", "Pure Pulse 10", "Pure White Noise" };
    static const char* sub[] = { "Sub Square Saw", "Sub Pulse Saw", "Sub Sine Saw" };
    static const char* sync[] = { "Sync Sine", "Sync Triangle", "Sync Saw", "Sync Square", "Sync Pulse 33", "Sync Pulse 10", "Sync Chop Saw 1", "Sync Chop Saw 2", "Sync Chop Square" };
    static const char* shape[] = { "Shape Triangle", "Shape Saw", "Shape Square", "Shape Half Sine", "Shape Parabolic" };
    static const char* shapeSine[] = { "Shape Sine Saw", "Shape Sine Square", "Shape Sine Pulse", "Shape Sine Squeeze", "Shape Sine Fold", "Shape Sine ESaw", "Shape Sine ESquare" };
    static const char* multi[] = { "Multi Saw", "Multi Saw 8ve", "Multi Saw 5th", "Multi Saw 5th+", "Multi Saw 8ve 8ve+", "Multi Saw 8ve 5th+" };
    static const char* super[] = { "Super Saw", "Super Square", "Super Organ", "Super Saw Bright", "Super Square Bright" };
    static const char* misc[] = { "Pink Noise", "Red Noise", "Bell" };
    struct Cat { const char* const* names; int count; };
    static const Cat cats[] = { { pure, 7 }, { sub, 3 }, { sync, 9 }, { shape, 5 }, { shapeSine, 7 }, { multi, 6 }, { super, 5 }, { misc, 3 } };
    if (c < 0 || c >= 8 || w < 0 || w >= cats[c].count) return nullptr;
    return cats[c].names[w];
}

const char* factorySampleName (uint32_t id)
{
    const auto* b = std::begin (bk::nsmp::kSampleNames);
    const auto* e = std::end (bk::nsmp::kSampleNames);
    const auto* it = std::lower_bound (b, e, id, [] (const bk::nsmp::SampleName& n, uint32_t v) { return n.id < v; });
    return it != e && it->id == id ? it->name : nullptr;
}
uint32_t soundId (const std::string& name)
{
    const auto* d = reinterpret_cast<const uint8_t*> (name.data());
    const uint32_t m = 0x5bd1e995u;
    const size_t n = name.size();
    uint32_t h = uint32_t (n);
    size_t i = 0;
    for (; n - i >= 4; i += 4)
    {
        uint32_t k = (uint32_t (d[i]) << 24) | (uint32_t (d[i + 1]) << 16) | (uint32_t (d[i + 2]) << 8) | d[i + 3];
        k *= m;
        k ^= k >> 24;
        k *= m;
        h = (h * m) ^ k;
    }
    const size_t rest = n - i;
    if (rest == 3) h ^= uint32_t (d[i + 2]) << 16;
    if (rest >= 2) h ^= uint32_t (d[i + 1]) << 8;
    if (rest >= 1) h = (h ^ d[i]) * m;
    h ^= h >> 13;
    h *= m;
    h ^= h >> 15;
    return h == 0 ? 1 : h;
}
} // namespace bk::synth
