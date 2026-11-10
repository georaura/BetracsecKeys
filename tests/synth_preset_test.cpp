// Presets de synth (.ns4y): valores crudos de cada capa contra OpenNord (tools/inspect/synthgolden.ts).
//   tests/run_synth_tests.sh "<carpeta con .ns4y>"
#include "../Source/core/SynthPreset.h"

#include <cstdio>
#include <filesystem>
#include <set>
#include <fstream>
#include <iterator>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

using namespace bk::synth;

int main (int argc, char** argv)
{
    // Ids de los samples del usuario, calculados del nombre del archivo.
    std::set<uint32_t> localIds;
    if (argc > 2)
        for (const auto& e : std::filesystem::recursive_directory_iterator (argv[2]))
            if (e.path().extension() == ".nsmp4" || e.path().extension() == ".nsmp3") localIds.insert (soundId (e.path().stem().string()));
    int resolved = 0;
    // Nombre del campo en OpenNord → campo de Keys.
    std::map<std::string, Field> names;
    static const char* on[kFieldCount] = {
        "layer on/off", "volume", "octave shift", "samples/analog", "sample ID/name", "analog type (knob 1)", "analog cat (knob 2)",
        "analog wave/partial (knob 3)", "osc ctrl", "pitch fine", "pitch coarse", "osc env attack", "osc env decay", "osc env release",
        "osc env amt", "osc env-to-pitch on/off", "osc env velocity on/off", "LFO target", "LFO shape", "LFO rate/time", "LFO mod amt",
        "amp env attack", "amp env decay", "amp env release", "amp env velocity", "filter on/off", "filter type", "filter freq",
        "filter resonance / freq HP", "filter track", "filter drive", "filter env amt", "filter env attack", "filter env decay",
        "filter env release", "filter velocity on/off", "mono on/off", "legato on/off", "glide", "unison level", "vibrato mode",
        "vibrato rate", "vibrato amount", "susped on/off", "pan",
    };
    for (int f = 0; f < kFieldCount; ++f) names[on[f]] = Field (f);
    std::ifstream golden (argv[1]);
    std::string line;
    int ok = 0, bad = 0, sampleNamed = 0, samples = 0;
    const std::regex pair (R"re("([^"]+)":(-?\d+))re");
    while (std::getline (golden, line))
    {
        std::istringstream in (line);
        std::string file, letter, raw, show;
        std::getline (in, file, '\t'); std::getline (in, letter, '\t'); std::getline (in, raw, '\t'); std::getline (in, show);
        std::ifstream f (file, std::ios::binary);
        std::vector<uint8_t> bytes ((std::istreambuf_iterator<char> (f)), {});
        std::string err;
        auto p = parseNs4y (bytes.data(), bytes.size(), err);
        const auto& l = p->layers[size_t (letter[0] - 'A')];
        int diffs = 0;
        for (auto it = std::sregex_iterator (raw.begin(), raw.end(), pair); it != std::sregex_iterator(); ++it)
        {
            const auto n = names.find ((*it)[1]);
            if (n == names.end()) continue;
            const auto want = std::stoll ((*it)[2]);
            if (l[n->second] != want) { if (! diffs++) std::printf ("DISTINTO %s %s\n", file.c_str(), letter.c_str()); std::printf ("  %s: %lld ≠ %lld\n", (*it)[1].str().c_str(), want, (long long) l[n->second]); }
        }
        if (l.usesSample()) { ++samples; sampleNamed += factorySampleName (l.sampleId()) != nullptr; resolved += localIds.count (l.sampleId()) > 0; }
        if (diffs) ++bad; else ++ok;
    }
    std::printf ("%d capas iguales a OpenNord, %d distintas; %d de %d capas con sample tienen nombre en la tabla\n", ok, bad, sampleNamed, samples);
    if (argc > 2) std::printf ("%d de %d capas con sample encuentran su archivo por el hash del nombre\n", resolved, samples);
    // Comprobación del hash con un par conocido de la tabla de OpenNord.
    if (soundId ("Small Choir Fem ah Soft_CL 4.1") != 264740869u) { std::printf ("FALLO: soundId\n"); ++bad; }
    return bad ? 1 : 0;
}
