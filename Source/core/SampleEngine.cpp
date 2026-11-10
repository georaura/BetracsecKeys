#include "SampleEngine.h"

#include "FileUtf8.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <map>

namespace bk::nsmp
{
size_t SampleSound::memoryBytes() const
{
    size_t n = 0;
    for (const auto& s : strokes) n += s.pcm.size() * sizeof (int16_t);
    return n;
}

bool SampleSound::stereo() const
{
    return std::any_of (strokes.begin(), strokes.end(), [] (const Stroke& s) { return s.channels == 2; });
}

std::unique_ptr<SampleSound> loadSample (const std::string& path, std::string& error)
{
    auto f = bk::io::openRead (path);
    if (! f) { error = "No se pudo abrir el archivo."; return nullptr; }
    const std::vector<uint8_t> bytes ((std::istreambuf_iterator<char> (f)), {});
    auto decoded = decode (bytes.data(), bytes.size(), error);
    if (! decoded) return nullptr;
    auto s = std::make_unique<SampleSound>();
    s->path = path;
    s->name = decoded->name;
    s->codec = decoded->codec;
    std::map<uint32_t, int> index;
    for (auto& st : decoded->strokes)
    {
        SampleSound::Stroke out;
        out.frames = st.frames;
        out.channels = st.channels;
        out.sampleRate = st.sampleRate;
        out.loops = st.loops && st.loopEnd > st.loopStart + 4;
        out.loopStart = st.loopStart;
        out.loopEnd = st.frames;
        out.loopDecayDbPerSecond = st.loopDecayDbPerSecond;
        // Los de fábrica caben en 16 bits (pico 8192); si alguno no, se escala para no recortar.
        int32_t peak = 1;
        for (auto x : st.pcm) peak = std::max (peak, x < 0 ? -x : x);
        const double g = peak > 32767 ? 32767.0 / peak : 1.0;
        out.pcm.resize (st.pcm.size());
        for (size_t i = 0; i < st.pcm.size(); ++i) out.pcm[i] = int16_t (std::lround (st.pcm[i] * g));
        index[st.id] = int (s->strokes.size());
        s->strokes.push_back (std::move (out));
    }
    for (const auto& z : decoded->zones)
    {
        const auto it = index.find (z.strokeId);
        if (it == index.end()) continue;
        s->zones.push_back ({ z.root, std::min (z.low, z.high), std::max (z.low, z.high), z.velLow, z.velHigh, it->second, z.oneShot });
    }
    if (s->zones.empty()) { error = "El sample no tiene zonas que se puedan tocar."; return nullptr; }
    return s;
}

namespace
{
constexpr double kScale = 0.35 / 8192.0; // pico de fábrica 8192, con margen para acordes
constexpr float kSilence = 1.0e-4f;

inline float at (const SampleSound::Stroke& s, int64_t i, int c)
{
    if (i < 0) return 0.0f;
    if (s.loops && i >= int64_t (s.loopEnd)) i = s.loopStart + (i - s.loopStart) % int64_t (s.loopEnd - s.loopStart);
    else if (i >= int64_t (s.frames)) return 0.0f;
    return s.pcm[size_t (i) * size_t (s.channels) + size_t (c)];
}

inline float hermite (float xm1, float x0, float x1, float x2, float t)
{
    const float c1 = 0.5f * (x1 - xm1);
    const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * t + c2) * t + c1) * t + x0;
}
} // namespace

void SampleEngine::prepare (double sampleRate)
{
    sr_ = sampleRate > 0 ? sampleRate : 48000;
    allNotesOff (true);
}

void SampleEngine::setSound (const SampleSound* s)
{
    for (auto& v : voices_) v.stage = Stage::Off;
    sound_ = s;
}

SampleEngine::Voice& SampleEngine::allocate()
{
    for (auto& v : voices_) if (v.stage == Stage::Off) return v;
    Voice* best = nullptr;
    for (auto& v : voices_) if ((v.stage == Stage::Release || v.stage == Stage::Kill) && (! best || v.age < best->age)) best = &v;
    if (! best) for (auto& v : voices_) if (! best || v.age < best->age) best = &v;
    return *best;
}

