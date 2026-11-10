// Compara el lector de .ns4o con la referencia de OpenNord (tools/inspect/organgolden.ts).
//   tests/run_organ_tests.sh <carpeta con .ns4o>
#include "../Source/core/OrganPreset.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

using namespace bk::organ;

static std::string json (const OrganParams& p)
{
    static const char* models[] = { "B3", "VOX", "FARF", "PIPE1", "PIPE2", "B3 BASS" };
    auto b = [] (bool v) { return v ? "true" : "false"; };
    std::ostringstream s;
    s << "{\"model\":\"" << models[int (p.model)] << "\",\"drawbars\":[";
    for (int i = 0; i < 9; ++i) s << (i ? "," : "") << int (p.drawbars[size_t (i)]);
    char vol[32];
    const double v = std::round (p.volumeDb * 10.0) / 10.0;
    if (v == std::floor (v)) std::snprintf (vol, sizeof vol, "%d", int (v)); else std::snprintf (vol, sizeof vol, "%.1f", v);
    s << "],\"perc\":{\"on\":" << b (p.percussion.on) << ",\"harm3rd\":" << b (p.percussion.harm3rd)
      << ",\"decayFast\":" << b (p.percussion.decayFast) << ",\"volSoft\":" << b (p.percussion.volSoft)
      << "},\"vib\":" << b (p.vibOn) << ",\"volumeDb\":" << vol << ",\"octave\":" << p.octaveShift
      << ",\"sustain\":" << b (p.sustain) << "}";
    return s.str();
}

int main (int argc, char** argv)
{
    std::ifstream golden (argc > 1 ? argv[1] : "build/inspect/organ-golden.tsv");
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
        auto preset = parseNs4o (bytes.data(), bytes.size(), err);
        const int L = letter == "B" ? 1 : 0;
        const std::string got = preset ? json (preset->layers[size_t (L)]) : "null";
        if (got == want && preset && preset->layerOn[size_t (L)]) ++ok;
        else { ++bad; std::printf ("DISTINTO %s %s\n  OpenNord: %s\n  Keys:     %s\n", file.c_str(), letter.c_str(), want.c_str(), got.c_str()); }
    }
    std::printf ("%d presets iguales a OpenNord, %d distintos\n", ok, bad);
    return bad ? 1 : 0;
}
