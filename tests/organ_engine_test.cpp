// Motor de órgano: misma interpretación que OpenNord (tools/inspect/organrender.ts) muestra a muestra,
// afinación de las ruedas, percusión y rotary. Deja build/tests/organ_*.wav para escucharlo.
//   tests/run_organ_tests.sh
#include "../Source/core/OrganEngine.h"
#include "../Source/core/Rotary.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace bk::organ;
constexpr double SR = 48000;
static int failures = 0;
#define CHECK(cond, ...) do { if (! (cond)) { ++failures; std::printf ("  FALLO: "); } else std::printf ("  ok: "); std::printf (__VA_ARGS__); std::printf ("\n"); } while (0)

struct Ev { double t; bool on; int note; };
static const std::vector<Ev> kEvents {
    { 0.0, true, 60 }, { 0.0, true, 64 }, { 0.0, true, 67 }, { 0.5, false, 60 }, { 0.5, false, 64 }, { 0.5, false, 67 },
    { 0.6, true, 72 }, { 0.62, true, 76 }, { 1.0, false, 72 }, { 1.05, false, 76 },
};

static std::vector<float> renderScript (OrganEngine& e, const std::vector<Ev>& evs, double seconds)
{
    std::vector<float> out (size_t (std::lround (seconds * SR)));
    size_t pos = 0;
    for (const auto& ev : evs)
    {
        const size_t at = std::min (out.size(), size_t (std::lround (ev.t * SR)));
        if (at > pos) { e.render (out.data() + pos, int (at - pos)); pos = at; }
        if (ev.on) e.noteOn (ev.note); else e.noteOff (ev.note);
    }
    if (pos < out.size()) e.render (out.data() + pos, int (out.size() - pos));
    return out;
}

static void writeWav (const std::string& path, const std::vector<float>& l, const std::vector<float>& r)
{
    std::ofstream f (path, std::ios::binary);
    auto u32 = [&] (uint32_t v) { f.write ((const char*) &v, 4); };
    auto u16 = [&] (uint16_t v) { f.write ((const char*) &v, 2); };
    const uint32_t n = uint32_t (l.size());
    f.write ("RIFF", 4); u32 (36 + n * 4); f.write ("WAVEfmt ", 8); u32 (16); u16 (1); u16 (2);
    u32 (uint32_t (SR)); u32 (uint32_t (SR) * 4); u16 (4); u16 (16); f.write ("data", 4); u32 (n * 4);
    for (uint32_t i = 0; i < n; ++i)
        for (float s : { l[i], r[i] }) u16 (uint16_t (int16_t (std::lround (std::clamp (s, -1.0f, 1.0f) * 32767))));
}

/** Pico de la DFT con ventana de Hann alrededor de `f0` (±60 cents, pasos de 0,1 cent). */
static double peakFreq (const std::vector<float>& x, size_t from, size_t n, double f0)
{
    double best = 0, bf = f0;
    for (double c = -60; c <= 60; c += 0.1)
    {
        const double f = f0 * std::pow (2.0, c / 1200);
        double re = 0, im = 0;
        for (size_t i = 0; i < n; ++i)
        {
            const double w = 0.5 - 0.5 * std::cos (2 * M_PI * double (i) / double (n - 1));
            re += w * x[from + i] * std::cos (2 * M_PI * f * double (i) / SR);
            im -= w * x[from + i] * std::sin (2 * M_PI * f * double (i) / SR);
        }
        const double m = std::hypot (re, im);
        if (m > best) { best = m; bf = f; }
    }
    return bf;
}

