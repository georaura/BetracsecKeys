#include "Ysfc.h"

#include "FileUtf8.h"
#include <algorithm>
#include <cstring>
#include <fstream>

namespace bk::ysfc
{
namespace
{
uint32_t be32 (const uint8_t* p) { return (uint32_t (p[0]) << 24) | (uint32_t (p[1]) << 16) | (uint32_t (p[2]) << 8) | p[3]; }

/** Lector de bloques "longitud big-endian + datos" sobre un buffer; marca error si se sale. */
struct Reader
{
    const uint8_t* d;
    size_t n, pos = 0;
    bool bad = false;
    uint32_t u32()
    {
        if (pos + 4 > n) { bad = true; return 0; }
        const auto v = be32 (d + pos);
        pos += 4;
        return v;
    }
    std::pair<const uint8_t*, size_t> block()
    {
        const auto len = u32();
        if (bad || pos + len > n) { bad = true; return { nullptr, 0 }; }
        const auto* p = d + pos;
        pos += len;
        return { p, len };
    }
};

std::string cString (const uint8_t* p, size_t max)
{
    size_t len = 0;
    while (len < max && p[len] != 0) ++len;
    std::string s (reinterpret_cast<const char*> (p), len);
    while (! s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

FmOp parseOp (const uint8_t* b, size_t len)
{
    FmOp o;
    if (len < 33) return o;
    o.keyOnReset = b[0] != 0;
    o.fixed = b[1] != 0;
    o.coarse = b[2];
    o.fine = b[3];
    o.detune = b[4];
    o.pitchKeyFollow = b[5];
    o.pitchVelocity = b[6];
    o.form = b[7];
    o.skirt = b[8];
    o.resonance = b[9];
    o.pegInit = b[10];
    o.pegAttack = b[11];
    o.pegAttackTime = b[12];
    o.pegDecayTime = b[13];
    o.hold = b[14];
    for (int i = 0; i < 4; ++i) { o.times[size_t (i)] = b[15 + i]; o.levels[size_t (i)] = b[19 + i]; }
    o.timeKeyFollow = b[23];
    o.level = b[24];
    o.breakPoint = b[25];
    o.depthLo = b[26];
    o.depthHi = b[27];
    o.curveLo = b[28] & 3;
    o.curveHi = b[29] & 3;
    o.levelVelocity = b[30];
    o.pitchModSens = b[31];
    o.ampModSens = b[32];
    return o;
}

void parseHeader (PartInfo& part, const std::pair<const uint8_t*, size_t>& h)
{
    if (h.second < 41) return;
    const auto* p = h.first;
    part.name = cString (p, 21);
    part.partOn = p[24] != 0;
    part.keyboard = p[25] != 0;
    part.velLo = p[26]; part.velHi = p[27];
    part.noteLo = p[28]; part.noteHi = p[29];
    part.bendUp = int (p[30]) - 64; part.bendDown = int (p[31]) - 64;
    part.volume = p[34]; part.pan = p[35];
    part.noteShift = int (p[40]) - 64;
}

AwmElement parseElement (const uint8_t* e, size_t n)
{
    AwmElement x;
    if (n < 146) return x;
    x.on = e[0] != 0;
    x.bank = e[1];
    x.pan = e[6];
    x.noteLo = e[11]; x.noteHi = e[12]; x.velLo = e[13]; x.velHi = e[14];
    x.level = e[40]; x.levelVelSens = e[41]; x.levelVelOffset = e[42];
    x.aegTime = { e[44], e[45], e[46], e[48] };       // e[47]: tiempo de half damper
    x.aegLevel = { e[49], e[50], e[51], e[52] };
    x.aegTimeKeyFollow = e[55]; x.aegKeyCentre = e[56];
    x.coarse = e[67]; x.fine = e[68]; x.pitchKeyFollow = e[71]; x.pitchKeyCentre = e[72];
    x.filterType = e[91]; x.cutoff = e[92]; x.cutoffVelSens = e[93]; x.resonance = e[94]; x.hpfCutoff = e[96]; x.distance = e[97];
    for (int i = 0; i < 5; ++i) { x.fegTime[size_t (i)] = e[99 + i]; x.fegLevel[size_t (i)] = e[104 + i]; }
    x.fegDepth = e[109]; x.fegLevelVelSens = e[112]; x.cutoffKeyFollow = e[124];
    x.lfoWave = e[132]; x.lfoSync = e[133]; x.lfoDelay = e[134]; x.lfoSpeed = e[135];
    x.lfoAmp = e[136]; x.lfoPitch = e[137]; x.lfoFilter = e[138]; x.lfoFadeIn = e[139];
    x.wave = e[n - 2] | (e[n - 1] << 8);
    return x;
}

/** Una performance del bloque DPFM (ConvertWithMoss, YamahaYsfcPerformance.read). */
std::optional<Performance> parsePerformance (const uint8_t* d, size_t n)
{
    Reader r { d, n };
    Performance perf;
    const auto common = r.block();
    if (r.bad || common.second < 21) return std::nullopt;
    perf.name = cString (common.first, 21);
    for (int i = 0; i < 4; ++i) r.block();          // reverb, variación, EQ y efecto maestros
    const auto parts = r.u32();
    if (r.bad || parts > 64) return std::nullopt;
    std::vector<std::pair<const uint8_t*, size_t>> headers;
    for (uint32_t i = 0; i < parts; ++i) headers.push_back (r.block());
    r.block(); r.block();                            // partes A/D y entrada digital
    for (uint32_t i = 0; i < parts && ! r.bad; ++i)
    {
        const auto type = r.u32();
        if (type == 0 || type == 1)
        {
            const auto elements = r.u32();
            if (r.bad || elements > 256) return std::nullopt;
            AwmPart part;
            parseHeader (part, headers[i]);
            for (uint32_t e = 0; e < elements; ++e)
            {
                const auto b = r.block();
                if (type == 0 && b.first != nullptr) part.elements.push_back (parseElement (b.first, b.second));
            }
            if (type == 1) { if (part.partOn && part.volume > 0) perf.drumParts++; }
            else if (part.partOn && part.volume > 0) perf.awm.push_back (std::move (part));
        }
        else if (type == 2)
        {
            const auto ops = r.u32();
            if (r.bad || ops > 16) return std::nullopt;
            const auto fmCommon = r.block();
            FmPart part;
            parseHeader (part, headers[i]);
            if (fmCommon.second >= 67)
            {
                const auto* c = fmCommon.first;
                part.lfoWave = c[57]; part.lfoSpeed = c[58]; part.lfoDelay = c[60]; part.lfoSync = c[61];
                part.lfoPmd = c[62]; part.lfoAmd = c[63];
                part.algorithm = std::min<int> (c[65], 87);
                part.feedback = std::min<int> (c[66], 7);
            }
            for (uint32_t k = 0; k < ops; ++k)
            {
                const auto op = r.block();
                if (k < 8) part.ops[k] = parseOp (op.first, op.second);
            }
            perf.fm.push_back (part);
        }
        else return std::nullopt;
    }
    if (r.bad) return std::nullopt;
    return perf;
}
} // namespace

bool Waveform::readable() const
{
    if (keybanks.empty()) return false;
    for (const auto& k : keybanks) if (k.format != 5 && k.format != 0) return false;
    return true;
}

const Waveform* Library::waveFor (const AwmElement& e) const
{
    if (e.bank != 1 || e.wave < 1 || size_t (e.wave) > waves.size()) return nullptr;
    const auto& w = waves[size_t (e.wave - 1)];
    return w.readable() ? &w : nullptr;
}

namespace
{
uint32_t le32 (const uint8_t* p) { return p[0] | (uint32_t (p[1]) << 8) | (uint32_t (p[2]) << 16) | (uint32_t (p[3]) << 24); }

/** Ondas: nombres (EWFM), keybanks (DWFM, 64 bytes cada uno) y dónde están sus muestras (DWIM). */
void readWaves (std::ifstream& f, uint32_t ewfm, uint32_t dwfm, uint32_t dwim, Library& lib)
{
    auto chunk = [&f] (uint32_t off, std::vector<uint8_t>& out) -> bool
    {
        uint8_t h[8];
        f.clear();
        f.seekg (std::streamoff (off));
        f.read (reinterpret_cast<char*> (h), 8);
        if (! f) return false;
        const auto len = be32 (h + 4);
        if (len > (64u << 20)) return false;
        out.resize (len);
        f.read (reinterpret_cast<char*> (out.data()), std::streamsize (len));
        return size_t (f.gcount()) == len;
    };
    std::vector<uint8_t> names, meta;
    if (! chunk (dwfm, meta)) return;
    if (ewfm != 0) chunk (ewfm, names);
    // Nombres en el orden de las entradas ("Entr" … "n:Nombre\0").
    std::vector<std::string> nameList;
    for (size_t i = 0; i + 4 < names.size(); ++i)
        if (std::memcmp (names.data() + i, "Entr", 4) == 0)
        {
            const auto len = be32 (names.data() + i + 4);
            const auto* e = names.data() + i + 8;
            const uint8_t* end = std::min<const uint8_t*> (names.data() + names.size(), e + len);
            const auto* colon = std::find (e, end, uint8_t (':'));
            nameList.push_back (colon < end ? cString (colon + 1, size_t (end - colon - 1)) : std::string());
            i += 7 + len;
        }
    size_t o = 4;
    while (o + 8 <= meta.size() && std::memcmp (meta.data() + o, "Data", 4) == 0)
    {
        const auto n = be32 (meta.data() + o + 4);
        if (o + 8 + n > meta.size()) break;
        const auto* d = meta.data() + o + 8;
        Waveform w;
        if (lib.waves.size() < nameList.size()) w.name = nameList[lib.waves.size()];
        const size_t count = n >= 2 ? size_t (d[0] | (d[1] << 8)) : 0;
        for (size_t k = 0; k < count && 4 + (k + 1) * 64 <= n; ++k)
        {
            const auto* b = d + 4 + k * 64;
            Keybank kb;
            kb.keyLo = b[0]; kb.keyHi = b[1]; kb.velLo = b[2]; kb.velHi = b[3];
            kb.level = b[4]; kb.pan = b[5] & 0x7F; kb.root = b[8]; kb.coarse = b[9]; kb.fine = b[10];
            kb.format = b[14]; kb.loopMode = b[15];
            kb.rate = le32 (b + 36); kb.playStart = le32 (b + 40);
            kb.loopStart = le32 (b + 44) * 16 + b[18]; kb.loopEnd = le32 (b + 48);
            kb.length = le32 (b + 60);
            w.keybanks.push_back (kb);
        }
        lib.waves.push_back (std::move (w));
        o += 8 + n;
    }
    // Posición de cada trozo de muestra en DWIM (sin leerlo): un "Data" por onda con n × (tamaño + datos).
    f.clear();
    f.seekg (std::streamoff (dwim) + 12);
    for (auto& w : lib.waves)
    {
        uint8_t h[12];
        f.read (reinterpret_cast<char*> (h), 12);
        if (! f || std::memcmp (h, "Data", 4) != 0) { w.keybanks.clear(); break; }
        const auto len = be32 (h + 4), items = be32 (h + 8);
        const auto start = uint64_t (f.tellg()) - 4;
        for (uint32_t k = 0; k < items; ++k)
        {
            uint8_t s[4];
            f.read (reinterpret_cast<char*> (s), 4);
            const auto size = be32 (s);
            if (k < w.keybanks.size())
            {
                w.keybanks[k].offset = uint64_t (f.tellg());
                w.keybanks[k].length = std::min<uint32_t> (w.keybanks[k].length, size / 2);
            }
            f.seekg (std::streamoff (size), std::ios::cur);
        }
        if (items != w.keybanks.size()) w.keybanks.clear();   // no casan: la onda no se usa
        f.seekg (std::streamoff (start + len));
    }
}
} // namespace

bool looksLikeYsfc (const uint8_t* d, size_t size)
{
    return size >= 0x50 && std::memcmp (d, "YAMAHA-YSFC", 11) == 0;
}

std::optional<Library> readLibrary (const std::string& path, std::string& error)
{
    auto f = bk::io::openRead (path);
    if (! f) { error = "No se puede abrir el archivo."; return std::nullopt; }
    uint8_t head[0x200] = {};
    f.read (reinterpret_cast<char*> (head), sizeof head);
    if (! looksLikeYsfc (head, size_t (f.gcount()))) { error = "No es una librería FM compatible (.X7L, .X8L)."; return std::nullopt; }
    Library lib;
    lib.version = cString (head + 16, 16);
    if (lib.version.rfind ("4.", 0) != 0 && lib.version.rfind ("5.", 0) != 0)
    {
        error = "Versión YSFC " + lib.version + " no soportada.";
        return std::nullopt;
    }
    uint32_t dpfm = 0, ewfm = 0, dwfm = 0, dwim = 0;
    for (size_t i = 0x40; i + 8 <= sizeof head; i += 8)
    {
        if (std::memcmp (head + i, "DPFM", 4) == 0) dpfm = be32 (head + i + 4);
        if (std::memcmp (head + i, "EWFM", 4) == 0) ewfm = be32 (head + i + 4);
        if (std::memcmp (head + i, "DWFM", 4) == 0) dwfm = be32 (head + i + 4);
        if (std::memcmp (head + i, "DWIM", 4) == 0) dwim = be32 (head + i + 4);
    }
    lib.path = path;
    if (dpfm == 0) { error = "La librería no tiene performances."; return std::nullopt; }
    uint8_t ch[8];
    f.seekg (std::streamoff (dpfm));
    f.read (reinterpret_cast<char*> (ch), 8);
    if (! f || std::memcmp (ch, "DPFM", 4) != 0) { error = "Bloque de performances dañado."; return std::nullopt; }
    const auto len = be32 (ch + 4);
    if (len > (64u << 20)) { error = "Bloque de performances demasiado grande."; return std::nullopt; }
    std::vector<uint8_t> body (len);
    f.read (reinterpret_cast<char*> (body.data()), std::streamsize (len));
    if (size_t (f.gcount()) != len) { error = "Archivo incompleto."; return std::nullopt; }
    size_t o = 4;
    while (o + 8 <= body.size() && std::memcmp (body.data() + o, "Data", 4) == 0)
    {
        const auto n = be32 (body.data() + o + 4);
        if (o + 8 + n > body.size()) break;
        if (auto p = parsePerformance (body.data() + o + 8, n)) lib.performances.push_back (std::move (*p));
        o += 8 + n;
    }
    if (lib.performances.empty()) { error = "No se pudo leer ninguna performance."; return std::nullopt; }
    if (dwfm != 0 && dwim != 0) readWaves (f, ewfm, dwfm, dwim, lib);
    // Elementos que no van a sonar: ondas de la ROM del instrumento o ilegibles (WXC).
    for (auto& p : lib.performances)
        for (const auto& part : p.awm)
            for (const auto& e : part.elements)
                if (e.on && lib.waveFor (e) == nullptr) ++p.romElements;
    return lib;
}
} // namespace bk::ysfc
