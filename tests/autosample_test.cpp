// Muestreador: recorte, bucle y formato .bkm con tomas sintéticas (sin instrumento).
#include "../Source/plugin/AutosampleDsp.h"
#include "../Source/plugin/KeysSampleFile.h"
#include <cstdio>

using namespace juce;
static bk::autosample::Take synth (bool sustained, double latency, double sr = 48000)
{
    bk::autosample::Take t;
    t.sampleRate = sr; t.preRoll = int (0.08 * sr);
    const int hold = int (2.0 * sr), tail = int (1.5 * sr), start = t.preRoll + int (latency * sr);
    t.noteOffAt = t.preRoll + hold;
    t.audio.setSize (2, start + hold + tail);
    Random rnd (1);
    double ph = 0;
    for (int i = 0; i < t.audio.getNumSamples(); ++i)
    {
        float x = (rnd.nextFloat() * 2 - 1) * 1.0e-4f;   // ruido −80 dB
        if (i >= start)
        {
            const double tt = (i - start) / sr;
            double env = sustained ? std::min (1.0, tt / 0.01) : std::exp (-tt * 1.5);
            if (i > t.noteOffAt) env *= std::exp (-(i - t.noteOffAt) / sr * 12.0);
            ph += 220.0 / sr; ph -= std::floor (ph);
            x += float (0.5 * env * (2 * ph - 1));
        }
        t.audio.setSample (0, i, x); t.audio.setSample (1, i, x * 0.9f);
    }
    return t;
}

int main()
{
    int failures = 0;
    const auto a = synth (true, 0.015), b = synth (false, 0.03);
    const auto ra = bk::autosample::process (a, true), rb = bk::autosample::process (b, false);
    const double lat = (a.preRoll + 0.015 * 48000) - (a.audio.getNumSamples() - 0); (void) lat;
    const bool okA = ra.loops && ra.loopEnd - ra.loopStart > 0.25 * 48000 && ra.audio.getNumSamples() == ra.loopEnd + 4 && ! ra.silent;
    std::printf ("%s sostenido: bucle %d..%d (%.2f s), %.2f s guardados (grabados %.2f s), ruido %.0f dB\n", okA ? "ok:" : "FALLO:",
                 ra.loopStart, ra.loopEnd, (ra.loopEnd - ra.loopStart) / 48000.0, ra.audio.getNumSamples() / 48000.0,
                 a.audio.getNumSamples() / 48000.0, Decibels::gainToDecibels (ra.noise));
    failures += ! okA;
    // `searchFrom`: el bucle no empieza antes de lo pedido (sonidos que se asientan tarde: FM, pads).
    {
        const int from = a.preRoll + int (1.5 * 48000);
        const auto rc = bk::autosample::process (a, true, -1.0, from);
        const int startInTake = a.preRoll + int (0.015 * 48000) - int (0.002 * 48000);   // donde empieza lo guardado
        const bool okS = rc.loops && rc.loopStart + startInTake >= from - 64 && rc.loopEnd - rc.loopStart > 0.25 * 48000 * 0.9;
        std::printf ("%s bucle desde 1,5 s: empieza a %.2f s de la nota\n", okS ? "ok:" : "FALLO:", (rc.loopStart + startInTake - a.preRoll) / 48000.0);
        failures += ! okS;
    }
    // Primer golpe: arranca ~2 ms antes del sonido (el preámbulo y el retardo se recortan).
    int first = 0; while (std::abs (rb.audio.getSample (0, first)) < 0.01f) ++first;
    const bool okB = ! rb.loops && first > 0 && first < 0.01 * 48000 && rb.audio.getNumSamples() < b.audio.getNumSamples() - 0.1 * 48000;
    std::printf ("%s percusivo: empieza %.1f ms antes del sonido, %.2f s guardados de %.2f s\n", okB ? "ok:" : "FALLO:",
                 first / 48.0, rb.audio.getNumSamples() / 48000.0, b.audio.getNumSamples() / 48000.0);
    failures += ! okB;

    std::vector<bk::bkm::Stroke> strokes (2);
    strokes[0].audio = ra.audio; strokes[0].loops = true; strokes[0].loopStart = ra.loopStart; strokes[0].loopEnd = ra.loopEnd;
    strokes[0].root = 57; strokes[0].low = 0; strokes[0].high = 127; strokes[0].velLow = 1; strokes[0].velHigh = 80;
    strokes[1].audio = rb.audio; strokes[1].root = 57; strokes[1].velLow = 81; strokes[1].velHigh = 127;
    const File f = File::getCurrentWorkingDirectory().getChildFile ("build/tests/prueba.bkm");
    String err;
    const bool wrote = bk::bkm::write (f, "Prueba", "test", 48000, strokes, err);
    std::string e2;
    auto s = bk::bkm::read (f, e2);
    const bool okF = wrote && s != nullptr && s->zones.size() == 2 && s->strokes.size() == 2 && s->strokes[0].loops
                     && s->strokes[0].loopEnd == uint32_t (ra.loopEnd) && s->strokes[1].frames == uint32_t (rb.audio.getNumSamples())
                     && s->strokes[0].channels == 2 && s->releaseUnlooped;
    double raw = 0; for (auto& st : strokes) raw += st.audio.getNumSamples() * 2.0 * 2;
    std::printf ("%s .bkm: escrito y leído (%s%s), %.0f KB frente a %.0f KB en WAV 16 bits\n", okF ? "ok:" : "FALLO:", err.toRawUTF8(), e2.c_str(),
                 f.getSize() / 1024.0, raw / 1024.0);
    failures += ! okF;
    // La reproducción con el motor de samples: suena y el bucle se mantiene mientras la tecla está pulsada.
    bk::nsmp::SampleEngine eng; eng.prepare (48000); eng.setSound (s.get());
    eng.noteOn (57, 60);
    std::vector<float> L (48000 * 4), R (L.size());
    for (size_t i = 0; i < L.size(); i += 256) eng.render (L.data() + i, R.data() + i, 256);
    float late = 0; for (size_t i = L.size() - 4800; i < L.size(); ++i) late = std::max (late, std::abs (L[i]));
    eng.noteOff (57);
    std::fill (L.begin(), L.end(), 0.0f);
    for (size_t i = 0; i < 48000; i += 256) eng.render (L.data() + i, R.data() + i, 256);
    float after = 0; for (size_t i = 24000; i < 48000; ++i) after = std::max (after, std::abs (L[i]));
    const bool okP = late > 0.05f && after < late * 0.05f;
    std::printf ("%s reproducción: sigue sonando a los 4 s (%.2f) y se apaga al soltar (%.4f)\n", okP ? "ok:" : "FALLO:", late, after);
    failures += ! okP;
    return failures ? 1 : 0;
}
