#include "PianoPreset.h"
#include "PianoNames.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <regex>

namespace bk::npno
{
const char* pianoLayerTypeName (int type)
{
    static const char* names[] = { "Grand", "Upright", "Electric", "Clav", "Digital", "Misc" };
    return type >= 0 && type < 6 ? names[type] : "Piano";
}

const char* pianoModelName (uint32_t id)
{
    const auto* begin = std::begin (kPianoNames);
    const auto* end = std::end (kPianoNames);
    const auto* it = std::lower_bound (begin, end, id, [] (const PianoName& n, uint32_t v) { return n.id < v; });
    return it != end && it->id == id ? it->name : nullptr;
}

namespace
{
int64_t field (const uint8_t* d, size_t size, int beg, int end)
{
    if (beg < 0 || size_t (end >> 3) >= size) return -1;
    int64_t v = 0;
    for (int bit = beg; bit <= end; ++bit) v = (v << 1) | ((d[bit >> 3] >> (7 - (bit & 7))) & 1);
    return v;
}

/** Capa A; la B está 96 bits más allá (salvo el on/off, que va justo antes). */
constexpr int kLayerB = 96;
constexpr int kOn = 394, kOnB = 393, kVolBeg = 398, kVolEnd = 404, kVolB = 31;
constexpr int kOctBeg = 500, kOctEnd = 503, kType = 506, kModelBeg = 516, kModelEnd = 547;
constexpr int kSoftRel = 548, kStringRes = 549, kPedalNoise = 550, kTouchBeg = 551, kTouchEnd = 552;
constexpr int kDynBeg = 555, kDynEnd = 556, kTimbreBeg = 558, kTimbreEnd = 560;

std::string lower (std::string s)
{
    for (auto& c : s) c = char (std::tolower ((unsigned char) c));
    return s;
}

/** Guiones bajos → espacios, espacios juntados, minúsculas, sin .npno (normalizePianoName). */
std::string normalize (std::string s)
{
    if (s.size() > 5 && lower (s.substr (s.size() - 5)) == ".npno") s.resize (s.size() - 5);
    std::string out;
    for (char c : s)
    {
        if (c == '_') c = ' ';
        if (std::isspace ((unsigned char) c)) { if (! out.empty() && out.back() != ' ') out += ' '; }
        else out += c;
    }
    while (! out.empty() && out.back() == ' ') out.pop_back();
    return lower (out);
}

/** "6.3" → 630, "6.10" → 610 (versionNumber). */
int versionNumber (const std::string& v)
{
    static const std::regex re (R"(^(\d{1,2})\.(\d{1,2})$)");
    std::smatch m;
    if (! std::regex_match (v, m, re)) return 0;
    const int minor = std::stoi (m[2]);
    return std::stoi (m[1]) * 100 + (m[2].length() == 1 ? minor * 10 : minor);
}

struct Key { std::string model; int version = 0; };

/** Nombre → modelo + versión: "White_Grand_XL_6.3" → { "white grand xl", 630 } (pianoKey). */
Key pianoKey (const std::string& name)
{
    const auto n = normalize (name);
    static const std::regex re (R"((^|[^\d.])(\d{1,2}\.\d{1,2})$)");
    std::smatch m;
    if (! std::regex_search (n, m, re)) return { n, 0 };
    const auto pos = size_t (m.position (2));
    std::string model = n.substr (0, pos);
    while (! model.empty() && model.back() == ' ') model.pop_back();
    if (model.empty()) return { n, 0 };
    return { model, versionNumber (m[2]) };
}

bool startsWithWord (const std::string& s, const std::string& prefix)
{
    return s == prefix || (s.size() > prefix.size() && s.compare (0, prefix.size(), prefix) == 0 && s[prefix.size()] == ' ');
}

/** ¿La tabla del panel tiene este nombre con un tamaño detrás ("wurlitzer 2" → "Wurlitzer 2 XL")? */
bool isSizedBase (const std::string& model)
{
    static const std::regex re (R"(^(.+) (xl|xm|lrg|med|sml)$)");
    for (const auto& n : kPianoNames)
    {
        std::smatch m;
        const auto nn = normalize (n.name);
        if (std::regex_match (nn, m, re) && m[1] == model) return true;
    }
    return false;
}

/** Los nombres con los que se puede reconocer un piano local (localPianoKeys). */
std::vector<Key> localKeys (const LocalPiano& p)
{
    const auto fromName = pianoKey (p.fileName);
    const int version = p.headerVersion ? p.headerVersion : fromName.version;
    std::vector<Key> keys { { fromName.model, version } };
    const auto head = normalize (p.headerName);
    if (head.empty()) return keys;
    const auto variant = normalize (p.headerVariant);
    // Las descargas de Nord nombran el instrumento grabado tras el nombre ("royal grand 3d yas6 xl").
    if (! variant.empty() && startsWithWord (fromName.model, head + " " + variant))
    {
        std::string rest = fromName.model.size() > head.size() + variant.size() + 1
                         ? fromName.model.substr (head.size() + variant.size() + 2) : std::string();
        keys.push_back ({ rest.empty() ? head : head + " " + rest, version });
    }
    // Un archivo renombrado: solo su cabecera dice qué es, salvo que el nombre venga en tamaños.
    if (! startsWithWord (fromName.model, head) && ! isSizedBase (head)) keys.push_back ({ head, p.headerVersion });
    return keys;
}
} // namespace

bool looksLikeNs4n (const uint8_t* d, size_t size)
{
    return size >= 12 && std::memcmp (d, "CBIN", 4) == 0 && std::memcmp (d + 8, "ns4n", 4) == 0;
}

std::optional<PianoPreset> parseNs4n (const uint8_t* d, size_t size, std::string& error)
{
    if (! looksLikeNs4n (d, size)) { error = "No es un preset de piano compatible (.ns4n)."; return std::nullopt; }
    PianoPreset preset;
    for (int L = 0; L < 2; ++L)
    {
        const int o = L * kLayerB;
        auto& l = preset.layers[size_t (L)];
        const auto on = field (d, size, L == 0 ? kOn : kOnB, L == 0 ? kOn : kOnB);
        const auto model = field (d, size, kModelBeg + o, kModelEnd + o);
        if (on < 0 || model < 0) { if (L == 0) { error = "El preset está incompleto."; return std::nullopt; } break; }
        l.on = on == 1;
        l.modelId = uint32_t (model);
        if (const char* n = pianoModelName (l.modelId)) l.modelName = n;
        l.pianoType = int (field (d, size, kType + o, kType + 2 + o));
        const auto touch = field (d, size, kTouchBeg + o, kTouchEnd + o); // 0 heavy, 1 med, 2 light
        l.touch = touch == 0 ? 2 : touch == 2 ? 0 : 1;
        l.softRelease = field (d, size, kSoftRel + o, kSoftRel + o) == 1;
        l.stringRes = field (d, size, kStringRes + o, kStringRes + o) == 1;
        l.pedalNoise = field (d, size, kPedalNoise + o, kPedalNoise + o) == 1;
        const auto vol = field (d, size, kVolBeg + L * kVolB, kVolEnd + L * kVolB);
        l.volumeDb = vol <= 0 ? -120.0f : float (40.0 * std::log10 (double (vol) / 127.0));
        const auto oct = field (d, size, kOctBeg + o, kOctEnd + o);
        l.octaveShift = oct >= 14 ? int (oct) - 16 : (oct >= 0 && oct <= 2 ? int (oct) : 0);
        l.timbre = int (field (d, size, kTimbreBeg + o, kTimbreEnd + o) & 3);
        l.dynComp = int (field (d, size, kDynBeg + o, kDynEnd + o));
    }
    if (! preset.layers[0].on && ! preset.layers[1].on) preset.layers[0].on = true;
    return preset;
}

int findPianoByName (const std::string& panelName, const std::vector<LocalPiano>& locals)
{
    const auto want = pianoKey (panelName);
    if (want.model.empty()) return -1;
    int best = -1, bestVersion = -1;
    std::string bestName;
    for (size_t i = 0; i < locals.size(); ++i)
        for (const auto& k : localKeys (locals[i]))
            if (k.model == want.model && (want.version == 0 || k.version == want.version))
            {
                // La versión más nueva; a igualdad, el nombre de archivo menor (como OpenNord).
                if (k.version > bestVersion || (k.version == bestVersion && locals[i].fileName < bestName))
                { best = int (i); bestVersion = k.version; bestName = locals[i].fileName; }
                break;
            }
    return best;
}
} // namespace bk::npno
