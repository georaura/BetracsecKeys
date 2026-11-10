// Piano Nord → BeTracks Pro (Source/core/PianoExport): cada tecla queda cubierta por sus capas sin huecos ni
// solapes, con el tono, la caída y el apagador de su nota. Usa el piano sintético de tests/golden (sin
// audio de Clavia).
//   tests/run_sfgr_tests.sh
#include "../Source/core/PianoExport.h"
#include "../Source/core/PianoEngine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>

int main()
{
    int failures = 0;
    auto check = [&] (bool ok, const char* what) { failures += ! ok; std::printf ("%s %s\n", ok ? "ok:" : "FALLO:", what); };

    std::string err;
    auto piano = bk::loadPiano ("tests/golden/M4.npno", err);
    check (piano != nullptr, "se carga el piano de prueba");
    if (! piano) { std::printf ("   %s\n", err.c_str()); return 1; }
    // El piano sintético no cae antes del bucle: se le pone una caída DISTINTA por semitono (2, 4, 6… dB/s),
    // para que un índice corrido se note. La referencia es el motor de piano de Keys, no una fórmula copiada.
    auto withPre = [] (bk::PianoSound& p)
    {
        for (auto& s : p.strokes)
            for (size_t k = 0; k < s.preLoop.size(); ++k) s.preLoop[k] = std::pow (10.0, -2.0 * double (k + 1) / 20.0 / 22050.0);
    };
    withPre (*piano);
    auto refPre = bk::loadPiano ("tests/golden/M4.npno", err);   // el motor con la caída…
    auto refFlat = bk::loadPiano ("tests/golden/M4.npno", err);  // …y sin ella: el cociente es la caída sola
    withPre (*refPre);
    for (auto& s : refFlat->strokes) { s.preLoop.fill (1.0); }
    auto render = [] (const bk::PianoSound& p, int key, int frames)
    {
        bk::PianoEngine e;
        e.prepare (48000);
        e.setSound (&p);
        std::vector<float> l (size_t (frames), 0.0f), r (size_t (frames), 0.0f);
        e.noteOn (key, 100);
        e.render (l.data(), r.data(), frames);
        return l;
    };
    const auto meta = piano->meta;   // copia: convert mueve el PCM
    std::vector<bk::StrokeAudio> audio;
    for (const auto& s : piano->strokes) { bk::StrokeAudio a = s; a.pcm.clear(); audio.push_back (a); }

    bk::pianoexport::Converted c;
    check (bk::pianoexport::convert (*piano, c, err), "se convierte");
    check (c.extras.size() == c.sound.zones.size() && c.attackZones > 0, "una ZoneExtra por zona");

    // Cobertura: en cada tecla de cada zona del piano, cada velocidad 1..127 cae en UNA zona de ataque.
    bool covered = true, pitch = true, decay = true, release = true;
    for (int key = 0; key < 128; ++key)
    {
        const int pz = meta.zoneForKey (key);
        for (int v = 1; v <= 127; ++v)
        {
            int hits = 0;
            for (size_t i = 0; i < c.sound.zones.size(); ++i)
            {
                const auto& z = c.sound.zones[i];
                if (c.extras[i].releaseTrigger || key < z.low || key > z.high || v < z.velLow || v > z.velHigh) continue;
                ++hits;
                if (z.root != meta.zones[size_t (pz)].root + 4) pitch = false;
                // La caída es la del golpe que el piano elige para esa tecla y fuerza.
                const auto& a = audio[size_t (meta.strokeFor (pz, v, 0, 0))];
                const int pre = std::clamp (meta.zones[size_t (pz)].root - key + 1, 0, 13);
                if (std::abs (c.extras[i].preLoopDecayDbPerSec - bk::pianoexport::decayDbPerSecond (a.preLoop[size_t (pre)])) > 1e-3
                    || (a.loop && std::abs (c.extras[i].loopDecayDbPerSec - bk::pianoexport::decayDbPerSecond (a.loopDecay)) > 1e-3))
                    decay = false;
                const bool undamped = key >= meta.undampedFrom;
                if (undamped != (c.extras[i].releaseSec >= 60.0f) || c.extras[i].releaseSec <= 0) release = false;
            }
            if (hits != (pz >= 0 ? 1 : 0)) covered = false;
        }
    }
    check (covered, "cada tecla y fuerza: exactamente una zona de ataque (ninguna fuera del piano)");

    // La caída antes del bucle, medida en el motor de Keys a 0,1 s, contra la que lleva la zona.
    bool engineDecay = true;
    for (int key : { 36, 40, 46, 47, 48, 70, 72, 80, 96 })
    {
        const int frames = 4800;   // 0,1 s, antes de cualquier bucle del piano de prueba
        const auto a = render (*refPre, key, frames + 64), b = render (*refFlat, key, frames + 64);
        double ea = 0, eb = 0;
        for (int i = frames - 64; i < frames; ++i) { ea += double (a[size_t (i)]) * a[size_t (i)]; eb += double (b[size_t (i)]) * b[size_t (i)]; }
        const double measured = eb > 0 && ea > 0 ? -10.0 * std::log10 (ea / eb) / 0.0993 : -1;   // dB/s
        for (size_t i = 0; i < c.sound.zones.size(); ++i)
        {
            const auto& z = c.sound.zones[i];
            if (c.extras[i].releaseTrigger || key != z.low || 100 < z.velLow || 100 > z.velHigh) continue;
            if (std::abs (measured - c.extras[i].preLoopDecayDbPerSec) > 0.2)
            {
                engineDecay = false;
                std::printf ("   tecla %d: el motor cae %.2f dB/s, la zona lleva %.2f\n", key, measured, c.extras[i].preLoopDecayDbPerSec);
            }
        }
    }
    check (engineDecay, "la caída antes del bucle es la que hace el piano de Keys en esa tecla");
    check (pitch, "nota central = raíz + 4 (la grabación suena 4 semitonos por encima)");
    check (decay, "caída antes del bucle (por semitono) y dentro (del golpe), en dB/s");
    check (release, "release: el apagador de la tecla; sin apagador, 60 s");
    check ((c.releaseZones > 0) == piano->hasReleases, "zonas de soltar tecla si el piano las trae");

    // Y se escribe: con caída, el bloque de modulación va detrás del PCM.
    bk::sfgr::ExportOptions o;
    o.zoneExtras = c.extras;
    const auto w = bk::sfgr::write ("build/tests/piano.sfgr", c.sound.name, c.sound, o);
    std::ifstream f ("build/tests/piano.sfgr", std::ios::binary);
    std::vector<uint8_t> d ((std::istreambuf_iterator<char> (f)), {});
    uint64_t mod = 0;
    if (d.size() >= 160) std::memcpy (&mod, d.data() + 144, 8);
    check (w.ok && mod != 0 && w.zones == int (c.sound.zones.size()), "se escribe con la caída en el bloque de modulación");
    std::printf ("   %s: %d zonas de ataque, %d de soltar, %d muestras, %.1f MB\n", c.sound.name.c_str(), c.attackZones,
                 c.releaseZones, w.samples, double (w.ramBytes) / 1e6);
    return failures ? 1 : 0;
}
