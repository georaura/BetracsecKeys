// Exportación a BeTracks Pro (.sfgr v1): el archivo cumple la especificación (cabecera de 160 bytes, zonas de 56,
// índice de 32, CRC32 de tablas y de PCM, muestras alineadas a 4), el remuestreo a 48 kHz es fiel y las capas
// de velocidad llevan su compensación. No usa código de BeTracks Pro: lee el archivo con la especificación.
//   tests/run_sfgr_tests.sh
#include "../Source/core/SfgrWriter.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

static uint32_t u32 (const std::vector<uint8_t>& b, size_t at) { uint32_t v; std::memcpy (&v, b.data() + at, 4); return v; }
static uint64_t u64 (const std::vector<uint8_t>& b, size_t at) { uint64_t v; std::memcpy (&v, b.data() + at, 8); return v; }
static float f32 (const std::vector<uint8_t>& b, size_t at) { float v; std::memcpy (&v, b.data() + at, 4); return v; }

int main()
{
    int failures = 0;
    auto check = [&] (bool ok, const char* what) { failures += ! ok; std::printf ("%s %s\n", ok ? "ok:" : "FALLO:", what); };

    // Un banco de prueba: un seno de 1 kHz a 35.002 Hz (como un Nord Sample) con bucle, y un golpe estéreo
    // de 3 frames impares (para la alineación), en dos capas de velocidad grabadas a 32 y 127.
    bk::nsmp::SampleSound s;
    s.releaseUnlooped = true;
    bk::nsmp::SampleSound::Stroke a;
    a.channels = 1; a.sampleRate = 35002; a.frames = 35003;
    a.pcm.resize (a.frames);
    for (uint32_t i = 0; i < a.frames; ++i) a.pcm[i] = int16_t (std::lround (16000 * std::sin (2 * M_PI * 1000.0 * i / 35002.0)));
    a.loops = true; a.loopStart = 1000; a.loopEnd = 30000;
    bk::nsmp::SampleSound::Stroke b;
    b.channels = 2; b.sampleRate = 48000; b.frames = 4801;
    b.pcm.assign (size_t (b.frames) * 2, 4000);
    s.strokes = { a, b };
    bk::nsmp::SampleSound::Zone z1 { 60, 0, 127, 1, 80, 0, false };
    z1.velRoot = 32;
    bk::nsmp::SampleSound::Zone z2 { 60, 0, 127, 81, 127, 1, false };
    z2.velRoot = 127;
    s.zones = { z1, z2 };

    const std::string path = "build/tests/prueba.sfgr";
    bk::sfgr::ExportOptions o;
    o.trimDb = 0;
    const auto r = bk::sfgr::write (path, "Prueba ñ", s, o);
    check (r.ok, "se escribe");
    std::ifstream f (path, std::ios::binary);
    std::vector<uint8_t> d ((std::istreambuf_iterator<char> (f)), {});

    check (d.size() >= 160 && std::memcmp (d.data(), "APNO", 4) == 0 && u32 (d, 4) == 1 && u32 (d, 8) == 0x01020304u && u32 (d, 12) == 160,
           "cabecera: magia APNO, versión 1, endianness, 160 bytes");
    check (u64 (d, 16) == d.size(), "fileSize = tamaño real");
    check (u32 (d, 24) == 48000 && u32 (d, 28) == 2 && u32 (d, 32) == 2 && u32 (d, 36) == 1, "48 kHz, 2 zonas, 2 muestras, PCM16");
    const uint64_t zo = u64 (d, 40), so = u64 (d, 48), po = u64 (d, 56), pb = u64 (d, 64);
    check (zo == 160 && so == 160 + 2 * 56 && po == so + 2 * 32 && po + pb == d.size(), "tablas y PCM contiguos y dentro del archivo");
    check (bk::sfgr::crc32 (d.data() + so, 64, bk::sfgr::crc32 (d.data() + zo, 112)) == u32 (d, 72), "CRC de las tablas");
    check (bk::sfgr::crc32 (d.data() + po, size_t (pb)) == u32 (d, 76), "CRC del PCM");
    check (std::string (reinterpret_cast<const char*> (d.data() + 80)) == "Prueba ñ", "nombre en UTF-8");
    for (int i = 0; i < 2; ++i)
    {
        const size_t at = size_t (so) + size_t (i) * 32;
        const uint64_t off = u64 (d, at), frames = u64 (d, at + 8);
        const uint32_t ch = u32 (d, at + 16);
        check (off % 4 == 0 && off + frames * ch * 2 <= pb && u32 (d, at + 20) == 48000
                   && bk::sfgr::crc32 (d.data() + po + off, size_t (frames * ch * 2)) == u32 (d, at + 24),
               i == 0 ? "muestra 0: alineada, dentro del PCM, CRC propio" : "muestra 1 (estéreo, frames impares): alineada");
    }
    // Remuestreo: el seno de 1 kHz sigue siendo de 1 kHz a 48 kHz (comparado con el seno exacto).
    {
        const uint64_t frames = u64 (d, so + 8);
        const auto* pcm = reinterpret_cast<const int16_t*> (d.data() + po);
        double sig = 0, err = 0;
        const double g = 0.891 / (16000.0 / 32768.0) * 16000.0;
        for (uint64_t j = 200; j + 200 < frames; ++j) { const double ref = g * std::sin (2 * M_PI * 1000.0 * double (j) / 48000.0); sig += ref * ref; err += (pcm[j] - ref) * (pcm[j] - ref); }
        const double snr = 10 * std::log10 (sig / err);
        std::printf ("   seno 1 kHz de 35.002 a 48.000 Hz: %llu frames, señal/error %.1f dB\n", (unsigned long long) frames, snr);
        // Con bucle, la muestra se guarda hasta el final del bucle (+8 frames de guarda para interpolar).
        check (frames == uint64_t (std::lround (30000 * 48000.0 / 35002.0)) + 8 && snr > 80, "remuestreo fiel (más de 80 dB) y corte tras el bucle");
    }
    // Zonas: bucle escalado a 48 kHz, compensación de capa (TOUCH 2: +40·log10(127/32) dB) y release.
    {
        const size_t z = size_t (zo);
        const uint32_t ls = u32 (d, z + 32), le = u32 (d, z + 36);
        check (d[z + 28] == 1 && std::abs (int (ls) - int (std::lround (1000 * 48000.0 / 35002.0))) <= 1
                   && std::abs (int (le) - int (std::lround (30000 * 48000.0 / 35002.0))) <= 1, "bucle a 48 kHz");
        check (std::abs (f32 (d, z + 16) - float (40 * std::log10 (127.0 / 32))) < 0.01f && std::abs (f32 (d, z + 56 + 16)) < 0.01f,
               "capa grabada a 32: +24 dB; a 127: 0 dB");
        check (std::abs (f32 (d, z + 52) - 0.3f) < 1e-6f && f32 (d, z + 48) == 1.0f, "liberación de SAMPLES y sostenido 1");
    }
    // Recorte de colas: un golpe que decae deja de ocupar lo que queda bajo −70 dB.
    {
        bk::nsmp::SampleSound t;
        bk::nsmp::SampleSound::Stroke c;
        c.channels = 1; c.sampleRate = 48000; c.frames = 48000 * 6;
        c.pcm.resize (c.frames);
        for (uint32_t i = 0; i < c.frames; ++i) c.pcm[i] = int16_t (std::lround (30000 * std::exp (-double (i) / 48000.0 * 4) * std::sin (2 * M_PI * 220.0 * i / 48000.0)));
        t.strokes = { c };
        t.zones = { { 57, 0, 127, 1, 127, 0, false } };
        bk::sfgr::ExportOptions o2;
        const auto e = bk::sfgr::estimate (t, o2);
        const auto w = bk::sfgr::write ("build/tests/prueba-cola.sfgr", "cola", t, o2);
        // −4 neper/s ≈ −34,7 dB/s: −70 dB llega hacia los 2 s (con 50 ms de margen).
        check (w.ok && w.ramBytes < w.ramBytesBeforeTrim / 2 && std::abs (double (e.ramBytes) - double (w.ramBytes)) < 0.02 * w.ramBytes,
               "recorte de colas (−70 dB) y la estimación coincide");
        std::printf ("   cola: %.2f s guardados de 6 s\n", double (w.ramBytes) / 2 / 48000);
    }
    // Sin caída no hay bloque de modulación: bytes 144..159 en cero, el archivo de v1 de siempre.
    {
        check (u64 (d, 144) == 0 && u32 (d, 152) == 0 && u32 (d, 156) == 0, "sin caída: sin bloque de modulación");
    }
    // Caída por zona, zona de soltado y release por zona (pianos Nord; BeTracks Pro con el cambio 008).
    {
        bk::sfgr::ExportOptions o3;
        o3.trimDb = 0;
        bk::sfgr::ZoneExtra e0;
        e0.preLoopDecayDbPerSec = 12.5f; e0.loopDecayDbPerSec = 3.2f; e0.releaseSec = 0.4f;
        bk::sfgr::ZoneExtra e1;
        e1.releaseTrigger = true; e1.rtDecayDbPerSec = 4.0f; e1.volumeDb = -10.0f;
        o3.zoneExtras = { e0, e1 };
        const auto w = bk::sfgr::write ("build/tests/prueba-caida.sfgr", "caida", s, o3);
        std::ifstream g ("build/tests/prueba-caida.sfgr", std::ios::binary);
        std::vector<uint8_t> c ((std::istreambuf_iterator<char> (g)), {});
        const uint64_t mo = u64 (c, 144);
        const uint32_t mb = u32 (c, 152);
        check (w.ok && u64 (c, 16) == c.size() && mo == u64 (c, 56) + u64 (c, 64) && mo + mb == c.size() && mb == 16 + 2 * 16,
               "caída: bloque detrás del PCM, dentro de fileSize, 16 bytes por zona");
        check (mb >= 16 && bk::sfgr::crc32 (c.data() + mo, mb) == u32 (c, 156) && u32 (c, size_t (mo)) == 0x584F444Du
                   && c[mo + 4] == 16 && u32 (c, size_t (mo) + 8) == 2, "caída: CRC, magia MODX, registro de 16, 2 zonas");
        auto u16 = [&] (size_t at) { return uint16_t (c[at] | (c[at + 1] << 8)); };
        const size_t r0 = size_t (mo) + 16, r1 = r0 + 16;
        check (c[r0] == 255 && c[r0 + 6] == 255 && u16 (r0 + 12) == 1250 && u16 (r0 + 14) == 320,
               "zona 0: sin CC (255) y caída 12,50 / 3,20 dB/s en centésimas");
        // La zona 1 toca el golpe sin bucle: caída dentro del bucle 0 aunque se pida.
        check (u16 (r1 + 12) == 0 && u16 (r1 + 14) == 0, "zona 1 (sin bucle, sin caída): ceros");
        const size_t z = size_t (u64 (c, 40));
        check (std::abs (f32 (c, z + 52) - 0.4f) < 1e-6f && c[z + 14] == 0, "zona 0: release propio, de ataque");
        check (c[z + 56 + 14] == 1 && c[z + 56 + 15] == 40 && std::abs (f32 (c, z + 56 + 16) - (-10.0f)) < 0.01f,
               "zona 1: de soltado, rtDecay 4,0 dB/s, −10 dB");
    }
    // Un Nord Sample con bucle que cae: la caída del bucle viaja sola, sin zoneExtras.
    {
        bk::nsmp::SampleSound t = s;
        t.strokes[0].loopDecayDbPerSecond = 6.0;
        bk::sfgr::ExportOptions o4;
        o4.trimDb = 0;
        const auto w = bk::sfgr::write ("build/tests/prueba-nsmp-caida.sfgr", "nsmp", t, o4);
        std::ifstream g ("build/tests/prueba-nsmp-caida.sfgr", std::ios::binary);
        std::vector<uint8_t> c ((std::istreambuf_iterator<char> (g)), {});
        const size_t r0 = size_t (u64 (c, 144)) + 16;
        check (w.ok && u64 (c, 144) != 0 && (c[r0 + 14] | (c[r0 + 15] << 8)) == 600, "Nord Sample: caída del bucle 6 dB/s en el archivo");
    }
    return failures ? 1 : 0;
}
