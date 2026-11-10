// Motor FM-X: lee una librería de MONTAGE (.X7L), toca cada performance con partes FM-X y comprueba que
// suena sin NaN ni saturación; deja una demo en WAV.
//   tests/run_fmx_tests.sh "<librería .X7L>"
#include "../Source/core/FmxEngine.h"
#include "../Source/core/FmxAlgorithms.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

using namespace bk;
constexpr double SR = 48000;

static void render (fmx::FmxEngine& e, std::vector<float>& L, std::vector<float>& R, int frames)
{
    const size_t at = L.size();
    L.resize (at + size_t (frames)); R.resize (at + size_t (frames));
    for (int i = 0; i < frames; i += 200) e.render (L.data() + at + i, R.data() + at + i, std::min (200, frames - i));
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

static float rms (const std::vector<float>& x, size_t a, size_t b)
{
    double s = 0;
    b = std::min (b, x.size());
    for (size_t i = a; i < b; ++i) s += double (x[i]) * x[i];
    return b > a ? float (std::sqrt (s / double (b - a))) : 0.0f;
}

int main (int argc, char** argv)
{
    if (argc < 2) { std::printf ("uso: fmx_test <librería .X7L>\n"); return 2; }
    int failures = 0;
    std::string err;
    const auto lib = ysfc::readLibrary (argv[1], err);
    if (! lib) { std::printf ("FALLO: %s\n", err.c_str()); return 1; }
    int fmPerf = 0, onlyFm = 0, fmParts = 0;
    for (const auto& p : lib->performances) { fmPerf += p.hasFm(); onlyFm += p.onlyFm(); fmParts += int (p.fm.size()); }
    std::printf ("YSFC %s: %zu performances, %d con FM-X (%d solo FM-X), %d partes FM-X\n", lib->version.c_str(), lib->performances.size(), fmPerf, onlyFm, fmParts);
    const bool countsOk = lib->performances.size() == 256 && onlyFm == 144 && fmParts == 213;
    std::printf ("%s recuento (256 / 144 / 213 en MONTAGE Expanded; solo FM-X sin contar partes de muestras a volumen 0)\n", countsOk ? "ok:" : "aviso:");

    // E-Piano 1 = E.PIANO 1 del DX7: algoritmo 13 (= DX7 5), feedback 6, OP2 ×14, detune ±7 en OP5/OP6.
    for (const auto& p : lib->performances)
        if (p.name == "E-Piano 1" && ! p.fm.empty())
        {
            const auto& f = p.fm[0];
            const bool ok = f.algorithm == 12 && f.feedback == 6 && f.ops[6].coarse == 14 && f.ops[7].coarse == 1
                            && f.ops[3].detune == 8 && f.ops[2].detune == 22;
            failures += ! ok;
            std::printf ("%s E-Piano 1: algoritmo %d, feedback %d, OP2 ×%d, detune OP5 %d OP6 %d\n", ok ? "ok:" : "FALLO:",
                         f.algorithm + 1, f.feedback, f.ops[6].coarse, f.ops[3].detune - 15, f.ops[2].detune - 15);
        }

    fmx::FmxEngine::buildTables();
    int played = 0, silent = 0, nans = 0, hot = 0;
    std::vector<float> demoL, demoR;
    const char* demoNames[] = { "E-Piano 1", "Brass 1", "Marimba", "Tubular Bells", "Bass 1", "Strings 1", "Dancing Operators", "Pure FM-X..perience1", "Harp 1", "B3 Slow", "Scary Toys" };
    float worstPeak = 0;
    std::string worstName;
    int hidden = 0;
    for (const auto& p : lib->performances)
    {
        if (! p.hasFm()) continue;
        // Como en Keys: las performances cuyas partes FM-X están a volumen 0 (las sube el Super Knob) no se listan.
        if (std::none_of (p.fm.begin(), p.fm.end(), [] (const ysfc::FmPart& f) { return f.partOn && f.volume > 0; })) { ++hidden; continue; }
        fmx::FmxEngine::prepareTables (p);
        fmx::FmxEngine e;
        e.prepare (SR);
        e.setPerformance (&p);
        std::vector<float> L, R;
        const int chord[] = { 48, 55, 60, 64, 67, 72 };
        for (int k : chord) e.noteOn (k, 100);
        render (e, L, R, int (SR * 1.5));
        for (int k : chord) e.noteOff (k);
        render (e, L, R, int (SR * 1.0));
        float peak = 0;
        bool nan = false;
        for (size_t i = 0; i < L.size(); ++i) { peak = std::max ({ peak, std::abs (L[i]), std::abs (R[i]) }); nan = nan || ! std::isfinite (L[i]) || ! std::isfinite (R[i]); }
        ++played;
        if (nan) { ++nans; std::printf ("  NaN: %s\n", p.name.c_str()); }
        if (peak < 1.0e-3f) { ++silent; std::printf ("  muda: %s (pico %.5f)\n", p.name.c_str(), peak); }
        if (peak > 0.85f) { ++hot; std::printf ("  fuerte: %s pico %.2f\n", p.name.c_str(), peak); }
        if (peak > worstPeak) { worstPeak = peak; worstName = p.name; }
        if (std::find_if (std::begin (demoNames), std::end (demoNames), [&] (const char* n) { return p.name == n; }) != std::end (demoNames))
        {
            // Demo: escala lenta y acorde, al nivel real (sin normalizar).
            fmx::FmxEngine d;
            d.prepare (SR);
            d.setPerformance (&p);
            std::vector<float> dl, dr;
            for (int k : { 48, 52, 55, 60, 64, 67, 72 }) { d.noteOn (k, 90); render (d, dl, dr, int (SR * 0.25)); d.noteOff (k); }
            for (int k : { 48, 55, 64, 71 }) d.noteOn (k, 100);
            render (d, dl, dr, int (SR * 1.6));
            for (int k : { 48, 55, 64, 71 }) d.noteOff (k);
            render (d, dl, dr, int (SR * 1.0));
            std::printf ("  demo: %s (pico acorde %.2f, rms %.3f)\n", p.name.c_str(), peak, rms (L, 0, size_t (SR)));
            demoL.insert (demoL.end(), dl.begin(), dl.end());
            demoR.insert (demoR.end(), dr.begin(), dr.end());
        }
    }
    failures += nans;
    std::printf ("%d performances tocadas (%d sin listar: FM-X a volumen 0): %d con NaN, %d casi mudas en 2,5 s, %d por encima de 0,85 antes del limitador (máximo %.2f en %s)\n",
                 played, hidden, nans, silent, hot, worstPeak, worstName.c_str());

    // Igualar volumen (como la sección FM de Keys): sonoridad de cada performance y diferencia que queda.
    {
        std::vector<float> after;
        const auto t0 = std::chrono::steady_clock::now();
        float gentle = 0, bass = 0;
        for (const auto& p : lib->performances)
        {
            if (std::none_of (p.fm.begin(), p.fm.end(), [] (const ysfc::FmPart& f) { return f.partOn && f.volume > 0; })) continue;
            fmx::FmxEngine::prepareTables (p);
            const float loud = fmx::FmxEngine::measureLoudness (p);
            const float corrected = loud + std::clamp (-22.0f - loud, -12.0f, 12.0f);
            after.push_back (corrected);
            if (p.name == "Gentle EP") gentle = corrected;
            if (p.name == "Bass 1") bass = corrected;
        }
        const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count() / double (after.size());
        std::sort (after.begin(), after.end());
        const float p10 = after[after.size() / 10], p90 = after[after.size() * 9 / 10];
        const bool ok = p90 - p10 < 3.0f && std::abs (gentle - bass) < 1.0f;
        failures += ! ok;
        std::printf ("%s igualar volumen: 80 %% de las performances entre %.1f y %.1f dB; Gentle EP %.1f, Bass 1 %.1f; %.1f ms por medida\n",
                     ok ? "ok:" : "FALLO:", p10, p90, gentle, bass, ms);
    }

    // E.PIANO 1: una nota debe sonar y apagarse sola en unos segundos (decay del DX7), no quedarse.
    for (const auto& p : lib->performances)
        if (p.name == "E-Piano 1")
        {
            fmx::FmxEngine e; e.prepare (SR); e.setPerformance (&p);
            std::vector<float> L, R;
            e.noteOn (60, 100);
            render (e, L, R, int (SR * 6));
            const float a = rms (L, size_t (SR * 0.05), size_t (SR * 0.25)), b = rms (L, size_t (SR * 2), size_t (SR * 2.2)), c = rms (L, size_t (SR * 5.5), size_t (SR * 6));
            const bool ok = a > 0.005f && b < a && c < b;
            failures += ! ok;
            std::printf ("%s E-Piano 1, Do4 sostenido: %.1f dB a 0,1 s, %.1f dB a 2 s, %.1f dB a 5,7 s\n", ok ? "ok:" : "FALLO:",
                         20 * std::log10 (a + 1e-9f), 20 * std::log10 (b + 1e-9f), 20 * std::log10 (c + 1e-9f));
        }
    wav ("build/tests/fmx_demo.wav", demoL, demoR);
    std::printf ("Demo: build/tests/fmx_demo.wav (%.0f s)\n", demoL.size() / SR);
    return failures ? 1 : 0;
}
