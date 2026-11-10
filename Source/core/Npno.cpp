// BeTracks Keys — lectura de pianos Nord (.npno). Port de OpenNord (AGPL-3.0); ver Npno.h.
#include "Npno.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

namespace bk::npno
{
namespace
{
constexpr size_t kMetaOffset = 0x16, kMetaSize = 0x67;
constexpr size_t kKeymapOffset = 0x8c;
constexpr size_t kDamperTable = 0x20c;
constexpr size_t kUndampedFrom = 0x40d;
constexpr size_t kDirOffset = 0x61c;
constexpr size_t kLayerTable = 0x622;
constexpr size_t kRecords = 0x732;
constexpr size_t kRecordSize = 0x76;
constexpr int kCnspV6 = 0x464;
constexpr int kPredictors[5][4] = { { 0, 0, 0, 0 }, { 1, 0, 0, 0 }, { 2, -1, 0, 0 }, { 3, -3, 1, 0 }, { 4, -6, 4, -1 } };

uint16_t be16 (const uint8_t* p) { return uint16_t ((p[0] << 8) | p[1]); }
uint32_t be32 (const uint8_t* p) { return (uint32_t (p[0]) << 24) | (uint32_t (p[1]) << 16) | (uint32_t (p[2]) << 8) | p[3]; }
int16_t be16s (const uint8_t* p) { return int16_t (be16 (p)); }

long findCnsp (const uint8_t* d, size_t size)
{
    for (size_t i = 0; i + 4 <= std::min<size_t> (size, 0x80); ++i)
        if (std::memcmp (d + i, "CNSP", 4) == 0)
            return long (i);
    return -1;
}

std::string trim (const std::string& s)
{
    // Quita los espacios de los extremos y junta los repetidos ("Ya G2     XL" → "Ya G2 XL").
    std::string out;
    for (char c : s)
    {
        if (c == ' ' || c == '\t') { if (! out.empty() && out.back() != ' ') out += ' '; }
        else out += c;
    }
    while (! out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

/** Primer tramo ASCII imprimible de al menos minLen caracteres en [from, to). */
std::string firstAsciiRun (const uint8_t* d, size_t size, size_t from, size_t to, size_t minLen = 3)
{
    std::string run;
    for (size_t i = from; i < std::min (to, size); ++i)
    {
        const uint8_t c = d[i];
        if (c >= 0x20 && c < 0x7f) run += char (c);
        else
        {
            if (run.size() >= minLen) return trim (run);
            run.clear();
        }
    }
    return run.size() >= minLen ? trim (run) : std::string();
}

std::array<int, 128> readLayerTable (const uint8_t* d, size_t base)
{
    std::array<int, 128> t {};
    for (int n = 0; n < 128; ++n) t[size_t (n)] = be16 (d + base + kLayerTable + size_t (n) * 2);
    return t;
}

std::vector<Zone> readKeyMap (const uint8_t* d, size_t size, size_t cnsp)
{
    const size_t at = cnsp + kKeymapOffset;
    if (at + 128 > size) return {};
    std::map<int, std::pair<int, int>> span; // raíz → [grave, aguda]
    for (int note = 0; note < 128; ++note)
    {
        const int r = d[at + size_t (note)];
        if (r == 0 || r == 0xff || r > 127) continue;
        auto it = span.find (r);
        if (it == span.end()) span[r] = { note, note };
        else { it->second.first = std::min (it->second.first, note); it->second.second = std::max (it->second.second, note); }
    }
    if (span.empty()) return {};
    if (span.size() == 1)
    {
        // Una sola zona solo cuenta si la tabla de capas por raíz dice lo mismo.
        if (cnsp + kLayerTable + 256 > size) return {};
        const int root = span.begin()->first;
        const auto t = readLayerTable (d, cnsp);
        for (int n = 0; n < 128; ++n)
            if ((n == root) != (t[size_t (n)] > 0)) return {};
    }
    std::vector<Zone> zones;
    for (auto& [root, lh] : span) zones.push_back ({ root, lh.first, lh.second });
    std::sort (zones.begin(), zones.end(), [] (const Zone& a, const Zone& b) { return a.low < b.low; });
    return zones;
}
} // namespace

double touchAttenuation (int velocity)
{
    const double v = std::clamp (velocity, 1, 127);
    return std::clamp (-200.0 * std::log10 (v / 127.0), 0.0, 200.0);
}

double attenuationGain (double attenuation) { return std::pow (10.0, -attenuation / 100.0); }

double damperT60 (int index) { return 0.0015 * std::pow (10.0, index / 20.0); }

std::array<int, 3> Piano::listCounts() const
{
    std::array<int, 3> n {};
    for (const auto& s : strokes) ++n[size_t (s.flags & 3) % 3];
    return n;
}

int Piano::zoneForKey (int midi) const
{
    for (size_t z = 0; z < zones.size(); ++z)
        if (zones[z].low <= midi && midi <= zones[z].high) return int (z);
    return -1;
}

int Piano::strokeFor (int zone, int velocity, int selectOffset, int list) const
{
    return strokeForAttenuation (zone, touchAttenuation (velocity) + selectOffset, list);
}

int Piano::strokeForAttenuation (int zone, double att, int list) const
{
    int pick = -1, prevFloor = 0;
    for (const auto& s : strokes)
    {
        if (s.zone != zone || s.flags != list) continue;
        if (pick < 0) pick = s.index;
        else
        {
            // Cambia de capa a medio camino entre suelos (truncado hacia cero, como Math.trunc).
            const int sw = s.floor - (s.floor - prevFloor) / 2;
            if (sw <= att) pick = s.index;
        }
        prevFloor = s.floor;
    }
    return pick;
}

bool looksLikeNpno (const uint8_t* d, size_t size)
{
    if (size < 12 || std::memcmp (d, "CBIN", 4) != 0) return false;
    size_t end = 12;
    while (end > 8 && (d[end - 1] == 0 || d[end - 1] == 0x20)) --end;
    return std::string ((const char*) d + 8, end - 8) == "npno";
}

std::optional<Identity> readIdentity (const uint8_t* d, size_t size)
{
    if (! looksLikeNpno (d, size) || size < 0x16) return std::nullopt;
    const long c = findCnsp (d, size);
    if (c < 0 || size_t (c) + kMetaOffset + kMetaSize > size) return std::nullopt;
    Identity id;
    const int raw = d[0x14] | (d[0x15] << 8);
    char ver[16];
    std::snprintf (ver, sizeof ver, "%d.%02d", raw / 100, raw % 100);
    id.version = ver;
    const auto full = firstAsciiRun (d, size, size_t (c) + kMetaOffset, size_t (c) + kMetaOffset + kMetaSize);
    const auto hash = full.find ('#');
    id.name = hash == std::string::npos ? full : trim (full.substr (0, hash));
    id.variant = hash == std::string::npos ? std::string() : trim (full.substr (hash + 1));
    id.pianoType = d[size_t (c) + 0x18];
    return id;
}

const char* pianoTypeName (int type)
{
    switch (type)
    {
        case 5:  return "Grand";
        case 6:  return "Upright";
        case 1:  return "Electric Grand";
        case 2:  return "Electric";
        case 3:  return "Electric Reed";
        case 4:  return "Clav";
        case 7:  return "Harpsichord";
        case 14: return "Digital";
        case 15: return "Misc";
        case 16: return "Mallets";
        default: return "Other";
    }
}

int framesPerBlock (int blockWords, int width, int channels)
{
    return ((blockWords - 1) * 16) / (width * channels) - kBlockOverlap;
}

std::optional<Piano> parse (const uint8_t* d, size_t size, std::string& error)
{
    if (! looksLikeNpno (d, size)) { error = "No es un piano compatible (.npno)."; return std::nullopt; }
    Piano p;
    p.versionRaw = d[0x14] | (d[0x15] << 8);
    const int major = p.versionRaw / 100;
    char ver[16];
    std::snprintf (ver, sizeof ver, "%d.%02d", major, p.versionRaw - major * 100);
    p.version = ver;

    const long c = findCnsp (d, size);
    if (c < 0 || size_t (c) + kRecords > size) { error = "El piano no tiene directorio de audio."; return std::nullopt; }
    const size_t base = size_t (c);

    const auto full = firstAsciiRun (d, size, base + kMetaOffset, base + kMetaOffset + kMetaSize);
    const auto hash = full.find ('#');
    p.name = hash == std::string::npos ? full : trim (full.substr (0, hash));
    p.variant = hash == std::string::npos ? std::string() : trim (full.substr (hash + 1));
    p.pianoType = d[base + 0x18];
    p.undampedFrom = d[base + kUndampedFrom];
    for (size_t k = 0; k < 128; ++k) p.damperByKey[k] = d[base + kDamperTable + k];

    p.zones = readKeyMap (d, size, base);

    p.dirVersion = be16 (d + base + kDirOffset);
    p.channels = be16 (d + base + kDirOffset + 2);
    const int count = be16 (d + base + kDirOffset + 4);
    if ((p.channels != 1 && p.channels != 2) || count == 0 || base + kRecords + size_t (count) * kRecordSize > size)
    { error = "El directorio del piano está dañado."; return std::nullopt; }

    const int floorScale = p.dirVersion < kCnspV6 ? 5 : 1;
    for (int i = 0; i < count; ++i)
    {
        const uint8_t* r = d + base + kRecords + size_t (i) * kRecordSize;
        Stroke s;
        s.index = i;
        s.offset = base + be32 (r);
        s.flags = r[0x04] & 3;
        s.floor = r[0x05] * floorScale;
        s.frames = be32 (r + 0x06);
        s.blocks = be16 (r + 0x0a);
        for (int ch = 0; ch < p.channels; ++ch)
        {
            const uint8_t* h = r + (ch == 0 ? 0x0c : 0x14);
            for (int k = 0; k < 4; ++k) s.history[size_t (ch)][size_t (3 - k)] = be16s (h + k * 2); // el más reciente primero
        }
        s.loopStart = be32 (r + 0x1c);
        s.loopLength = be32 (r + 0x20);
        s.hasLoop = s.loopLength != 0;
        s.loopDecay = s.hasLoop ? be32 (r + 0x2e) / 8388608.0 : 1.0;
        for (size_t k = 0; k < 14; ++k)
        {
            const double f = be32 (r + 0x36 + k * 4) / 8388608.0;
            s.preLoop[k] = (f > 0.0 && f <= 1.0) ? f : 1.0; // 0 o >1 sería un valor inservible
        }
        s.damper = r[0x72];
        s.damperSoft = r[0x73];
        s.partner = r[0x33] * floorScale; // en v5, pasos de 1 dB como el suelo
        s.level = be16s (r + 0x34);
        p.strokes.push_back (s);
    }

    // Zona de cada golpe, por la tabla de capas por raíz; si no cuadra, "un suelo 0 abre zona".
    const auto table = readLayerTable (d, base);
    std::vector<int> counts;
    int total = 0;
    for (int n : table) if (n > 0) { counts.push_back (n); total += n; }
    if (total == count)
    {
        size_t i = 0;
        for (size_t z = 0; z < counts.size(); ++z)
            for (int k = 0; k < counts[z]; ++k) p.strokes[i++].zone = int (z);
    }
    else
    {
        int zone = -1;
        for (auto& s : p.strokes)
        {
            if ((s.floor == 0 && s.flags != 2) || zone < 0) ++zone;
            s.zone = zone;
        }
    }

    // Los golpes van seguidos, así que el tamaño de bloque sale de dos vecinos.
    if (p.strokes.size() > 1 && p.strokes[0].blocks)
    {
        const long diff = long (p.strokes[1].offset) - long (p.strokes[0].offset);
        const long denom = long (p.strokes[0].blocks) * 2;
        if (diff <= 0 || diff % denom != 0) { error = "Tamaño de bloque irregular."; return std::nullopt; }
        p.blockWords = int (diff / denom);
    }
    else p.blockWords = 511 * p.channels;
    if (p.blockWords < 2) { error = "Tamaño de bloque inválido."; return std::nullopt; }

    for (const auto& s : p.strokes)
    {
        const size_t end = s.offset + size_t (s.blocks) * size_t (p.blockWords) * 2;
        const long maxFrames = long (s.blocks) * framesPerBlock (p.blockWords, 1, p.channels);
        if (end > size || long (s.frames) > maxFrames) { error = "Un golpe del piano está fuera del archivo."; return std::nullopt; }
    }

    int zoneCount = 0;
    for (const auto& s : p.strokes) zoneCount = std::max (zoneCount, s.zone + 1);
    if (p.zones.empty() || int (p.zones.size()) != zoneCount)
    { error = "El mapa de teclas no corresponde con las grabaciones."; return std::nullopt; }
    return p;
}

bool decodeStroke (const uint8_t* d, size_t size, const Piano& p, const Stroke& s, int32_t* out, std::string& error)
{
    const int channels = p.channels, blockWords = p.blockWords;
    if (long (s.frames) > long (s.blocks) * framesPerBlock (blockWords, 1, channels)
        || s.offset + size_t (s.blocks) * size_t (blockWords) * 2 > size)
    { error = "golpe " + std::to_string (s.index) + ": no cabe en sus bloques"; return false; }

    auto hist = s.history;
    uint32_t t = 0;
    for (int b = 0; b < s.blocks; ++b)
    {
        const uint8_t* at = d + s.offset + size_t (b) * size_t (blockWords) * 2;
        const uint16_t header = be16 (at);
        const int width = header & 0x1f;
        const int order = (header >> 5) & 7;
        if (width == 0 || order > 4)
        { error = "golpe " + std::to_string (s.index) + ": cabecera de bloque inválida"; return false; }
        const int* coef = kPredictors[order];
        const int own = framesPerBlock (blockWords, width, channels);
        const int64_t half = int64_t (1) << (width - 1), full = int64_t (1) << width;
        uint32_t bit = 0;
        for (int n = 0; n < own; ++n)
        {
            if (t >= s.frames) { error = "golpe " + std::to_string (s.index) + ": más tramas que el directorio"; return false; }
            for (int c = 0; c < channels; ++c)
            {
                // `width` bits, del menos significativo al más, a través de palabras de 16 bits.
                int64_t v = 0;
                for (int got = 0; got < width;)
                {
                    const uint16_t word = be16 (at + 2 + (bit >> 4) * 2);
                    const int offs = int (bit & 15);
                    const int take = std::min (16 - offs, width - got);
                    v |= int64_t ((word >> offs) & ((1 << take) - 1)) << got;
                    got += take;
                    bit += uint32_t (take);
                }
                const int64_t r = v >= half ? v - full : v;
                auto& h = hist[size_t (c)];
                int64_t x = r;
                for (int k = 0; k < order; ++k) x += int64_t (coef[k]) * h[size_t (k)];
                h[3] = h[2]; h[2] = h[1]; h[1] = h[0]; h[0] = int32_t (x);
                out[size_t (t) * size_t (channels) + size_t (c)] = int32_t (x);
            }
            ++t;
        }
    }
    if (t != s.frames)
    { error = "golpe " + std::to_string (s.index) + ": " + std::to_string (t) + " tramas, el directorio dice " + std::to_string (s.frames); return false; }
    return true;
}
} // namespace bk::npno
