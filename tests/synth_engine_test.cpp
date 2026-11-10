// Motor del synth: cada capa de los presets de fábrica (.ns4y) suena, sin NaN ni saturación; afinación de
// una sierra pura; demos en WAV.
//   tests/run_synth_tests.sh "<presets .ns4y>" "<samples>"   (también compila y ejecuta esto)
#include "../Source/core/SynthEngine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace bk::synth;
namespace fs = std::filesystem;
constexpr double SR = 48000;

static void render (SynthEngine& e, std::vector<float>& L, std::vector<float>& R, int frames)
{
    const size_t at = L.size();
    L.resize (at + size_t (frames)); R.resize (at + size_t (frames));
    for (int i = 0; i < frames; i += 256) e.render (L.data() + at + i, R.data() + at + i, std::min (256, frames - i));
}

static void wav (const std::string& p, const std::vector<float>& L, const std::vector<float>& R)
{
    std::ofstream f (p, std::ios::binary);
    auto u32 = [&] (uint32_t v) { f.write ((const char*) &v, 4); };
    auto u16 = [&] (uint16_t v) { f.write ((const char*) &v, 2); };
    const uint32_t n = uint32_t (L.size());
    f.write ("RIFF", 4); u32 (36 + n * 4); f.write ("WAVEfmt ", 8); u32 (16); u16 (1); u16 (2); u32 (48000); u32 (192000); u16 (4); u16 (16); f.write ("data", 4); u32 (n * 4);
    for (uint32_t i = 0; i < n; ++i) for (float s : { L[i], R[i] }) u16 (uint16_t (int16_t (std::lround (std::clamp (s, -1.0f, 1.0f) * 32767))));
}

int main (int argc, char** argv)
{
    int failures = 0;
    // Afinación: sierra pura, filtro abierto.
    {
        SynthEngine e; e.prepare (SR);
        SynthParams p; p.category = 0; p.wave = 2; p.filterOn = false; p.ampD = 127;
        e.setParams (p);
        e.noteOn (69, 100);
        std::vector<float> L, R; render (e, L, R, int (SR));
        double best = 0, bf = 440;
        for (double c = -30; c <= 30; c += 0.2)
        {
            const double f = 440 * std::pow (2.0, c / 1200);
            double re = 0, im = 0;
            for (size_t i = 9600; i < 9600 + 24000; ++i) { const double w = 0.5 - 0.5 * std::cos (2 * M_PI * (i - 9600) / 23999.0); re += w * L[i] * std::cos (2 * M_PI * f * i / SR); im -= w * L[i] * std::sin (2 * M_PI * f * i / SR); }
            if (std::hypot (re, im) > best) { best = std::hypot (re, im); bf = f; }
        }
        const bool ok = std::abs (bf - 440) < 0.3;
        failures += ! ok;
        std::printf ("%s afinación: sierra pura en La4 a %.2f Hz\n", ok ? "ok:" : "FALLO:", bf);
    }
    // Samples del usuario por id (hash del nombre).
    std::map<uint32_t, std::string> samples;
    if (argc > 2)
        for (const auto& e : fs::recursive_directory_iterator (argv[2]))
            if (e.path().extension() == ".nsmp4" || e.path().extension() == ".nsmp3") samples[soundId (e.path().stem().string())] = e.path().string();
    std::map<uint32_t, std::unique_ptr<bk::nsmp::SampleSound>> loaded;

    int layers = 0, silent = 0, nans = 0, hot = 0;
    std::vector<std::string> silentNames;
    std::vector<float> demoL, demoR;
    int demos = 0;
    for (const auto& e : fs::recursive_directory_iterator (argv[1]))
    {
        if (e.path().extension() != ".ns4y") continue;
        std::ifstream f (e.path(), std::ios::binary);
        std::vector<uint8_t> bytes ((std::istreambuf_iterator<char> (f)), {});
        std::string err;
        auto preset = parseNs4y (bytes.data(), bytes.size(), err);
        if (! preset) continue;
        for (size_t li = 0; li < preset->layers.size(); ++li)
        {
            const auto& l = preset->layers[li];
            if (! l.on()) continue;
            ++layers;
            SynthEngine eng; eng.prepare (SR);
            const auto p = paramsFromLayer (l);
            if (p.source == Source::Sample)
            {
                const auto it = samples.find (l.sampleId());
                if (it == samples.end()) continue;
                auto& s = loaded[l.sampleId()];
                if (! s) { std::string e2; s = bk::nsmp::loadSample (it->second, e2); }
                eng.setSample (s.get());
            }
            eng.setParams (p);
            std::vector<float> L, R;
            for (int k : { 48, 55, 60, 64 }) eng.noteOn (k, 100);
            render (eng, L, R, int (SR * 1.2));
            for (int k : { 48, 55, 60, 64 }) eng.noteOff (k);
            render (eng, L, R, int (SR * 0.6));
            float peak = 0; bool nan = false;
            for (size_t i = 0; i < L.size(); ++i) { peak = std::max ({ peak, std::abs (L[i]), std::abs (R[i]) }); nan = nan || ! std::isfinite (L[i]) || ! std::isfinite (R[i]); }
            if (nan) ++nans;
            const std::string label = e.path().stem().string() + " " + char ('A' + li);
            if (peak < 5.0e-4f) { ++silent; silentNames.push_back (label + " (pico " + std::to_string (peak) + ")"); }
            if (peak > 1.5f) { ++hot; std::printf ("  fuerte: %s pico %.2f\n", label.c_str(), peak); }
            // Unas cuantas a la demo.
            if (demos < 10 && layers % 37 == 0 && ! nan && peak > 0.01f)
            {
                ++demos;
                const float g = 0.8f / std::max (peak, 0.3f);
                for (size_t i = 0; i < L.size(); ++i) { demoL.push_back (L[i] * g); demoR.push_back (R[i] * g); }
                std::printf ("  demo %d: %s\n", demos, e.path().filename().c_str());
            }
        }
    }
    failures += nans; // "fuertes" es antes del limitador de salida (softLimit): informativo
    std::printf ("%d capas tocadas: %d con NaN, %d mudas, %d por encima de 1,5 antes del limitador\n", layers, nans, silent, hot);
    for (size_t i = 0; i < std::min<size_t> (silentNames.size(), 8); ++i) std::printf ("  muda: %s\n", silentNames[i].c_str());
    wav ("build/tests/synth_demo.wav", demoL, demoR);
    std::printf ("Demo: build/tests/synth_demo.wav (%.0f s)\n", demoL.size() / SR);
    return failures ? 1 : 0;
}
