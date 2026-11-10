#include "PianoEngine.h"

#include <algorithm>
#include <cmath>

namespace bk
{
namespace
{
constexpr double kSilence = 1.0e-4; // −80 dB: la voz se apaga
constexpr double kHeadroom = 0.35;  // margen para acordes (como OpenNord)
constexpr double kPcmScale = kHeadroom / 32767.0;

/** Muestra i del canal c, con el bucle desenrollado; 0 fuera del golpe. */
inline float sampleAt (const StrokeAudio& s, int channels, int64_t i, int c)
{
    if (i < 0) return 0.0f;
    if (s.loop && i >= int64_t (s.loopEnd))
        i = s.loopStart + (i - s.loopStart) % int64_t (s.loopEnd - s.loopStart);
    else if (i >= int64_t (s.frames))
        return 0.0f;
    return s.pcm[size_t (i) * size_t (channels) + size_t (c)];
}

/** Interpolación cúbica de Hermite (4 puntos). */
inline float hermite (float xm1, float x0, float x1, float x2, float t)
{
    const float c1 = 0.5f * (x1 - xm1);
    const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * t + c2) * t + c1) * t + x0;
}
} // namespace

void PianoEngine::prepare (double sampleRate)
{
    sampleRate_ = sampleRate > 0 ? sampleRate : 48000;
    envStep_ = npno::kEnvelopeRate / sampleRate_;
    killMul_ = std::exp (-1.0 / (0.002 * sampleRate_)); // 2 ms: nota repetida o robada
    meterFall_ = float (std::exp (-1.0 / (0.05 * sampleRate_)));
    allNotesOff (true);
}

void PianoEngine::setSound (const PianoSound* sound)
{
    for (auto& v : voices_) v.stage = Stage::Off;
    sound_ = sound;
}

PianoEngine::Voice& PianoEngine::allocate (const Voice* except)
{
    Voice* best = nullptr;
    for (auto& v : voices_)
        if (v.stage == Stage::Off && &v != except) return v;
    // Sin voces libres: la que se está apagando más antigua, si no la más antigua.
    for (auto& v : voices_)
        if (&v != except && v.stage != Stage::Held && (! best || v.age < best->age)) best = &v;
    if (! best)
        for (auto& v : voices_)
            if (&v != except && (! best || v.age < best->age)) best = &v;
    return *best;
}

void PianoEngine::noteOn (int key, int velocity)
{
    if (key < 0 || key > 127) return;
    keyDown_[size_t (key)] = true;
    if (! sound_ || velocity <= 0) return;
    const auto& meta = sound_->meta;
    const int played = std::clamp (key + params_.transpose, 0, 127);
    const int zone = meta.zoneForKey (played);
    if (zone < 0) return;

    // Dinámica: comprime la velocidad hacia 127 antes de la curva de toque.
    const int vel = std::clamp (int (std::lround (127.0 - (127.0 - velocity) * params_.dynamics)), 1, 127);
    // String Res: con el pedal pisado, la grabación hecha con el pedal pisado (si el piano la trae).
    int idx = -1;
    if (params_.stringRes && sustain_ && sound_->hasPedalDown)
    {
        idx = meta.strokeFor (zone, vel, params_.touchOffset, 1);
        if (idx >= 0 && sound_->strokes[size_t (idx)].pcm.empty()) idx = -1;
    }
    if (idx < 0) idx = meta.strokeFor (zone, vel, params_.touchOffset, 0);
    if (idx < 0) return;

    // Nota repetida: la anterior en esa tecla deja paso (su ruido de soltar tecla no).
    for (auto& v : voices_)
        if (v.key == key && ! v.noise && (v.stage == Stage::Held || v.stage == Stage::Released)) kill (v);

    const auto& s = sound_->strokes[size_t (idx)];
    const auto& z = meta.zones[size_t (zone)];
    Voice& v = allocate();
    v = Voice {};
    v.stage = Stage::Held;
    v.stroke = &s;
    v.key = key;
    v.zone = zone;
    v.velocity = vel;
    v.undamped = played >= meta.undampedFrom;
    const double semis = played - z.root - npno::kRootOffset + params_.tuneCents / 100.0;
    v.inc = std::pow (2.0, semis / 12.0) * npno::kSampleRate / sampleRate_;
    v.gain = npno::attenuationGain (npno::touchAttenuation (vel)) * kPcmScale;
    const int pre = std::clamp (z.root - played + 1, 0, 13);
    v.preLoopMul = std::pow (s.preLoop[size_t (pre)], envStep_);
    v.loopMul = std::pow (s.loopDecay, envStep_);
    v.age = ++counter_;
}

void PianoEngine::release (Voice& v)
{
    if (! sound_ || v.stage != Stage::Held) return;
    if (v.undamped) { v.stage = Stage::Released; v.releaseMul = 1.0; return; } // sin apagador: sigue sonando
    const auto& s = *v.stroke;
    int idx = params_.softRelease ? s.damperSoft : s.damper;
    const int perKey = sound_->meta.damperByKey[size_t (std::clamp (v.key + params_.transpose, 0, 127))];
    if (! params_.softRelease && perKey > 0) idx = perKey;
    if (idx <= 0) idx = 40; // archivo sin dato: unos 150 ms
    const double tau = std::max (0.002, npno::damperT60 (idx) * params_.releaseScale / 6.91);
    v.releaseMul = std::exp (-1.0 / (tau * sampleRate_));
    v.stage = Stage::Released;
    startReleaseNoise (v);
}

