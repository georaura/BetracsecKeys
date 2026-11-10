// BeTracks Keys — Nord Samples. Port de OpenNord (AGPL-3.0); ver NordSample.h.
#include "NordSample.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace bk::nsmp
{
namespace
{
constexpr uint32_t kNsmpMagic = 0x4e534d50; // "NSMP"
constexpr int64_t kBound = int64_t (1) << 26;
constexpr double kEnvelopeRate = 21700;
constexpr int kPredictor[8][7] = {
    { 0 }, { 1 }, { 2, -1 }, { 3, -3, 1 }, { 4, -6, 4, -1 }, { 5, -10, 10, -5, 1 },
    { 6, -15, 20, -15, 6, -1 }, { 7, -21, 35, -35, 21, -7, 1 },
};

uint32_t u32be (const uint8_t* b, size_t o) { return (uint32_t (b[o]) << 24) | (uint32_t (b[o + 1]) << 16) | (uint32_t (b[o + 2]) << 8) | b[o + 3]; }
uint32_t u24be (const uint8_t* b, size_t o) { return (uint32_t (b[o]) << 16) | (uint32_t (b[o + 1]) << 8) | b[o + 2]; }
uint32_t u16be (const uint8_t* b, size_t o) { return (uint32_t (b[o]) << 8) | b[o + 1]; }

struct Section
{
    std::string tag;
    uint32_t version = 0;
    size_t payload = 0, end = 0;
    bool is (const char* suffix) const { return tag.size() >= 3 && tag.compare (tag.size() - 3, 3, suffix) == 0; }
};

/** Inicio del árbol de secciones (codec 3/4): "NSMP" en 0x2c o, en exportaciones antiguas, 0x18. */
size_t bodyStart (const uint8_t* b, size_t size)
{
    if (size >= 0x30 && u32be (b, 0x2c) == kNsmpMagic) return 0x2c;
    if (size >= 0x1c && u32be (b, 0x18) == kNsmpMagic) return 0x18;
    return 0;
}

std::vector<Section> sections (const uint8_t* b, size_t size)
{
    std::vector<Section> out;
    size_t o = bodyStart (b, size);
    if (o == 0) return out;
    while (o + 12 <= size)
    {
        const uint32_t tag = u32be (b, o), version = u32be (b, o + 4), len = u32be (b, o + 8);
        const size_t payload = o + 12;
        if (payload + len > size) break;
        Section s;
        for (int i = 3; i >= 0; --i)
        {
            const char c = char ((tag >> (i * 8)) & 0xff);
            s.tag += (c >= 0x20 && c < 0x7f) ? c : '.';
        }
        s.version = version;
        s.payload = payload;
        s.end = payload + len;
        out.push_back (s);
        o = s.end;
    }
    return out;
}

std::string readName (const uint8_t* b, const Section& hdr)
{
    std::string run;
    for (size_t i = hdr.payload; i < hdr.end; ++i)
    {
        const uint8_t c = b[i];
        if (c >= 0x20 && c < 0x7f) run += char (c);
        else if (run.size() >= 2) break;
        else run.clear();
    }
    const auto a = run.find_first_not_of (' '), z = run.find_last_not_of (' ');
    return a == std::string::npos ? std::string() : run.substr (a, z - a + 1);
}

// ── Zonas ───────────────────────────────────────────────────────────────────────────────────
Zone record16 (const uint8_t* b, size_t o, uint32_t id)
{
    Zone z;
    z.root = b[o]; z.high = b[o + 1]; z.low = b[o + 2];
    z.oneShot = b[o + 5] != 0;
    z.strokeId = id;
    z.velLow = b[o + 14]; z.velHigh = b[o + 15];
    return z;
}

/** Tabla de zonas de codec 4 (`map` versión ≥ 21): parseCodec4ZoneRecords. */
std::vector<Zone> codec4Zones (const uint8_t* b, const Section& map, const std::set<uint32_t>& ids)
{
    constexpr size_t rec = 16, perNote = 6 + 128 * 10, header = 32;
    auto marker = [&] (size_t o) { return (b[o + 12] == 0 && b[o + 13] == 1 && b[o + 14] == 0) || b[o + 7] == 1; };
    auto idAt = [&] (size_t o) -> uint32_t
    {
        const uint32_t full = u32be (b, o + 8);
        return ids.count (full) || ! ids.count (b[o + 11]) ? full : b[o + 11];
    };
    auto valid = [&] (size_t o)
    {
        return o + rec <= map.end && b[o] <= 127 && b[o + 1] <= 127 && b[o + 2] <= 127 && ids.count (idAt (o)) && marker (o);
    };
    std::vector<Zone> zones;
    const size_t start = map.payload + perNote + header;
    if (start <= map.end)
    {
        const int count = b[start - 1];
        const long trailer = long (map.end) - long (start + size_t (count) * rec);
        if (count >= 1 && trailer >= 0 && trailer <= 8 && valid (start))
        {
            for (int i = 0; i < count; ++i) zones.push_back (record16 (b, start + size_t (i) * rec, idAt (start + size_t (i) * rec)));
            return zones;
        }
        // Zonas con varias capas: cabecera de 8 bytes y luego [id:u32][fuerza:u16][velMin][velMax] por golpe.
        size_t o = start;
        bool layered = false, ok = count >= 1;
        std::vector<Zone> walk;
        for (int i = 0; ok && i < count; ++i)
        {
            if (o + 8 > map.end || b[o] > 127 || b[o + 1] > 127 || b[o + 2] > b[o] || b[o] > b[o + 1]) { ok = false; break; }
            const int n = int (u16be (b, o + 6));
            if (n < 1 || n > 64 || o + 8 + size_t (n) * 8 > map.end) { ok = false; break; }
            if (n > 1) layered = true;
            for (int j = 0; j < n; ++j)
            {
                const size_t e = o + 8 + size_t (j) * 8;
                const uint32_t id = u32be (b, e);
                if (! ids.count (id)) { ok = false; break; }
                Zone z;
                z.root = b[o]; z.high = b[o + 1]; z.low = b[o + 2]; z.oneShot = b[o + 5] != 0;
                z.strokeId = id; z.velLow = b[e + 6]; z.velHigh = b[e + 7];
                walk.push_back (z);
            }
            o += 8 + size_t (n) * 8;
        }
        const long trailer2 = long (map.end) - long (o);
        if (ok && layered && trailer2 >= 0 && trailer2 <= 8) return walk;
    }
    // Último recurso: la serie más larga de registros válidos (empates hacia el final).
    size_t bestStart = 0, bestN = 0;
    long bestTrailer = -1;
    for (size_t p = map.payload + 6; p + rec <= map.end; ++p)
    {
        size_t n = 0;
        for (size_t o = p; valid (o); o += rec) ++n;
        if (n == 0) continue;
        const long trailer = long (map.end) - long (p + n * rec);
        if (n > bestN || (n == bestN && trailer < bestTrailer)) { bestStart = p; bestN = n; bestTrailer = trailer; }
    }
    for (size_t i = 0; i < bestN; ++i) zones.push_back (record16 (b, bestStart + i * rec, idAt (bestStart + i * rec)));
    return zones;
}

/** Codec 3 (`map` versión 13/14): [6B global][128 × 6B][1B nº de zonas][N × 16B]. */
std::vector<Zone> codec3Zones (const uint8_t* b, const Section& map)
{
    std::vector<Zone> zones;
    const size_t start = map.payload + 6 + 128 * 6 + 1;
    if (start > map.end) return zones;
    const int count = b[start - 1];
    for (int i = 0; i < count && start + size_t (i) * 16 + 16 <= map.end; ++i)
    {
        const size_t o = start + size_t (i) * 16;
        zones.push_back (record16 (b, o, u32be (b, o + 8)));
    }
    return zones;
}

// ── Códec NW1 (nsmp-codec.ts) ───────────────────────────────────────────────────────────────
struct Mark { uint32_t word; uint64_t sample; };
struct Decoded
{
    std::vector<std::vector<int32_t>> ch;
    size_t endOffset = 0;
    std::vector<Mark> marks;
    bool ok = true;
};

struct Channel
{
    int32_t ring[32] {};
    int head = 0;
    std::vector<int32_t>* out = nullptr;
    void emit (int64_t residual, int order)
    {
        int64_t pred = 0;
        for (int k = 0; k < order; ++k) pred += int64_t (kPredictor[order][k]) * ring[(head - 1 - k) & 31];
        const int32_t s = int32_t (residual + pred);
        ring[head] = s;
        head = (head + 1) & 31;
        out->push_back (s);
    }
};

inline int64_t signExtend (uint64_t v, int bw) { return v >= (uint64_t (1) << (bw - 1)) ? int64_t (v) - (int64_t (1) << bw) : int64_t (v); }

Decoded decodeStroke (const uint8_t* b, size_t size, size_t start, int channels, bool wordInterleaved)
{
    Decoded d;
    d.ch.assign (size_t (channels), {});
    std::vector<Channel> cs (static_cast<size_t> (channels));
    for (int c = 0; c < channels; ++c) cs[size_t (c)].out = &d.ch[size_t (c)];
    size_t o = start;
    uint64_t index = 0, cum = 0;
    while (o + 4 <= size)
    {
        const uint32_t w = u32be (b, o);
        const int count = int (w & 0x3fff), order = int ((w >> 14) & 0xf), bw = int ((w >> 19) & 0xf) + 1;
        const bool lin = (w >> 23) & 1;
        if (lin) d.marks.push_back ({ uint32_t ((o - start) / 4), wordInterleaved ? cum : index });
        o += 4;
        if (wordInterleaved)
        {
            // Codec 4: cada canal es su propio flujo de palabras de 32 bits, intercaladas palabra a palabra.
            if (count == 0) continue;            // marca de segmento
            if (order > 7) break;
            cum += uint64_t (count);
            const int perCh = count / channels;
            const int wordsPerCh = (perCh * bw + 31) / 32;
            if (o + size_t (wordsPerCh) * size_t (channels) * 4 > size) break;
            for (int c = 0; c < channels; ++c)
            {
                const int n = c == channels - 1 ? count - perCh * (channels - 1) : perCh;
                uint64_t acc = 0;
                int nbits = 0, produced = 0;
                for (int k = 0; k < wordsPerCh; ++k)
                {
                    acc |= uint64_t (u32be (b, o + (size_t (k) * size_t (channels) + size_t (c)) * 4)) << (32 - nbits);
                    nbits += 32;
                    while (nbits >= bw && produced < n)
                    {
                        cs[size_t (c)].emit (signExtend ((acc >> (64 - bw)) & ((uint64_t (1) << bw) - 1), bw), order);
                        ++produced;
                        acc <<= bw;
                        nbits -= bw;
                    }
                }
            }
            o += size_t (wordsPerCh) * size_t (channels) * 4;
            continue;
        }
        // Codec 3: un solo flujo con las muestras intercaladas, terminado en un bloque de parada.
        if (order == 0 && bw == 1) break;
        if (order > 7 || count == 0) { d.ok = false; break; }
        const int words = (count * bw + 31) / 32;
        if (o + size_t (words) * 4 > size) { d.ok = false; break; }
        uint64_t acc = 0;
        int nbits = 0, produced = 0;
        for (int k = 0; k < words; ++k)
        {
            acc |= uint64_t (u32be (b, o)) << (32 - nbits);
            o += 4;
            nbits += 32;
            while (nbits >= bw && produced < count)
            {
                cs[size_t (index % uint64_t (channels))].emit (signExtend ((acc >> (64 - bw)) & ((uint64_t (1) << bw) - 1), bw), order);
                ++produced;
                ++index;
                acc <<= bw;
                nbits -= bw;
            }
        }
    }
    d.endOffset = o;
    return d;
}

/** Busca el inicio del flujo de un golpe y lo decodifica (decodeStrokeSection, codec 3/4). */
bool decodeSection (const uint8_t* b, const Section& s, bool wordInterleaved, Decoded& out, int& channelsOut)
{
    const int hint = (b[s.payload + 8] == 1 || b[s.payload + 8] == 2) ? b[s.payload + 8] : 0;
    const int order[2] = { hint == 2 ? 2 : 1, hint == 2 ? 1 : 2 };
    for (size_t hdr = 0x60; hdr <= 0x180; hdr += 4)
    {
        const size_t start = s.payload + hdr;
        if (start >= s.end) break;
        for (int channels : order)
        {
            auto d = decodeStroke (b, s.end, start, channels, wordInterleaved);
            if (! d.ok || d.ch[0].size() <= 100) continue;
            int64_t peak = 0;
            for (auto x : d.ch[0]) peak = std::max<int64_t> (peak, x < 0 ? -int64_t (x) : x);
            if (d.endOffset + 8 >= s.end && peak < kBound) { out = std::move (d); channelsOut = channels; return true; }
        }
    }
    return false;
}

double loopDecayDb (uint32_t raw)
{
    const double c = std::abs (raw >= 0x800000 ? double (raw) - 0x1000000 : double (raw)) / 0x800000;
    if (! (c > 0) || c >= 1) return 0;
    return -20.0 * std::log10 (c) * kEnvelopeRate;
}
} // namespace

bool looksLikeNsmp (const uint8_t* b, size_t size)
{
    if (size < 0x30 || bodyStart (b, size) == 0) return false;
    const int codec = (b[0x14] | (b[0x15] << 8)) / 100;
    return codec == 3 || codec == 4 || bodyStart (b, size) == 0x18;
}

std::optional<Sample> readInfo (const uint8_t* b, size_t size, std::string& error)
{
    if (! looksLikeNsmp (b, size)) { error = "No es un sample compatible (.nsmp3/.nsmp4)."; return std::nullopt; }
    Sample s;
    s.codec = (b[0x14] | (b[0x15] << 8)) / 100;
    for (const auto& sec : sections (b, size))
        if (sec.is ("hdr")) { s.name = readName (b, sec); break; }
    return s;
}

std::optional<Sample> decode (const uint8_t* b, size_t size, std::string& error)
{
    auto info = readInfo (b, size, error);
    if (! info) return std::nullopt;
    Sample s = *info;
    const auto secs = sections (b, size);
    const Section* map = nullptr;
    std::set<uint32_t> ids;
    for (const auto& sec : secs)
    {
        if (sec.is ("map") && ! map) map = &sec;
        if (sec.is ("stk")) ids.insert (u32be (b, sec.payload));
    }
    if (! map) { error = "El sample no tiene mapa de zonas."; return std::nullopt; }
    if (map->version >= 21) s.zones = codec4Zones (b, *map, ids);
    else if (map->version == 13 || map->version == 14) s.zones = codec3Zones (b, *map);
    else { error = "Este formato de sample (map v" + std::to_string (map->version) + ") aún no se lee."; return std::nullopt; }

    const bool wordInterleaved = s.codec == 4;
    for (const auto& sec : secs)
    {
        if (! sec.is ("stk")) continue;
        Decoded d;
        int channels = 1;
        if (! decodeSection (b, sec, wordInterleaved, d, channels)) continue;
        Stroke st;
        st.id = u32be (b, sec.payload);
        st.channels = channels;
        const uint32_t rate = u16be (b, sec.payload + 6);
        st.sampleRate = rate >= 8000 && rate <= 96000 ? rate : 35002;
        st.frames = uint32_t (d.ch[0].size());
        st.pcm.resize (size_t (st.frames) * size_t (channels));
        for (uint32_t t = 0; t < st.frames; ++t)
            for (int c = 0; c < channels; ++c)
                st.pcm[size_t (t) * size_t (channels) + size_t (c)] = t < d.ch[size_t (c)].size() ? d.ch[size_t (c)][t] : 0;
        // Bucle: los punteros de la cabecera son posiciones del flujo comprimido; se buscan en las
        // marcas de segmento (strokeLoop), anclados en el final del flujo.
        const size_t p = sec.payload;
        const uint32_t r1 = u32be (b, p + 0x12), r2 = u32be (b, p + 0x1b), r3 = u32be (b, p + 0x24), r4 = u32be (b, p + 0x2d);
        st.loopStart = st.loopEnd = st.frames;
        if (r1 <= r2 && r2 <= r3 && r3 <= r4 && r4 > r1 && r4 - r1 <= (1u << 27) && ! d.marks.empty())
        {
            const bool loops = r3 != r4;
            const uint32_t loopBegin = r3 - r1, end = r4 - r1;
            auto frameAt = [&] (int64_t word) -> int64_t
            {
                for (const auto& m : d.marks)
                    if (int64_t (m.word) == word) return int64_t (m.sample / uint64_t (std::max (1, channels)));
                return -1;
            };
            const int64_t shifts[2] = { int64_t (d.marks.back().word) - int64_t (end), 0 };
            for (int64_t shift : shifts)
            {
                if (loops && frameAt (int64_t (loopBegin) + shift) < 0) continue;
                const int64_t ls = loops ? frameAt (int64_t (loopBegin) + shift) : st.frames;
                st.loopStart = uint32_t (ls);
                st.loops = loops && ls < int64_t (st.frames);
                st.loopDecayDbPerSecond = loops ? loopDecayDb (u24be (b, p + 0x28)) : 0;
                break;
            }
        }
        s.strokes.push_back (std::move (st));
    }
    if (s.strokes.empty()) { error = "No se pudo decodificar ningún golpe del sample."; return std::nullopt; }
    return s;
}
} // namespace bk::nsmp
