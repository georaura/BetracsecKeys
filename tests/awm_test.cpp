// Motor AWM2: carga las muestras de cada performance con ondas de la librería, toca un acorde y comprueba
// que suena sin NaN; deja una demo con las performances que suenan completas.
//   tests/run_fmx_tests.sh "<librería .X7L>"   (también compila y ejecuta esto)
#include "../Source/core/AwmEngine.h"
#include "../Source/core/FmxEngine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

using namespace bk;
constexpr double SR = 48000;

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
    if (argc < 2) return 2;
    int failures = 0;
    std::string err;
    const auto lib = ysfc::readLibrary (argv[1], err);
    if (! lib) { std::printf ("FALLO: %s\n", err.c_str()); return 1; }
    int readable = 0;
    for (const auto& w : lib->waves) readable += w.readable();
    std::printf ("%s ondas de la librería: %zu, %d legibles (PCM)\n", readable > 0 ? "ok:" : "FALLO:", lib->waves.size(), readable);
    failures += readable == 0;

    int played = 0, complete = 0, silent = 0, nans = 0;
    double loadMs = 0, maxMb = 0;
    std::vector<float> demoL, demoR;
    for (const auto& p : lib->performances)
    {
        const auto t0 = std::chrono::steady_clock::now();
        auto samples = awm::loadSamples (lib->path, p, lib->waves);
        if (samples == nullptr) continue;
        loadMs += std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
        maxMb = std::max (maxMb, samples->bytes / 1.0e6);
        ++played;
        fmx::FmxEngine::prepareTables (p);
        fmx::FmxEngine fm;
        awm::AwmEngine awm;
        fm.prepare (SR);
        awm.prepare (SR);
        fm.setPerformance (&p);
        awm.setPerformance (&p, samples.get());
        std::vector<float> L, R;
        auto run = [&] (int frames)
        {
            const size_t at = L.size();
            L.resize (at + size_t (frames)); R.resize (at + size_t (frames));
            for (int i = 0; i < frames; i += 256)
            {
                const int n = std::min (256, frames - i);
                fm.render (L.data() + at + i, R.data() + at + i, n);
                awm.render (L.data() + at + i, R.data() + at + i, n);
            }
        };
        const bool demo = p.complete() && p.fm.empty();
        if (demo) for (int k : { 48, 55, 60, 64, 67, 72 }) { fm.noteOn (k, 96); awm.noteOn (k, 96); run (int (SR * 0.22)); fm.noteOff (k); awm.noteOff (k); }
        for (int k : { 48, 55, 60, 64 }) { fm.noteOn (k, 100); awm.noteOn (k, 100); }
        run (int (SR * 1.5));
        for (int k : { 48, 55, 60, 64 }) { fm.noteOff (k); awm.noteOff (k); }
        run (int (SR * 1.0));
        float peak = 0;
        bool nan = false;
        for (size_t i = 0; i < L.size(); ++i) { peak = std::max ({ peak, std::abs (L[i]), std::abs (R[i]) }); nan = nan || ! std::isfinite (L[i]) || ! std::isfinite (R[i]); }
        nans += nan;
        if (peak < 1.0e-3f) { ++silent; std::printf ("  muda: %s\n", p.name.c_str()); }
        if (p.complete()) ++complete;
        if (demo)
        {
            const float g = 0.5f / std::max (peak, 0.05f);
            for (size_t i = 0; i < L.size(); ++i) { demoL.push_back (L[i] * g); demoR.push_back (R[i] * g); }
            std::printf ("  demo: %s (pico %.2f, %.1f MB de muestras)\n", p.name.c_str(), peak, samples->bytes / 1.0e6);
        }
    }
    failures += nans;
    std::printf ("%d performances con ondas de la librería (%d completas): %d con NaN, %d mudas; carga media %.0f ms, máximo %.1f MB\n",
                 played, complete, nans, silent, played > 0 ? loadMs / played : 0.0, maxMb);
    wav ("build/tests/awm_demo.wav", demoL, demoR);
    std::printf ("Demo: build/tests/awm_demo.wav (%.0f s)\n", demoL.size() / SR);
    return failures ? 1 : 0;
}