void PianoEngine::startReleaseNoise (const Voice& note)
{
    // El ruido del apagador al caer: la grabación de soltar tecla de la zona. Sigue el volumen que
    // tiene la cuerda ahora (no el del golpe): una nota que ya se ha apagado casi no hace ruido.
    // Se elige por esa atenuación y suena tantos dB por debajo como dice el archivo (+0x33 − suelo).
    if (note.noise || params_.releaseNoise <= 0.0f || ! sound_->hasReleases) return;
    // Cuánto ha bajado lo que suena desde el golpe (grabación + envolvente), medido en la voz.
    const double ratio = note.meterMax > 0 ? double (note.meter) / double (note.meterMax) : 0.0;
    const double decayAtt = ratio > 1.0e-6 ? -100.0 * std::log10 (std::min (1.0, ratio)) : 600.0;
    const double attNow = npno::touchAttenuation (note.velocity) + params_.touchOffset + decayAtt; // pasos de 0,2 dB
    if (attNow > 300.0) return; // la nota está a más de 60 dB: el ruido no se oiría
    const int idx = sound_->meta.strokeForAttenuation (note.zone, attNow, 2);
    if (idx < 0) return;
    const auto& s = sound_->strokes[size_t (idx)];
    if (s.pcm.empty()) return;
    const int below = s.partner > s.floor ? s.partner - s.floor : 50; // sin dato: unos 10 dB por debajo
    const double att = attNow + below;
    Voice& n = allocate (&note);
    n = Voice {};
    n.stage = Stage::Released;
    n.noise = true;
    n.stroke = &s;
    n.key = note.key;
    n.zone = note.zone;
    n.velocity = note.velocity;
    n.inc = note.inc;
    n.gain = npno::attenuationGain (att) * kPcmScale * params_.releaseNoise;
    n.age = ++counter_;
}

void PianoEngine::kill (Voice& v)
{
    if (v.stage == Stage::Off) return;
    v.stage = Stage::Killed;
    v.releaseMul = killMul_;
}

void PianoEngine::noteOff (int key)
{
    if (key < 0 || key > 127) return;
    keyDown_[size_t (key)] = false;
    for (auto& v : voices_)
        if (v.key == key && v.stage == Stage::Held)
        {
            if (sustain_) v.sustained = true;
            else release (v);
        }
}

void PianoEngine::setSustain (bool down)
{
    if (sustain_ == down) return;
    sustain_ = down;
    if (! down)
        for (auto& v : voices_)
            if (v.stage == Stage::Held && v.sustained && ! keyDown_[size_t (v.key)]) release (v);
}

void PianoEngine::allNotesOff (bool immediate)
{
    sustain_ = false;
    keyDown_.fill (false);
    for (auto& v : voices_)
    {
        if (immediate) v.stage = Stage::Off;
        else if (v.stage == Stage::Held) { release (v); if (v.undamped) kill (v); }
    }
}

void PianoEngine::render (float* left, float* right, int numFrames)
{
    if (! sound_) return;
    const int channels = sound_->channels;
    for (auto& v : voices_)
    {
        if (v.stage == Stage::Off) continue;
        const auto& s = *v.stroke;
        const double loopLen = s.loop ? double (s.loopEnd - s.loopStart) : 0.0;
        for (int n = 0; n < numFrames; ++n)
        {
            if (! s.loop && v.pos >= s.frames) { v.stage = Stage::Off; break; }
            const int64_t i = int64_t (v.pos);
            const float t = float (v.pos - double (i));
            const float g = float (v.level * v.gain);
            float l = hermite (sampleAt (s, channels, i - 1, 0), sampleAt (s, channels, i, 0),
                               sampleAt (s, channels, i + 1, 0), sampleAt (s, channels, i + 2, 0), t);
            float r = l;
            if (channels == 2)
                r = hermite (sampleAt (s, channels, i - 1, 1), sampleAt (s, channels, i, 1),
                             sampleAt (s, channels, i + 1, 1), sampleAt (s, channels, i + 2, 1), t);
            left[n] += l * g;
            const float a = (std::abs (l) + std::abs (r)) * 0.5f * float (v.level);
            v.meter = std::max (a, v.meter * meterFall_);
            v.meterMax = std::max (v.meterMax, v.meter);
            right[n] += r * g;

            // Envolvente: decaimiento antes del bucle o dentro de él, y el apagador si la tecla se soltó.
            v.level *= v.pos < s.loopStart || ! s.loop ? v.preLoopMul : v.loopMul;
            if (v.stage == Stage::Released || v.stage == Stage::Killed) v.level *= v.releaseMul;
            if (v.level < kSilence) { v.stage = Stage::Off; break; }

            v.pos += v.inc;
            if (s.loop && v.pos >= s.loopEnd) v.pos -= loopLen;
        }
    }
}

int PianoEngine::activeVoices() const
{
    int n = 0;
    for (const auto& v : voices_) n += v.stage != Stage::Off;
    return n;
}

std::array<uint64_t, 2> PianoEngine::soundingKeys() const
{
    std::array<uint64_t, 2> bits {};
    for (const auto& v : voices_)
        if (! v.noise && (v.stage == Stage::Held || v.stage == Stage::Released))
            bits[size_t (v.key / 64)] |= uint64_t (1) << (v.key % 64);
    return bits;
}
} // namespace bk