int main()
{
    struct Case { const char* name; Model model; std::array<float, 9> bars; bool perc, vib; };
    const Case cases[] = {
        { "b3", Model::B3, { 8, 8, 8, 0, 0, 0, 0, 0, 0 }, true, true },
        { "b3full", Model::B3, { 8, 8, 8, 8, 8, 8, 8, 8, 8 }, false, false },
        { "b3bass", Model::B3Bass, { 8, 6, 8, 0, 0, 0, 0, 0, 0 }, false, false },
        { "vox", Model::Vox, { 0, 8, 8, 8, 8, 4, 0, 0, 2 }, false, true },
        { "farf", Model::Farf, { 8, 0, 8, 8, 0, 0, 8, 0, 8 }, false, true },
        { "pipe1", Model::Pipe1, { 8, 0, 8, 6, 0, 4, 0, 0, 2 }, false, false },
        { "pipe2", Model::Pipe2, { 8, 8, 6, 4, 0, 0, 0, 0, 0 }, false, true },
    };
    std::printf ("Igual que OpenNord (misma interpretación, misma semilla)\n");
    for (const auto& c : cases)
    {
        OrganParams p;
        p.model = c.model;
        p.drawbars = c.bars;
        p.percussion = { c.perc, true, true, false };
        p.vibOn = c.vib;
        p.vibType = VibType::C3;
        OrganEngine e;
        e.setParams (p);
        e.prepare (SR);
        const auto got = renderScript (e, kEvents, 1.3);
        std::ifstream f (std::string ("build/inspect/organ/") + c.name + ".f32", std::ios::binary);
        std::vector<char> raw ((std::istreambuf_iterator<char> (f)), {});
        std::vector<float> want (raw.size() / 4);
        std::memcpy (want.data(), raw.data(), raw.size());
        double maxDiff = 0, peak = 0, err = 0, sig = 0;
        for (size_t i = 0; i < std::min (want.size(), got.size()); ++i)
        {
            maxDiff = std::max (maxDiff, double (std::abs (want[i] - got[i])));
            peak = std::max (peak, double (std::abs (want[i])));
            err += double (want[i] - got[i]) * (want[i] - got[i]);
            sig += double (want[i]) * want[i];
        }
        const double snr = 10 * std::log10 (sig / (err + 1e-30));
        CHECK (want.size() == got.size() && snr > 60, "%-7s diferencia máx %.2e (pico %.3f), señal/diferencia %.0f dB", c.name, maxDiff, peak, snr);
        writeWav (std::string ("build/tests/organ_") + c.name + ".wav", got, got);
    }

    std::printf ("Afinación de las ruedas\n");
    {
        OrganParams p;
        p.drawbars = { 0, 0, 8, 0, 0, 0, 0, 0, 0 };
        OrganEngine e;
        e.setParams (p);
        e.prepare (SR);
        const auto s = renderScript (e, { { 0, true, 69 } }, 1.0);
        const double f = peakFreq (s, 9600, 24000, 440);
        CHECK (std::abs (f - 440) < 0.1, "A4 en el 8': %.2f Hz (la rueda de La es exacta: 20·16·88/64 = 440)", f);
        OrganEngine e2;
        e2.setParams (p);
        e2.prepare (SR);
        const auto s2 = renderScript (e2, { { 0, true, 60 } }, 1.0);
        const double c4 = peakFreq (s2, 9600, 24000, 261.63);
        const double want = 20.0 * 16 * 85 / 104; // rueda 37: 16 dientes
        CHECK (std::abs (c4 - want) < 0.1, "C4: %.2f Hz (engranaje 85/104: %.2f, %+.1f cents del temperado)", c4, want, 1200 * std::log2 (want / 261.6256));
    }

    std::printf ("Percusión (disparo único)\n");
    {
        OrganParams p;
        p.drawbars = { 8, 8, 8, 0, 0, 0, 0, 0, 0 };
        p.percussion = { true, true, true, false };
        OrganEngine e;
        e.setParams (p);
        e.prepare (SR);
        std::vector<float> buf (4800);
        e.noteOn (60);
        e.render (buf.data(), 64);
        const float first = e.percussionEnvelope();
        e.render (buf.data(), 4800 * 5);
        const float later = e.percussionEnvelope();
        e.noteOn (64); // legato: sin nuevo golpe
        e.render (buf.data(), 64);
        const float legato = e.percussionEnvelope();
        CHECK (first > 0.9f && later < 0.2f && legato < 0.2f, "golpe %.2f, a los 0,5 s %.2f, nota ligada %.2f", first, later, legato);
    }

    std::printf ("Rotary\n");
    {
        OrganParams p;
        p.drawbars = { 8, 8, 8, 8, 0, 0, 0, 0, 0 };
        OrganEngine e;
        e.setParams (p);
        e.prepare (SR);
        Rotary r;
        r.prepare (SR);
        std::vector<float> mono (size_t (SR * 6)), L (mono.size()), R (mono.size());
        e.noteOn (57); e.noteOn (64); e.noteOn (69);
        e.render (mono.data(), int (mono.size()));
        const int half = int (mono.size() / 2);
        r.setSpeed (Rotary::Speed::Slow);
        r.process (mono.data(), L.data(), R.data(), half);
        r.setSpeed (Rotary::Speed::Fast);
        for (int at = half; at < int (mono.size()); at += 512)
            r.process (mono.data() + at, L.data() + at, R.data() + at, std::min (512, int (mono.size()) - at));
        double diff = 0, lsum = 0, peakOut = 0;
        bool finite = true;
        for (size_t i = 0; i < L.size(); ++i)
        {
            diff += std::abs (L[i] - R[i]);
            lsum += std::abs (L[i]);
            peakOut = std::max (peakOut, double (std::max (std::abs (L[i]), std::abs (R[i]))));
            finite = finite && std::isfinite (L[i]) && std::isfinite (R[i]);
        }
        CHECK (finite && diff / lsum > 0.2 && peakOut < 1.0, "estéreo (diferencia L/R %.0f %%), pico %.2f, sin NaN", 100 * diff / lsum, peakOut);
        writeWav ("build/tests/organ_rotary.wav", L, R);
    }

    std::printf ("Cambio de modelo con notas pulsadas\n");
    {
        OrganEngine e;
        OrganParams p;
        e.setParams (p);
        e.prepare (SR);
        std::vector<float> buf (2400);
        e.noteOn (60); e.noteOn (67);
        e.render (buf.data(), 2400);
        for (Model m : { Model::Vox, Model::Farf, Model::Pipe1, Model::B3 })
        {
            p.model = m;
            e.setParams (p);
            e.render (buf.data(), 2400);
        }
        float peak = 0;
        bool finite = true;
        for (float s : buf) { peak = std::max (peak, std::abs (s)); finite = finite && std::isfinite (s); }
        CHECK (finite && peak > 0.01f, "sin NaN y sigue sonando (pico %.2f, %d generadores)", peak, e.activeGenerators());
    }

    std::printf (failures ? "\n%d fallos\n" : "\nTodo correcto\n", failures);
    return failures ? 1 : 0;
}
