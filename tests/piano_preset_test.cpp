// Presets de piano (.ns4n): lectura y piano elegido, contra OpenNord (tools/inspect/pianogolden.ts).
//   tests/run_piano_preset_tests.sh <presets .ns4n> <carpeta de pianos>
#include "../Source/core/Npno.h"
#include "../Source/core/PianoPreset.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

using namespace bk::npno;
namespace fs = std::filesystem;

/** Valor de una clave "k":"v" en la línea JSON de la referencia (todas son cadenas). */
static std::string jsonValue (const std::string& j, const std::string& key)
{
    const auto k = "\"" + key + "\":\"";
    const auto p = j.find (k);
    if (p == std::string::npos) return {};
    const auto e = j.find ('"', p + k.size());
    return j.substr (p + k.size(), e - p - k.size());
}

int main (int, char** argv)
{
    // Pianos locales: nombre de archivo + cabecera.
    std::vector<LocalPiano> locals;
    for (const auto& e : fs::recursive_directory_iterator (argv[2]))
    {
        if (e.path().extension() != ".npno") continue;
        std::ifstream f (e.path(), std::ios::binary);
        std::vector<uint8_t> head (0x200);
        f.read ((char*) head.data(), 0x200);
        LocalPiano p { e.path().string(), e.path().stem().string(), {}, {}, 0 };
        if (auto id = readIdentity (head.data(), size_t (f.gcount())))
        {
            p.headerName = id->name;
            p.headerVariant = id->variant;
            const int raw = head[0x14] | (head[0x15] << 8);
            p.headerVersion = raw;
        }
        locals.push_back (p);
    }

    std::ifstream golden (argv[1]);
    std::string line;
    int ok = 0, bad = 0;
    while (std::getline (golden, line))
    {
        std::istringstream in (line);
        std::string file, letter, want;
        std::getline (in, file, '\t'); std::getline (in, letter, '\t'); std::getline (in, want);
        std::ifstream f (file, std::ios::binary);
        std::vector<uint8_t> bytes ((std::istreambuf_iterator<char> (f)), {});
        std::string err;
        auto preset = parseNs4n (bytes.data(), bytes.size(), err);
        const auto& l = preset->layers[letter == "B" ? 1 : 0];
        const int hit = findPianoByName (l.modelName, locals);
        const std::string gotFile = hit >= 0 ? fs::path (locals[size_t (hit)].path).filename().string() : "";
        static const char* touches[] = { "light", "med", "heavy" };
        static const char* timbres[] = { "off", "SOFT", "MID", "BRIGHT" };
        char vol[32];
        if (l.volumeDb <= -119) std::snprintf (vol, sizeof vol, "volume off");
        else std::snprintf (vol, sizeof vol, "%.1f dB", std::round (l.volumeDb * 10) / 10 + 0.0);
        const std::string octave = l.octaveShift > 0 ? "+" + std::to_string (l.octaveShift) : std::to_string (l.octaveShift);
        std::vector<std::string> diffs;
        auto cmp = [&] (const char* key, const std::string& got) { if (jsonValue (want, key) != got) diffs.push_back (std::string (key) + ": " + jsonValue (want, key) + " ≠ " + got); };
        cmp ("piano", l.modelName);
        cmp ("file", gotFile);
        cmp ("touch", touches[l.touch]);
        cmp ("softRel", l.softRelease ? "on" : "off");
        cmp ("stringRes", l.stringRes ? "on" : "off");
        cmp ("volume", vol);
        cmp ("octave", octave);
        // El timbre no se compara: su texto depende del tipo de piano (Dyno en los EP, pastillas en el
        // Clavinet) y Keys no lo aplica.
        (void) timbres;
        cmp ("dynComp", std::to_string (l.dynComp));
        if (diffs.empty() && l.on) ++ok;
        else
        {
            ++bad;
            std::printf ("DISTINTO %s %s\n", fs::path (file).filename().string().c_str(), letter.c_str());
            for (auto& d : diffs) std::printf ("  %s\n", d.c_str());
        }
    }
    std::printf ("%d capas iguales a OpenNord (lectura y piano elegido), %d distintas\n", ok, bad);
    return bad ? 1 : 0;
}
