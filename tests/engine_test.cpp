// Prueba del motor: afinación, capas, pedal, apagador y robo de voces, sobre los pianos sintéticos.
// Deja build/tests/engine_demo.wav para escucharlo.
#include "../Source/core/PianoEngine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <vector>

using namespace bk;

static int failures = 0;
#define CHECK(cond, ...) do { if (! (cond)) { ++failures; std::printf ("  FALLO: "); std::printf (__VA_ARGS__); std::printf ("\n"); } else { std::printf ("  ok: "); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

constexpr double SR = 48000;

struct Out { std::vector<float> l, r; };
static Out run (PianoEngine& e, int frames)
{
    Out o { std::vector<float> (size_t (frames)), std::vector<float> (size_t (frames)) };
    for (int at = 0; at < frames; at += 256)
    {
        const int n = std::min (256, frames - at);
        e.render (o.l.data() + at, o.r.data() + at, n);
    }
    return o;
}

static double rms (const std::vector<float>& x, size_t from, size_t to)
{
    double s = 0; to = std::min (to, x.size());
    for (size_t i = from; i < to; ++i) s += double (x[i]) * x[i];
    return to > from ? std::sqrt (s / double (to - from)) : 0;
}

/** Frecuencia fundamental por autocorrelación (búsqueda fina entre 50 y 2000 Hz). */
static double pitch (const std::vector<float>& x, size_t from, size_t len)
{
    auto ac = [&] (double lag) { double c = 0; for (size_t i = from; i < from + len; ++i) c += double (x[i]) * x[i + size_t (lag)]; return c; };
    const int lo = int (SR / 2000), hi = int (SR / 50);
    std::vector<double> cor (size_t (hi + 2));
    double best = 0;
    for (int lag = lo; lag <= hi + 1; ++lag) { cor[size_t (lag)] = ac (lag); if (lag <= hi) best = std::max (best, cor[size_t (lag)]); }
    // El primer máximo local que llega al 90 % del mayor: el periodo, no un múltiplo (evita errores de octava).
    int bestLag = lo;
    for (int lag = lo + 1; lag <= hi; ++lag)
        if (cor[size_t (lag)] >= 0.9 * best && cor[size_t (lag)] >= cor[size_t (lag - 1)] && cor[size_t (lag)] >= cor[size_t (lag + 1)]) { bestLag = lag; break; }
    // Interpolación parabólica alrededor del máximo.
    const double a = ac (bestLag - 1), b = ac (bestLag), c = ac (bestLag + 1);
    const double shift = 0.5 * (a - c) / (a - 2 * b + c);
    return SR / (bestLag + shift);
}

static void writeWav (const char* path, const std::vector<float>& l, const std::vector<float>& r)
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

int main()
{
    std::string err;
    auto m4 = loadPiano ("tests/golden/M4.npno", err);
    if (! m4) { std::printf ("no carga M4: %s\n", err.c_str()); return 1; }
    std::printf ("M4: %zu golpes, %.1f MB en memoria\n", m4->strokes.size(), m4->memoryBytes() / 1e6);

    PianoEngine e;
    e.prepare (SR);
    e.setSound (m4.get());

    std::printf ("Afinación\n");
    for (int key : { 40, 57, 69, 81 })
    {
        e.noteOn (key, 100);
        auto o = run (e, int (SR * 0.4));
        const double want = 440.0 * std::pow (2.0, (key - 69) / 12.0);
        const double got = pitch (o.l, size_t (SR * 0.1), 4096);
        const double cents = 1200 * std::log2 (got / want);
        CHECK (std::abs (cents) < 3, "tecla %d: %.2f Hz (esperado %.2f, %+.2f cents)", key, got, want, cents);
        e.allNotesOff (true);
    }
    e.setParams ({ .tuneCents = 100 });
    e.noteOn (69, 100);
    {
        auto o = run (e, int (SR * 0.4));
        const double got = pitch (o.l, size_t (SR * 0.1), 4096);
        CHECK (std::abs (1200 * std::log2 (got / 466.16)) < 3, "afinación +100 cents: %.2f Hz", got);
    }
    e.allNotesOff (true);
    e.setParams ({});

    std::printf ("Fuerza\n");
    {
        e.noteOn (60, 127); auto hard = run (e, int (SR * 0.3)); e.allNotesOff (true);
        e.noteOn (60, 30);  auto soft = run (e, int (SR * 0.3)); e.allNotesOff (true);
        const double db = 20 * std::log10 (rms (hard.l, 2000, 10000) / rms (soft.l, 2000, 10000));
        CHECK (db > 15 && db < 30, "vel 127 vs 30: %.1f dB más fuerte", db);
    }

    std::printf ("Apagador y pedal\n");
    {
        e.noteOn (60, 100);
        run (e, int (SR * 0.2));
        e.noteOff (60);
        auto o = run (e, int (SR * 0.5));
        CHECK (e.activeVoices() == 0, "tecla suelta: se apaga en < 0,5 s (voces activas %d)", e.activeVoices());

        e.setSustain (true);
        e.noteOn (60, 100);
        run (e, int (SR * 0.2));
        e.noteOff (60);
        auto held = run (e, int (SR * 0.5));
        CHECK (e.activeVoices() == 1 && rms (held.l, size_t (SR * 0.4), size_t (SR * 0.5)) > 1e-3, "con pedal sigue sonando");
        e.setSustain (false);
        run (e, int (SR * 0.5));
        CHECK (e.activeVoices() == 0, "al soltar el pedal se apaga");
    }

    std::printf ("Polifonía\n");
    {
        for (int k = 36; k < 36 + 120; ++k) e.noteOn (36 + (k % 61), 90);
        auto o = run (e, 512);
        CHECK (e.activeVoices() <= PianoEngine::kMaxVoices, "120 notas: %d voces (máx %d)", e.activeVoices(), PianoEngine::kMaxVoices);
        float peak = 0; for (float s : o.l) peak = std::max (peak, std::abs (s));
        CHECK (std::isfinite (peak), "sin NaN (pico %.2f)", peak);
        e.allNotesOff (true);
    }

    // Demo para escuchar: escala, acorde con pedal y acorde suelto.
    {
        std::vector<float> L, R;
        auto add = [&] (const Out& o) { L.insert (L.end(), o.l.begin(), o.l.end()); R.insert (R.end(), o.r.begin(), o.r.end()); };
        for (int k : { 48, 50, 52, 53, 55, 57, 59, 60 }) { e.noteOn (k, 70 + (k % 5) * 10); add (run (e, int (SR * 0.22))); e.noteOff (k); }
        e.setSustain (true);
        for (int k : { 48, 55, 64, 67, 72 }) { e.noteOn (k, 95); add (run (e, int (SR * 0.06))); }
        for (int k : { 48, 55, 64, 67, 72 }) e.noteOff (k);
        add (run (e, int (SR * 1.5)));
        e.setSustain (false);
        add (run (e, int (SR * 0.6)));
        writeWav ("build/tests/engine_demo.wav", L, R);
        std::printf ("Demo: build/tests/engine_demo.wav (%.1f s)\n", L.size() / SR);
    }

    std::printf (failures ? "\n%d fallos\n" : "\nTodo correcto\n", failures);
    return failures ? 1 : 0;
}
