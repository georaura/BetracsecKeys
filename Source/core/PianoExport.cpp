#include "PianoExport.h"
#include "PianoEngine.h"

#include <algorithm>
#include <cmath>

namespace bk::pianoexport
{
double decayDbPerSecond (double perUpdate)
{
    if (! (perUpdate > 0.0) || perUpdate >= 1.0) return 0.0;
    return -20.0 * std::log10 (perUpdate) * npno::kEnvelopeRate;
}

double noteDropDb (const PianoSound& piano, int key, int velocity, double seconds)
{
    // La nota tocada en el piano de Keys (sin ruido de soltar): cuánto ha bajado en `seconds` respecto a su
    // arranque. Es lo que el piano de Keys usa para elegir y atenuar el ruido de soltar tecla.
    constexpr double sr = 48000;
    PianoEngine e;
    e.prepare (sr);
    EngineParams p;
    p.releaseNoise = 0;
    e.setParams (p);
    e.setSound (&piano);
    const int n = int (seconds * sr) + 1;
    std::vector<float> l (size_t (n), 0.0f), r (size_t (n), 0.0f);
    e.noteOn (key, velocity);
    e.render (l.data(), r.data(), n);
    auto rms = [&] (int a, int len)
    {
        double acc = 0;
        for (int i = a; i < a + len && i < n; ++i) acc += double (l[size_t (i)]) * l[size_t (i)] + double (r[size_t (i)]) * r[size_t (i)];
        return acc / std::max (1, len);
    };
    const int w = int (0.05 * sr);
    const double a = rms (int (0.02 * sr), w), b = rms (n - w, w);
    if (a <= 0) return 0;
    if (b <= 0) return 120;
    return std::max (0.0, 10.0 * std::log10 (a / b));
}

bool convert (PianoSound& piano, Converted& out, std::string& error)
{
    const auto& meta = piano.meta;
    out = {};
    out.sound.name = piano.displayName();
    out.sound.path = piano.path;
    out.sound.codec = 0;
    out.sound.releaseUnlooped = true;

    // Golpes con audio (lista normal y ruidos de soltar), en el orden del archivo.
    std::vector<int> strokeIndex (piano.strokes.size(), -1);
    for (size_t i = 0; i < piano.strokes.size(); ++i)
    {
        auto& a = piano.strokes[i];
        if (a.pcm.empty() || (a.flags != 0 && a.flags != 2)) continue;
        bk::nsmp::SampleSound::Stroke s;
        s.frames = a.frames;
        s.channels = piano.channels;
        s.sampleRate = npno::kSampleRate;
        s.loops = a.loop && a.flags == 0;
        s.loopStart = a.loopStart;
        s.loopEnd = a.loopEnd;
        strokeIndex[i] = int (out.sound.strokes.size());   // el PCM se mueve al final: antes se toca el piano
        out.sound.strokes.push_back (std::move (s));
    }
    if (out.sound.strokes.empty()) { error = "El piano no tiene grabaciones cargadas."; return false; }

    auto addZones = [&] (int key, int zone, int list)
    {
        // Capas: rangos seguidos de velocidad que tocan el mismo golpe.
        int start = 1, current = meta.strokeFor (zone, 1, 0, list);
        for (int v = 2; v <= 128; ++v)
        {
            const int idx = v <= 127 ? meta.strokeFor (zone, v, 0, list) : -2;
            if (idx == current) continue;
            if (current >= 0 && strokeIndex[size_t (current)] >= 0)
            {
                const auto& st = meta.strokes[size_t (current)];
                const auto& z = meta.zones[size_t (zone)];
                bk::nsmp::SampleSound::Zone sz { z.root + npno::kRootOffset, key, key, start, v - 1, strokeIndex[size_t (current)], false };
                bk::sfgr::ZoneExtra ex;
                const auto& audio = piano.strokes[size_t (current)];
                if (list == 0)
                {
                    const int pre = std::clamp (z.root - key + 1, 0, 13);
                    ex.preLoopDecayDbPerSec = float (decayDbPerSecond (audio.preLoop[size_t (pre)]));
                    ex.loopDecayDbPerSec = audio.loop ? float (decayDbPerSecond (audio.loopDecay)) : 0.0f;
                    if (key >= meta.undampedFrom) ex.releaseSec = 60.0f;   // sin apagador: la apaga su caída
                    else
                    {
                        int d = audio.damper;
                        if (meta.damperByKey[size_t (key)] > 0) d = meta.damperByKey[size_t (key)];
                        if (d <= 0) d = 40;
                        ex.releaseSec = float (std::max (0.005, npno::damperT60 (d) * 80.0 / 60.0));
                    }
                    ++out.attackZones;
                }
                else
                {
                    // El ruido suena tantos pasos de 0,2 dB por debajo como dice el archivo; cuanto más se
                    // sostuvo la nota, menos ruido (la caída del bucle de la nota, hasta 25,5 dB/s).
                    const int below = st.partner > st.floor ? st.partner - st.floor : 50;
                    // BeTracks Pro sube este ruido +12 dB por defecto (su mando arranca ahí porque el ruido de los
                    // bancos SFZ viene muy bajo): se le resta aquí para que, con el mando en su sitio, suene como
                    // en Keys.
                    ex.volumeDb = float (-0.2 * below - kBeTracksNoiseBoostDb);
                    ex.releaseTrigger = true;
                    ex.releaseSec = 0.3f;
                    // Cuanto más se sostuvo la nota, menos ruido: lo que cae la nota en su primer segundo, medido
                    // en el piano de Keys (la caída del bucle sola se quedaba corta: 20 dB en el C5 a 1 s).
                    ex.rtDecayDbPerSec = float (std::min (25.5, noteDropDb (piano, key, std::clamp ((start + v - 1) / 2, 1, 127), 1.0)));
                    ++out.releaseZones;
                }
                out.sound.zones.push_back (sz);
                out.extras.push_back (ex);
            }
            start = v;
            current = idx;
        }
    };

    for (int key = 0; key < 128; ++key)
    {
        const int zone = meta.zoneForKey (key);
        if (zone < 0) continue;
        addZones (key, zone, 0);
        if (piano.hasReleases) addZones (key, zone, 2);
    }
    if (out.attackZones == 0) { error = "El piano no tiene zonas que tocar."; return false; }
    for (size_t i = 0; i < piano.strokes.size(); ++i)
        if (strokeIndex[i] >= 0)
        {
            out.sound.strokes[size_t (strokeIndex[i])].pcm = std::move (piano.strokes[i].pcm);
            piano.strokes[i].pcm.clear();
        }
    return true;
}
} // namespace bk::pianoexport