void SampleEngine::noteOn (int key, int velocity)
{
    if (key < 0 || key > 127) return;
    keyDown_[size_t (key)] = true;
    if (! sound_ || velocity <= 0) return;
    const int played = std::clamp (key + 12 * params_.octaveShift, 0, 127);
    const SampleSound::Zone* zone = nullptr;
    for (const auto& z : sound_->zones)
        if (played >= z.low && played <= z.high && velocity >= z.velLow && velocity <= z.velHigh) { zone = &z; break; }
    if (! zone) // sin capa para esa velocidad: la zona de la tecla más cercana en velocidad
        for (const auto& z : sound_->zones)
            if (played >= z.low && played <= z.high) { zone = &z; break; }
    if (! zone) return;

    // Nota repetida: la anterior deja paso.
    for (auto& v : voices_)
        if (v.key == key && v.stage != Stage::Off) { v.stage = Stage::Kill; v.releaseMul = float (std::exp (-1.0 / (0.003 * sr_))); }

    const auto& s = sound_->strokes[size_t (zone->stroke)];
    Voice& v = allocate();
    v = Voice {};
    v.stage = Stage::Attack;
    v.stroke = &s;
    v.key = key;
    v.oneShot = zone->oneShot || (! s.loops && ! sound_->releaseUnlooped);
    const double semis = played - zone->root + params_.tuneCents / 100.0;
    v.inc = s.sampleRate * std::pow (2.0, semis / 12.0) / sr_;
    // Velocidad: hasta ~36 dB entre la más suave y la más fuerte, según la sensibilidad. En una capa grabada a una
    // velocidad conocida, solo la diferencia con esa velocidad (la dinámica entre capas ya está en la grabación).
    const double db = zone->velRoot > 0 ? std::clamp (-36.0 * params_.velocity * (zone->velRoot - velocity) / 127.0, -36.0, 6.0)
                                        : -36.0 * params_.velocity * (1.0 - velocity / 127.0);
    v.gain = float (std::pow (10.0, db / 20.0) * kScale);
    v.env = params_.attackSec > 0.001f ? 0.0f : 1.0f;
    v.attackStep = params_.attackSec > 0.001f ? float (1.0 / (params_.attackSec * sr_)) : 1.0f;
    v.loopMul = s.loops && s.loopDecayDbPerSecond > 0 ? std::pow (10.0, -s.loopDecayDbPerSecond / 20.0 / sr_) : 1.0;
    v.age = ++counter_;
}

void SampleEngine::release (Voice& v)
{
    if (v.stage == Stage::Off || v.stage == Stage::Release || v.stage == Stage::Kill) return;
    v.stage = Stage::Release;
    v.releaseMul = float (std::exp (-1.0 / (std::max (0.005f, params_.releaseSec) / 6.91 * sr_)));
}

void SampleEngine::noteOff (int key)
{
    if (key < 0 || key > 127) return;
    keyDown_[size_t (key)] = false;
    for (auto& v : voices_)
    {
        if (v.key != key || v.stage == Stage::Off || v.stage == Stage::Release || v.stage == Stage::Kill) continue;
        if (sustain_) v.sustained = true;
        else if (! v.oneShot) release (v); // un one-shot suena entero
    }
}

void SampleEngine::setSustain (bool down)
{
    sustain_ = down;
    if (! down)
        for (auto& v : voices_)
            if (v.sustained && ! keyDown_[size_t (v.key)]) { v.sustained = false; if (! v.oneShot) release (v); }
}

void SampleEngine::allNotesOff (bool immediate)
{
    sustain_ = false;
    keyDown_.fill (false);
    for (auto& v : voices_)
    {
        if (immediate) v.stage = Stage::Off;
        else if (v.stage != Stage::Off) { v.oneShot = false; release (v); }
    }
}

void SampleEngine::render (float* left, float* right, int frames)
{
    if (! sound_) return;
    for (auto& v : voices_)
    {
        if (v.stage == Stage::Off) continue;
        const auto& s = *v.stroke;
        const double loopLen = s.loops ? double (s.loopEnd - s.loopStart) : 0.0;
        for (int n = 0; n < frames; ++n)
        {
            if (! s.loops && v.pos >= s.frames) { v.stage = Stage::Off; break; }
            const int64_t i = int64_t (v.pos);
            const float t = float (v.pos - double (i));
            const float l = hermite (at (s, i - 1, 0), at (s, i, 0), at (s, i + 1, 0), at (s, i + 2, 0), t);
            const float r = s.channels == 2 ? hermite (at (s, i - 1, 1), at (s, i, 1), at (s, i + 1, 1), at (s, i + 2, 1), t) : l;
            if (v.stage == Stage::Attack) { v.env += v.attackStep; if (v.env >= 1) { v.env = 1; v.stage = Stage::Hold; } }
            else if (v.stage == Stage::Release || v.stage == Stage::Kill) v.env *= v.releaseMul;
            if (s.loops && v.pos >= s.loopStart) v.loopLevel *= v.loopMul;
            const float g = v.gain * v.env * float (v.loopLevel);
            left[n] += l * g;
            right[n] += r * g;
            if ((v.stage == Stage::Release || v.stage == Stage::Kill) && v.env < kSilence) { v.stage = Stage::Off; break; }
            if (v.loopLevel < kSilence) { v.stage = Stage::Off; break; }
            v.pos += v.inc;
            if (s.loops && v.pos >= s.loopEnd) v.pos -= loopLen;
        }
    }
}

int SampleEngine::activeVoices() const
{
    int n = 0;
    for (const auto& v : voices_) n += v.stage != Stage::Off;
    return n;
}
} // namespace bk::nsmp
