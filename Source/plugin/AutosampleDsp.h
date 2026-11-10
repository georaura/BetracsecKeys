// BeTracks Keys — procesado de las tomas del muestreador: recorte del silencio inicial (compensa sola el
// retardo de MIDI y audio), final de la cola cuando cae al ruido de fondo, y bucle para sonidos sostenidos
// (el punto donde la onda mejor se parece a sí misma, con un fundido para que la unión no se oiga).
#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <cmath>

namespace bk::autosample
{
struct Take
{
    juce::AudioBuffer<float> audio;   ///< grabación completa: preámbulo de silencio, nota y cola
    int noteOffAt = 0;                ///< muestra en la que se soltó la nota
    int preRoll = 0;                  ///< muestras de silencio antes de la nota (para medir el ruido)
    double sampleRate = 48000;
};

struct Result
{
    juce::AudioBuffer<float> audio;
    bool loops = false;
    int loopStart = 0, loopEnd = 0;   ///< [loopStart, loopEnd); el audio lleva 4 muestras más tras loopEnd
    float peak = 0, noise = 0;        ///< pico y ruido de fondo (RMS del preámbulo), lineales
    bool clipped = false, silent = false;
};

inline float rmsOf (const juce::AudioBuffer<float>& a, int start, int len)
{
    double s = 0;
    int n = 0;
    for (int c = 0; c < a.getNumChannels(); ++c)
        for (int i = std::max (0, start); i < std::min (a.getNumSamples(), start + len); ++i) { s += double (a.getSample (c, i)) * a.getSample (c, i); ++n; }
    return n > 0 ? float (std::sqrt (s / n)) : 0.0f;
}

/** `minCorrelation`: parecido mínimo para aceptar un bucle (0,9). Con −1 el bucle es obligatorio: se toma el
 *  mejor punto que haya (sonidos que se sostienen sí o sí, como un órgano o un pad generados por Keys).
 *  `searchFrom`: muestra de la toma antes de la cual no se busca el inicio del bucle (−1 = sin límite). */
inline Result process (const Take& t, bool wantLoop, double minCorrelation = 0.9, int searchFrom = -1)
{
    Result r;
    const auto& a = t.audio;
    const int len = a.getNumSamples(), chans = a.getNumChannels();
    const double sr = t.sampleRate;
    if (len < 64 || chans == 0) { r.silent = true; return r; }
    r.noise = rmsOf (a, 0, std::max (1, t.preRoll - int (0.01 * sr)));
    for (int c = 0; c < chans; ++c) r.peak = std::max (r.peak, a.getMagnitude (c, 0, len));
    r.clipped = r.peak > 0.99f;
    if (r.peak < 0.001f || r.peak < r.noise * 10) { r.silent = true; return r; }

    auto mono = [&] (int i) { float v = 0; for (int c = 0; c < chans; ++c) v += a.getSample (c, i); return v / float (chans); };
    auto absMax = [&] (int i) { float v = 0; for (int c = 0; c < chans; ++c) v = std::max (v, std::abs (a.getSample (c, i))); return v; };

    // Inicio: el primer punto claramente por encima del ruido (−50 dB del pico como mínimo), 2 ms antes.
    const float onThr = std::max (r.noise * 6.0f, r.peak * 0.003f);
    int onset = 0;
    while (onset < len && absMax (onset) < onThr) ++onset;
    const int start = std::max (0, onset - int (0.002 * sr));

    // Final: la última ventana de 10 ms por encima del ruido (o −60 dB del pico) tras soltar la nota.
    const int win = std::max (32, int (0.01 * sr));
    const float offThr = std::max (r.noise * 2.0f, r.peak * 0.001f);
    int end = len;
    for (int p = std::max (start, t.noteOffAt); p + win <= len; p += win)
        if (rmsOf (a, p, win) > offThr) end = std::min (len, p + 2 * win);
    end = std::max (end, std::min (len, start + win * 4));

    if (wantLoop)
    {
        // Bucle dentro de la parte sostenida (entre el 35 % y el 92 % de la nota mantenida).
        const int held = t.noteOffAt - start;
        // `searchFrom`: no buscar antes de ahí (un sonido que cae al principio y luego se asienta: el bucle va en
        // la parte asentada, o saltaría de nivel en cada vuelta).
        const int b = start + int (0.92 * held), lo = std::max (start + int (0.35 * held), std::min (searchFrom, b - int (0.25 * sr) - 1100));
        const int minLoop = int (0.25 * sr), w = 1024;
        if (b - lo > minLoop + w && b + 4 < len)
        {
            auto corrAt = [&] (int s)
            {
                double xy = 0, xx = 0, yy = 0;
                for (int i = 0; i < w; ++i) { const double x = mono (s - w + i), y = mono (b - w + i); xy += x * y; xx += x * x; yy += y * y; }
                return xx > 0 && yy > 0 ? xy / std::sqrt (xx * yy) : 0.0;
            };
            int best = -1;
            double bestC = -2;
            for (int s = std::max (lo, w); s <= b - minLoop; s += 8)
                if (const double c = corrAt (s); c > bestC) { bestC = c; best = s; }
            if (best > 0)
            {
                for (int s = std::max (best - 8, w); s <= std::min (best + 8, b - minLoop); ++s)
                    if (const double c = corrAt (s); c > bestC) { bestC = c; best = s; }
            }
            if (best > 0 && bestC > minCorrelation)
            {
                const int xf = std::min (int (0.05 * sr), (b - best) / 4);
                r.audio.setSize (chans, b - start + 4);
                for (int c = 0; c < chans; ++c)
                {
                    const float* in = a.getReadPointer (c);
                    float* out = r.audio.getWritePointer (c);
                    for (int i = start; i < b; ++i) out[i - start] = in[i];
                    for (int i = 0; i < xf; ++i)
                    {
                        const float g = float (i) / float (xf);
                        out[b - xf + i - start] = in[b - xf + i] * (1.0f - g) + in[best - xf + i] * g;
                    }
                    for (int i = 0; i < 4; ++i) out[b - start + i] = in[best + i];
                }
                r.loops = true;
                r.loopStart = best - start;
                r.loopEnd = b - start;
                return r;
            }
        }
    }
    r.audio.setSize (chans, end - start);
    for (int c = 0; c < chans; ++c) r.audio.copyFrom (c, 0, a, c, start, end - start);
    // Fundido de 10 ms al final para que no haga clic.
    const int fade = std::min (r.audio.getNumSamples(), int (0.01 * sr));
    r.audio.applyGainRamp (r.audio.getNumSamples() - fade, fade, 1.0f, 0.0f);
    return r;
}
} // namespace bk::autosample
