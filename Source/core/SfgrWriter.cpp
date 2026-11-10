#include "SfgrWriter.h"

#include "FileUtf8.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace bk::sfgr
{
namespace
{
constexpr size_t kHeaderSize = 160, kZoneSize = 56, kSampleSize = 32;
constexpr float kTargetPeak = 0.891f;        // −1 dBFS

// ── Remuestreo de calidad (sinc con ventana de Kaiser, 64 puntos, 512 fases) ──────────────────
constexpr int kTaps = 64, kPhases = 512;

double besselI0 (double x)
{
    double s = 1, t = 1;
    for (int k = 1; k < 32; ++k) { t *= (x / (2 * k)) * (x / (2 * k)); s += t; }
    return s;
}

/** Tabla del núcleo para una frecuencia de corte (relativa a la entrada). */
std::vector<float> makeKernel (double cutoff)
{
    constexpr double beta = 8.6, kPi = 3.14159265358979323846;
    std::vector<float> k (size_t ((kPhases + 1) * kTaps));
    for (int p = 0; p <= kPhases; ++p)
        for (int t = 0; t < kTaps; ++t)
        {
            const double x = (t - kTaps / 2 + 1) - double (p) / kPhases;   // distancia en muestras de entrada
            const double sinc = std::abs (x) < 1e-9 ? 1.0 : std::sin (kPi * cutoff * x) / (kPi * cutoff * x);
            const double w = x / (kTaps / 2.0);
            const double win = std::abs (w) >= 1 ? 0.0 : besselI0 (beta * std::sqrt (1 - w * w)) / besselI0 (beta);
            k[size_t (p * kTaps + t)] = float (cutoff * sinc * win);
        }
    return k;
}

/** Un canal de `in` (frecuencia `from`) a `to`. */
std::vector<float> resample (const std::vector<float>& in, double from, double to, const std::vector<float>& kernel)
{
    if (std::abs (from - to) < 0.5) return in;
    const double ratio = to / from;
    const size_t outLen = size_t (std::floor ((in.size()) * ratio));
    std::vector<float> out (outLen);
    const int n = int (in.size());
    for (size_t j = 0; j < outLen; ++j)
    {
        const double pos = double (j) / ratio;
        const int base = int (std::floor (pos));
        const double frac = pos - base;
        const int phase = int (frac * kPhases);
        const float f = float (frac * kPhases - phase);
        const float* k0 = kernel.data() + size_t (phase * kTaps);
        const float* k1 = k0 + kTaps;
        double acc = 0;
        for (int t = 0; t < kTaps; ++t)
        {
            const int i = base + t - kTaps / 2 + 1;
            if (i < 0 || i >= n) continue;
            acc += double (in[size_t (i)]) * (k0[t] + (k1[t] - k0[t]) * f);
        }
        out[j] = float (acc);
    }
    return out;
}

void put16 (std::vector<uint8_t>& b, size_t at, uint16_t v) { b[at] = uint8_t (v); b[at + 1] = uint8_t (v >> 8); }
void put32 (std::vector<uint8_t>& b, size_t at, uint32_t v) { for (int i = 0; i < 4; ++i) b[at + size_t (i)] = uint8_t (v >> (8 * i)); }
void put64 (std::vector<uint8_t>& b, size_t at, uint64_t v) { for (int i = 0; i < 8; ++i) b[at + size_t (i)] = uint8_t (v >> (8 * i)); }
void putF (std::vector<uint8_t>& b, size_t at, float v) { uint32_t u; std::memcpy (&u, &v, 4); put32 (b, at, u); }

/** Un golpe ya convertido: canales separados en flotante a la frecuencia de salida. */
struct Prepared
{
    std::vector<std::vector<float>> ch;
    uint32_t frames = 0;
    bool loops = false;
    uint32_t loopStart = 0, loopEnd = 0;
};

float strokePeak (const bk::nsmp::SampleSound::Stroke& s)
{
    int m = 0;
    for (auto v : s.pcm) m = std::max (m, std::abs (int (v)));
    return float (m) / 32768.0f;
}

/** Frames que quedan tras recortar la cola de `p` por debajo de `thr` (lineal). */
uint32_t trimmedFrames (const Prepared& p, float thr, uint32_t sr)
{
    if (p.loops) return std::min<uint32_t> (p.frames, p.loopEnd + 8);
    if (thr <= 0) return p.frames;
    const uint32_t win = std::max<uint32_t> (64, sr / 100);
    uint32_t last = 0;
    for (uint32_t a = 0; a + win <= p.frames; a += win)
    {
        double e = 0;
        for (const auto& c : p.ch) for (uint32_t i = a; i < a + win; ++i) e += double (c[i]) * c[i];
        if (std::sqrt (e / (win * p.ch.size())) > thr) last = a + win;
    }
    return std::min (p.frames, last + sr / 20);   // 50 ms de margen
}

Prepared prepare (const bk::nsmp::SampleSound::Stroke& s, uint32_t outRate, float gain, std::vector<float>& kernelUp,
                  std::vector<float>& kernelDown, double& kernelDownCutoff)
{
    Prepared p;
    const int chans = std::max (1, s.channels);
    const double ratio = outRate / s.sampleRate;
    std::vector<float> one (s.frames);
    for (int c = 0; c < chans; ++c)
    {
        for (uint32_t f = 0; f < s.frames; ++f) one[f] = s.pcm[size_t (f) * size_t (chans) + size_t (c)] * (gain / 32768.0f);
        if (ratio >= 1.0 - 1e-9) p.ch.push_back (resample (one, s.sampleRate, outRate, kernelUp));
        else
        {
            // Bajar de frecuencia: el corte va a la nueva Nyquist (un núcleo por relación, se reutiliza).
            const double cutoff = ratio * 0.97;
            if (kernelDown.empty() || std::abs (kernelDownCutoff - cutoff) > 1e-6) { kernelDown = makeKernel (cutoff); kernelDownCutoff = cutoff; }
            p.ch.push_back (resample (one, s.sampleRate, outRate, kernelDown));
        }
    }
    p.frames = p.ch.empty() ? 0 : uint32_t (p.ch[0].size());
    if (s.loops && s.loopEnd > s.loopStart)
    {
        p.loopStart = uint32_t (std::lround (s.loopStart * ratio));
        p.loopEnd = std::min (p.frames, uint32_t (std::lround (s.loopEnd * ratio)));
        p.loops = p.loopEnd > p.loopStart + 16;
    }
    return p;
}

float globalPeak (const bk::nsmp::SampleSound& sound)
{
    float peak = 1.0e-6f;
    for (const auto& s : sound.strokes) peak = std::max (peak, strokePeak (s));
    return peak;
}

/** Recorrido común: prepara cada golpe y llama a `sink` con él ya recortado. */
template <typename Sink>
bool forEachPrepared (const bk::nsmp::SampleSound& sound, const ExportOptions& o, Sink&& sink, std::string& error)
{
    if (sound.strokes.empty() || sound.zones.empty()) { error = "No hay nada que exportar."; return false; }
    const float gain = kTargetPeak / globalPeak (sound);
    const float thr = o.trimDb < 0 ? kTargetPeak * float (std::pow (10.0, o.trimDb / 20.0)) : 0.0f;
    auto kernelUp = makeKernel (0.97);
    std::vector<float> kernelDown;
    double kernelDownCutoff = 0;
    for (size_t i = 0; i < sound.strokes.size(); ++i)
    {
        auto p = prepare (sound.strokes[i], o.sampleRate, gain, kernelUp, kernelDown, kernelDownCutoff);
        if (p.frames == 0) { error = "Muestra vacía (" + std::to_string (i) + ")."; return false; }
        const uint32_t before = p.frames;
        const uint32_t keep = std::max<uint32_t> (1, trimmedFrames (p, thr, o.sampleRate));
        if (keep < p.frames)
        {
            for (auto& c : p.ch) c.resize (keep);
            p.frames = keep;
            if (! p.loops)
            {
                const uint32_t fade = std::min<uint32_t> (keep, o.sampleRate / 100);
                for (auto& c : p.ch) for (uint32_t k = 0; k < fade; ++k) c[keep - fade + k] *= 1.0f - float (k + 1) / float (fade);
            }
        }
        if (! sink (i, p, before)) return false;
    }
    return true;
}
} // namespace

uint32_t crc32 (const void* data, size_t len, uint32_t seed)
{
    static const auto table = []
    {
        std::array<uint32_t, 256> t {};
        for (uint32_t i = 0; i < 256; ++i)
        {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            t[i] = c;
        }
        return t;
    }();
    uint32_t crc = ~seed;
    const auto* p = static_cast<const uint8_t*> (data);
    for (size_t i = 0; i < len; ++i) crc = table[(crc ^ p[i]) & 0xFFu] ^ (crc >> 8);
    return ~crc;
}

ExportResult estimate (const bk::nsmp::SampleSound& sound, const ExportOptions& o)
{
    // Sin remuestrear: frames escalados y el recorte medido sobre la muestra original (a ±1 %).
    ExportResult r;
    if (sound.strokes.empty() || sound.zones.empty()) { r.error = "No hay nada que exportar."; return r; }
    const float gain = kTargetPeak / globalPeak (sound);
    const float thr = o.trimDb < 0 ? kTargetPeak * float (std::pow (10.0, o.trimDb / 20.0)) : 0.0f;
    for (const auto& s : sound.strokes)
    {
        const double ratio = o.sampleRate / s.sampleRate;
        Prepared p;
        p.frames = s.frames;
        p.loops = s.loops && s.loopEnd > s.loopStart;
        p.loopEnd = s.loopEnd;
        p.ch.assign (size_t (std::max (1, s.channels)), std::vector<float> (s.frames));
        for (uint32_t f = 0; f < s.frames; ++f)
            for (int c = 0; c < s.channels; ++c) p.ch[size_t (c)][f] = s.pcm[size_t (f) * size_t (s.channels) + size_t (c)] * (gain / 32768.0f);
        const uint32_t keep = trimmedFrames (p, thr, uint32_t (s.sampleRate));
        const uint64_t bytesBefore = uint64_t (std::llround (s.frames * ratio)) * uint64_t (std::max (1, s.channels)) * 2;
        const uint64_t bytes = uint64_t (std::llround (keep * ratio)) * uint64_t (std::max (1, s.channels)) * 2;
        r.ramBytesBeforeTrim += bytesBefore;
        r.ramBytes += bytes;
    }
    r.zones = int (sound.zones.size());
    r.samples = int (sound.strokes.size());
    r.fileBytes = kHeaderSize + r.zones * kZoneSize + r.samples * kSampleSize + r.ramBytes;
    r.ok = true;
    return r;
}

ExportResult write (const std::string& path, const std::string& name, const bk::nsmp::SampleSound& sound, const ExportOptions& o)
{
    ExportResult r;
    const size_t zoneCount = sound.zones.size(), sampleCount = sound.strokes.size();
    if (zoneCount == 0 || zoneCount > 65536 || sampleCount == 0 || sampleCount > 65536)
    {
        r.error = "Número de zonas o muestras fuera del rango de .sfgr (1..65536).";
        return r;
    }
    const std::string tmp = path + ".part";
    FILE* f = bk::io::openFile (tmp, "wb");
    if (f == nullptr) { r.error = "No se puede escribir " + path; return r; }

    const uint64_t zoneOff = kHeaderSize, sampleOff = zoneOff + zoneCount * kZoneSize;
    const uint64_t payloadOff = sampleOff + sampleCount * kSampleSize;   // múltiplo de 8
    // Cabecera y tablas provisionales; se reescriben al final con los números de verdad.
    std::vector<uint8_t> zero (size_t (payloadOff), 0);
    std::fwrite (zero.data(), 1, zero.size(), f);

    std::vector<uint8_t> samples (sampleCount * kSampleSize, 0);
    uint64_t payloadBytes = 0;
    uint32_t payloadCrc = 0;
    std::vector<uint32_t> loopStart (sampleCount), loopEnd (sampleCount);
    std::vector<uint8_t> loops (sampleCount, 0);
    std::vector<int16_t> pcm;
    bool ioError = false;
    std::string err;
    const bool ok = forEachPrepared (sound, o, [&] (size_t i, const Prepared& p, uint32_t before)
    {
        const uint32_t chans = uint32_t (p.ch.size());
        pcm.resize (size_t (p.frames) * chans);
        for (uint32_t fr = 0; fr < p.frames; ++fr)
            for (uint32_t c = 0; c < chans; ++c)
                pcm[size_t (fr) * chans + c] = int16_t (std::lround (std::clamp (p.ch[c][fr], -1.0f, 32767.0f / 32768.0f) * 32768.0f));
        const size_t bytes = pcm.size() * 2;
        const uint32_t crc = crc32 (pcm.data(), bytes);
        if (std::fwrite (pcm.data(), 1, bytes, f) != bytes) { ioError = true; return false; }
        payloadCrc = crc32 (pcm.data(), bytes, payloadCrc);
        const size_t at = i * kSampleSize;
        put64 (samples, at, payloadBytes);
        put64 (samples, at + 8, p.frames);
        put32 (samples, at + 16, chans);
        put32 (samples, at + 20, o.sampleRate);
        put32 (samples, at + 24, crc);
        payloadBytes += bytes;
        // Cada muestra empieza alineada a 4 bytes.
        if (payloadBytes % 4 != 0)
        {
            const uint8_t pad[2] = { 0, 0 };
            std::fwrite (pad, 1, 2, f);
            payloadCrc = crc32 (pad, 2, payloadCrc);
            payloadBytes += 2;
        }
        loops[i] = p.loops ? 1 : 0;
        loopStart[i] = p.loopStart;
        loopEnd[i] = p.loopEnd;
        r.ramBytesBeforeTrim += uint64_t (before) * chans * 2;
        return true;
    }, err);
    if (! ok || ioError)
    {
        std::fclose (f);
        bk::io::removeFile (tmp);
        r.error = ioError ? "Error al escribir el archivo." : err;
        return r;
    }

    if (! o.zoneExtras.empty() && o.zoneExtras.size() != zoneCount)
    {
        std::fclose (f);
        bk::io::removeFile (tmp);
        r.error = "zoneExtras no cuadra con las zonas.";
        return r;
    }
    std::vector<uint8_t> zones (zoneCount * kZoneSize, 0);
    std::vector<uint16_t> decayPre (zoneCount, 0), decayLoop (zoneCount, 0);
    bool anyDecay = false;
    auto centi = [] (double dbPerSec) { return uint16_t (std::clamp (std::lround (dbPerSec * 100.0), 0L, 65535L)); };
    for (size_t z = 0; z < zoneCount; ++z)
    {
        const auto& src = sound.zones[z];
        const ZoneExtra ex = o.zoneExtras.empty() ? ZoneExtra {} : o.zoneExtras[z];
        const size_t s = size_t (std::clamp (src.stroke, 0, int (sampleCount) - 1));
        const size_t at = z * kZoneSize;
        zones[at + 0] = uint8_t (std::clamp (std::min (src.low, src.high), 0, 127));
        zones[at + 1] = uint8_t (std::clamp (std::max (src.low, src.high), 0, 127));
        zones[at + 2] = uint8_t (std::clamp (std::min (src.velLow, src.velHigh), 0, 127));
        zones[at + 3] = uint8_t (std::clamp (std::max (src.velLow, src.velHigh), 0, 127));
        put16 (zones, at + 4, uint16_t (int16_t (std::clamp (src.root, 0, 127))));
        // 6 transpose, 8 tune, 10 group, 12 offBy: cero. 14 trigger y 15 rtDecay: solo zonas de soltado.
        zones[at + 14] = ex.releaseTrigger ? 1 : 0;
        zones[at + 15] = ex.releaseTrigger ? uint8_t (std::clamp (std::lround (ex.rtDecayDbPerSec * 10.0f), 0L, 255L)) : 0;
        // Capas de velocidad: que a su velocidad de grabación suene a su nivel natural en BeTracks Pro.
        const float volumeDb = (src.velRoot > 0 ? float (o.touchCurve * 20.0 * std::log10 (127.0 / src.velRoot)) : 0.0f) + ex.volumeDb;
        putF (zones, at + 16, volumeDb);
        putF (zones, at + 20, 0.0f);
        put32 (zones, at + 24, uint32_t (s));
        zones[at + 28] = loops[s];
        // 29 seqPack, 30 loRand, 31 hiRand: cero (sin round robin).
        put32 (zones, at + 32, loops[s] ? loopStart[s] : 0);
        put32 (zones, at + 36, loops[s] ? loopEnd[s] : 0);
        putF (zones, at + 40, std::max (0.001f, o.attackSec));
        putF (zones, at + 44, 0.0f);
        putF (zones, at + 48, 1.0f);
        // Un "one-shot" (suena entero aunque se suelte la tecla) se escribe con un release largo.
        const bool oneShot = src.oneShot || (! loops[s] && ! sound.releaseUnlooped);
        const float release = ex.releaseSec >= 0 ? ex.releaseSec : (oneShot ? 60.0f : o.releaseSec);
        putF (zones, at + 52, std::clamp (release, 0.005f, 60.0f));

        // Caída: la de la zona, o la del bucle del sample (Nord Samples con bucles que se apagan).
        double loopDb = ex.loopDecayDbPerSec;
        if (loopDb <= 0 && loops[s]) loopDb = sound.strokes[s].loopDecayDbPerSecond;
        decayPre[z] = centi (ex.preLoopDecayDbPerSec > 0 ? double (ex.preLoopDecayDbPerSec) : sound.strokes[s].preLoopDecayDbPerSecond);
        decayLoop[z] = loops[s] ? centi (loopDb) : 0;
        anyDecay = anyDecay || decayPre[z] != 0 || decayLoop[z] != 0;
    }
    const uint32_t tableCrc = crc32 (samples.data(), samples.size(), crc32 (zones.data(), zones.size()));
    uint64_t fileSize = payloadOff + payloadBytes;

    // Bloque de modulación con la caída, DETRÁS del PCM (fileSize lo incluye). Registro de 16 bytes por zona:
    // los 12 de la modulación por CC, neutros (CC 255 = ninguno), y la caída antes y dentro del bucle en
    // centésimas de dB/s.
    std::vector<uint8_t> mod;
    if (anyDecay)
    {
        constexpr size_t kRec = 16;
        mod.assign (16 + zoneCount * kRec, 0);
        put32 (mod, 0, 0x584F444Du);           // "MODX"
        put16 (mod, 4, uint16_t (kRec));
        put16 (mod, 6, 0);                     // sin valores iniciales de CC
        put32 (mod, 8, uint32_t (zoneCount));
        for (size_t z = 0; z < zoneCount; ++z)
        {
            const size_t at = 16 + z * kRec;
            mod[at + 0] = 255;                 // xfCC: ninguno
            mod[at + 6] = 255;                 // gainCC: ninguno
            put16 (mod, at + 12, decayPre[z]);
            put16 (mod, at + 14, decayLoop[z]);
        }
    }

    std::vector<uint8_t> h (kHeaderSize, 0);
    std::memcpy (h.data(), "APNO", 4);
    put32 (h, 4, 1);                    // versión
    put32 (h, 8, 0x01020304u);          // comprobación de endianness
    put32 (h, 12, uint32_t (kHeaderSize));
    put64 (h, 16, fileSize);
    put32 (h, 24, o.sampleRate);
    put32 (h, 28, uint32_t (zoneCount));
    put32 (h, 32, uint32_t (sampleCount));
    put32 (h, 36, 1);                   // PCM16
    put64 (h, 40, zoneOff);
    put64 (h, 48, sampleOff);
    put64 (h, 56, payloadOff);
    put64 (h, 64, payloadBytes);
    put32 (h, 72, tableCrc);
    put32 (h, 76, payloadCrc);
    // Nombre: hasta 63 bytes sin partir un carácter UTF-8.
    size_t n = std::min<size_t> (name.size(), 63);
    while (n > 0 && n < name.size() && (uint8_t (name[n]) & 0xC0) == 0x80) --n;
    std::memcpy (h.data() + 80, name.data(), n);
    bool okWrite = true;
    if (! mod.empty())
    {
        // El archivo está justo detrás del PCM.
        okWrite = std::fwrite (mod.data(), 1, mod.size(), f) == mod.size();
        put64 (h, 144, fileSize);
        put32 (h, 152, uint32_t (mod.size()));
        put32 (h, 156, crc32 (mod.data(), mod.size()));
        fileSize += mod.size();
        put64 (h, 16, fileSize);
    }
    // Sin caída, 144..159 quedan en cero: sin bloque, el archivo de v1 de siempre.

    std::fseek (f, 0, SEEK_SET);
    okWrite = okWrite && std::fwrite (h.data(), 1, h.size(), f) == h.size();
    okWrite = okWrite && std::fwrite (zones.data(), 1, zones.size(), f) == zones.size();
    okWrite = okWrite && std::fwrite (samples.data(), 1, samples.size(), f) == samples.size();
    okWrite = (std::fclose (f) == 0) && okWrite;
    if (! okWrite) { bk::io::removeFile (tmp); r.error = "Error al escribir el archivo."; return r; }
    bk::io::removeFile (path);
    if (bk::io::renameFile (tmp, path) != 0) { bk::io::removeFile (tmp); r.error = "No se pudo guardar " + path; return r; }

    r.ok = true;
    r.fileBytes = fileSize;
    r.ramBytes = payloadBytes;
    r.zones = int (zoneCount);
    r.samples = int (sampleCount);
    return r;
}
} // namespace bk::sfgr
