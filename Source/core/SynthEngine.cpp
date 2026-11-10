#include "SynthEngine.h"

#include <algorithm>
#include <cmath>

namespace bk::synth
{
namespace
{
constexpr double kPi = 3.14159265358979323846, kTwoPi = 2 * kPi;
constexpr int kControl = 16; // muestras por actualización de modulación
// Nivel por voz: deja margen para acordes de 6 notas sin llegar al limitador (con 0,16 un tercio de las
// capas de fábrica lo pasaban y se oía intermodulación al tocar varias teclas).
constexpr double kVoiceLevel = 0.08;

double clamp01 (double v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

/** Corrección polyBLEP de un salto en la fase t (0..1) con incremento dt. */
double blep (double t, double dt)
{
    if (t < dt) { t /= dt; return t + t - t * t - 1.0; }
    if (t > 1.0 - dt) { t = (t - 1.0) / dt; return t * t + t + t + 1.0; }
    return 0.0;
}
double saw (double ph, double dt) { return 2.0 * ph - 1.0 - blep (ph, dt); }
double pulse (double ph, double dt, double pw)
{
    double s = ph < pw ? 1.0 : -1.0;
    s += blep (ph, dt);
    double t2 = ph - pw; if (t2 < 0) t2 += 1.0;
    s -= blep (t2, dt);
    return s;
}
double tri (double ph) { return 1.0 - 4.0 * std::abs (ph - 0.5); }
double sine (double ph) { return std::sin (kTwoPi * ph); }

bool advance (double& ph, double inc)
{
    ph += inc;
    if (ph >= 1.0) { ph -= std::floor (ph); return true; }
    return false;
}

uint32_t xorshift (uint32_t& s) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
double white (uint32_t& s) { return double (xorshift (s)) / 2147483648.0 - 1.0; }
} // namespace

// ── Escalas (synth-scaling.ts) ──────────────────────────────────────────────────────────────
double cutoffSemitones (double raw)
{
    const double v = std::clamp (raw, 0.0, 127.0);
    if (v >= 127) return 106;
    if (v > 117) return 65 + 1.7 * (v - 117);
    return v - 52;
}
double cutoffHz (double raw) { return 277.1826 * std::pow (2.0, cutoffSemitones (raw) / 12.0); }
double resonanceDamping (double raw)
{
    const double x = 1.0 - std::round (std::clamp (raw, 0.0, 127.0)) / 128.0;
    return 2.0 * x * x;
}
double attackSeconds (double raw) { return 0.0005 * std::pow (1.0 + std::clamp (raw, 0.0, 127.0) / 40.2, 8.0); }
double decaySeconds (double raw, bool isDecay)
{
    const double v = std::clamp (raw, 0.0, 127.0);
    if (isDecay && v >= 127) return INFINITY;
    return 0.003 * std::pow (1.0 + v / 54.1, 7.958);
}
double lfoRateHz (double raw) { return 0.02846 * std::pow (2.0, std::clamp (raw, 0.0, 127.0) / 8.966); }
double depthCurveSemitones (double raw)
{
    static const double L[][2] = { { 20, 0.5 }, { 21, 0.53 }, { 23, 0.66 }, { 25, 0.82 }, { 29, 1.2 }, { 36, 2 }, { 40, 2.7 },
        { 42, 3 }, { 48, 4.2 }, { 50, 4.8 }, { 52, 5.2 }, { 54, 5.8 }, { 56, 6.2 }, { 60, 7.3 }, { 64, 8.7 }, { 68, 10 },
        { 69, 10.6 }, { 73, 12 }, { 83, 16.5 }, { 95, 23 }, { 99, 26 }, { 107, 31 }, { 109, 33 }, { 111, 34 }, { 114, 37 },
        { 116, 38 }, { 120, 42 }, { 122, 43 }, { 127, 48 } };
    const double v = std::clamp (raw, 0.0, 127.0);
    if (v <= 20) return 0.5 * std::pow (v / 20.0, 2.5);
    for (size_t i = 1; i < std::size (L); ++i)
        if (v <= L[i][0]) return L[i - 1][1] + (L[i][1] - L[i - 1][1]) * (v - L[i - 1][0]) / (L[i][0] - L[i - 1][0]);
    return 48;
}
double fmHarmonicRatio (int w) { return w <= 0 ? 0.5 : double (std::min (w, 24)); }
double fmInharmonicRatio (int w) { return std::pow (2.0, (std::clamp (w, 0, 60) - 12) / 12.0); }
float softLimit (float x)
{
    const float a = std::abs (x);
    constexpr float t = 0.85f;
    if (a <= t) return x;
    return std::copysign (t + (1.0f - t) * std::tanh ((a - t) / (1.0f - t)), x);
}

// ── Envolventes: subida lineal; caída y liberación exponenciales que bajan 40 dB en el tiempo mostrado ──
void SynthEngine::Env::start (double attackSec, double decaySec, double sr)
{
    stage = Attack;
    attackStep = 1.0 / std::max (1.0, attackSec * sr);
    decayMul = std::isinf (decaySec) ? 1.0 : std::exp (-std::log (100.0) / std::max (1.0, decaySec * sr));
    if (attackSec <= 0.0006) { level = std::max (level, 0.999); }
}
void SynthEngine::Env::release (double releaseSec, double sr)
{
    if (stage == Off) return;
    stage = Release;
    releaseMul = std::exp (-std::log (100.0) / std::max (1.0, releaseSec * sr));
}
double SynthEngine::Env::next()
{
    switch (stage)
    {
        case Attack: level += attackStep; if (level >= 1.0) { level = 1.0; stage = Decay; } break;
        case Decay: level *= decayMul; break;
        case Release: level *= releaseMul; if (level < 1.0e-5) { level = 0; stage = Off; } break;
        case Off: break;
    }
    return level;
}

void SynthEngine::prepare (double sampleRate)
{
    sr_ = sampleRate > 0 ? sampleRate : 48000;
    allNotesOff (true);
}

void SynthEngine::setSample (const bk::nsmp::SampleSound* s)
{
    for (auto& v : voices_) if (v.stroke) v.active = false;
    sample_ = s;
}

SynthEngine::Voice& SynthEngine::allocate()
{
    for (auto& v : voices_) if (! v.active) return v;
    Voice* best = nullptr;
    for (auto& v : voices_) if (v.released && (! best || v.age < best->age)) best = &v;
    if (! best) for (auto& v : voices_) if (! best || v.age < best->age) best = &v;
    return *best;
}

void SynthEngine::startVoice (Voice& v, int key, int velocity, bool retrigger)
{
    const auto& p = params_;
    const double lastPitch = v.active ? v.pitch : -1;
    v.key = key;
    v.velocity = velocity;
    v.targetPitch = key;
    v.released = v.sustained = false;
    if (! retrigger && v.active) return; // legato: solo cambia la nota (con glide)
    v.pitch = p.glide > 0 && lastPitch >= 0 ? lastPitch : key;
    if (! v.active)
    {
        v.phase.fill (0);
        for (size_t i = 0; i < v.phase.size(); ++i) v.phase[i] = double ((v.age * 7919 + i * 104729) % 1000) / 1000.0;
        v.subPhase.fill (0);
        v.filter.reset();
        v.rng = 0x9e3779b9u ^ uint32_t (uint32_t (key) * 2654435761u + counter_);
    }
    v.amp.start (attackSeconds (p.ampA), decaySeconds (p.ampD, true), sr_);
    v.fenv.start (attackSeconds (p.fEnvA), decaySeconds (p.fEnvD, true), sr_);
    v.oenv.start (attackSeconds (p.oscEnvA), decaySeconds (p.oscEnvD, true), sr_);
    v.vibTime = 0;
    // Fuente "Sample": la zona de la tecla y la velocidad.
    v.stroke = nullptr;
    if (p.source == Source::Sample && sample_)
    {
        const bk::nsmp::SampleSound::Zone* zone = nullptr;
        for (const auto& z : sample_->zones)
            if (key >= z.low && key <= z.high && velocity >= z.velLow && velocity <= z.velHigh) { zone = &z; break; }
        if (! zone) for (const auto& z : sample_->zones) if (key >= z.low && key <= z.high) { zone = &z; break; }
        if (zone) { v.stroke = &sample_->strokes[size_t (zone->stroke)]; v.sampleRoot = zone->root; v.samplePos = 0; }
    }
    v.active = true;
    v.age = ++counter_;
}

void SynthEngine::noteOn (int key, int velocity)
{
    if (key < 0 || key > 127 || velocity <= 0) return;
    keyDown_[size_t (key)] = true;
    if (params_.mono)
    {
        if (monoCount_ < 128) monoStack_[size_t (monoCount_++)] = key;
        Voice& v = voices_[0];
        for (size_t i = 1; i < voices_.size(); ++i) voices_[i].active = false;
        const bool sounding = v.active && ! v.released;
        startVoice (v, key, velocity, ! (params_.legato && sounding));
        if (params_.glide <= 0 || ! sounding) v.pitch = key;
        return;
    }
    // Nota repetida: reutiliza su voz.
    for (auto& v : voices_)
        if (v.active && v.key == key) { startVoice (v, key, velocity, true); return; }
    double last = -1;
    uint64_t newest = 0;
    for (const auto& v : voices_) if (v.active && v.age > newest) { newest = v.age; last = v.pitch; }
    Voice& v = allocate();
    v.active = false;
    startVoice (v, key, velocity, true);
    if (params_.glide > 0 && last >= 0) v.pitch = last;
}

void SynthEngine::releaseVoice (Voice& v)
{
    if (! v.active || v.released) return;
    v.released = true;
    v.amp.release (decaySeconds (params_.ampR, false), sr_);
    v.fenv.release (decaySeconds (params_.fEnvR, false), sr_);
    v.oenv.release (decaySeconds (params_.oscEnvR, false), sr_);
}

void SynthEngine::noteOff (int key)
{
    if (key < 0 || key > 127) return;
    keyDown_[size_t (key)] = false;
    if (params_.mono)
    {
        int w = 0;
        for (int i = 0; i < monoCount_; ++i) if (monoStack_[size_t (i)] != key) monoStack_[size_t (w++)] = monoStack_[size_t (i)];
        monoCount_ = w;
        Voice& v = voices_[0];
        if (v.key != key) return;
        if (monoCount_ > 0) { startVoice (v, monoStack_[size_t (monoCount_ - 1)], v.velocity, ! params_.legato); return; }
        if (sustain_) v.sustained = true; else releaseVoice (v);
        return;
    }
    for (auto& v : voices_)
        if (v.active && v.key == key && ! v.released)
        {
            if (sustain_) v.sustained = true;
            else releaseVoice (v);
        }
}

void SynthEngine::setSustain (bool down)
{
    sustain_ = down;
    if (! down)
        for (auto& v : voices_)
            if (v.sustained && ! keyDown_[size_t (v.key)]) { v.sustained = false; releaseVoice (v); }
}

void SynthEngine::allNotesOff (bool immediate)
{
    sustain_ = false;
    keyDown_.fill (false);
    monoCount_ = 0;
    for (auto& v : voices_)
    {
        if (immediate) v.active = false;
        else releaseVoice (v);
    }
}

// ── Oscilador ────────────────────────────────────────────────────────────────────────────────
double SynthEngine::oscillator (Voice& v, int u, double f, double c, double& right)
{
    const auto& p = params_;
    const double dt = std::min (0.49, f / sr_);
    double out = 0;
    right = NAN; // mono salvo el sample estéreo
    auto& ph = v.phase[size_t (u)];
    switch (p.source)
    {
        case Source::FmH:
        case Source::FmI:
        {
            const double ratio = p.source == Source::FmH ? fmHarmonicRatio (p.wave) : fmInharmonicRatio (p.wave);
            const double index = c * 6.0;
            auto& mph = v.subPhase[size_t (u)];
            advance (mph, std::min (0.49, dt * ratio));
            out = std::sin (kTwoPi * ph + index * std::sin (kTwoPi * mph));
            advance (ph, dt);
            return out;
        }
        case Source::Wave:
        {
            // Las ondas WAVE del Stage 4 son tablas de su firmware: aquí, una aproximación con armónicos.
            const double bright = (p.wave % 7) / 7.0;
            out = 0.8 * sine (ph) + bright * 0.35 * saw (ph, dt) + 0.15 * (1 - bright) * sine (2 * ph);
            advance (ph, dt);
            return out;
        }
        case Source::Sample:
        {
            if (! v.stroke || u > 0) return 0;
            const auto& s = *v.stroke;
            // El golpe suena a su frecuencia en su raíz; a otra frecuencia, en proporción.
            const double rootHz = 440.0 * std::pow (2.0, (v.sampleRoot - 69) / 12.0);
            const double inc = s.sampleRate * f / rootHz / sr_;
            auto at = [&] (int64_t i, int ch) -> double
            {
                if (i < 0) return 0;
                if (s.loops && i >= int64_t (s.loopEnd)) i = s.loopStart + (i - s.loopStart) % int64_t (s.loopEnd - s.loopStart);
                else if (i >= int64_t (s.frames)) return 0;
                return s.pcm[size_t (i) * size_t (s.channels) + size_t (ch)] / 8192.0;
            };
            const int64_t i = int64_t (v.samplePos);
            const double t = v.samplePos - double (i);
            out = at (i, 0) + (at (i + 1, 0) - at (i, 0)) * t;
            if (s.channels == 2) right = at (i, 1) + (at (i + 1, 1) - at (i, 1)) * t;
            v.samplePos += inc;
            if (s.loops && v.samplePos >= s.loopEnd) v.samplePos -= double (s.loopEnd - s.loopStart);
            return out;
        }
        case Source::Analog: break;
    }
    const int w = p.wave;
    switch (p.category)
    {
        case 0: // Pure
            switch (w)
            {
                case 0: out = sine (ph); break;
                case 1: out = tri (ph); break;
                case 2: out = saw (ph, dt); break;
                case 3: out = pulse (ph, dt, 0.5); break;
                case 4: out = pulse (ph, dt, 0.33); break;
                case 5: out = pulse (ph, dt, 0.10); break;
                default: out = white (v.rng) * 0.6; break;
            }
            break;
        case 1: // Sub Osc: sierra + una octava abajo (cuadrada, pulso o seno), mezcla con OSC CTRL
        {
            auto& sp = v.subPhase[size_t (u)];
            const double sub = w == 0 ? pulse (sp, dt * 0.5, 0.5) : w == 1 ? pulse (sp, dt * 0.5, 0.25) : sine (sp);
            out = saw (ph, dt) * (1 - 0.5 * c) + sub * c;
            advance (sp, dt * 0.5);
            break;
        }
        case 2: // Sync: el esclavo (hasta 8× la frecuencia) se reinicia con el maestro
        {
            auto& sp = v.subPhase[size_t (u)];
            const double ratio = 1.0 + c * 7.0, sdt = std::min (0.49, dt * ratio);
            switch (w)
            {
                case 0: out = sine (sp); break;
                case 1: out = tri (sp); break;
                case 2: out = saw (sp, sdt); break;
                case 3: out = pulse (sp, sdt, 0.5); break;
                case 4: out = pulse (sp, sdt, 0.33); break;
                case 5: out = pulse (sp, sdt, 0.10); break;
                case 6: out = saw (sp, sdt) * (1.0 - ph); break;
                case 7: out = saw (sp, sdt) * (ph < 0.5 ? 1.0 : 0.0); break;
                default: out = pulse (sp, sdt, 0.5) * (1.0 - ph); break;
            }
            advance (sp, sdt);
            if (advance (ph, dt)) sp = 0;
            return out;
        }
        case 3: // Shape
            switch (w)
            {
                case 0: { const double k = 0.5 + 0.49 * c; out = ph < k ? -1 + 2 * ph / k : 1 - 2 * (ph - k) / (1 - k); break; }
                case 1: out = 2.0 * std::pow (ph, 1.0 + 3.0 * c) - 1.0; break;
                case 2: out = pulse (ph, dt, 0.5 - 0.47 * c); break;
                case 3: { const double wd = 1.0 - 0.9 * c; out = ph < wd ? std::sin (kPi * ph / wd) * 2 - 1 : -1; break; }
                default: { const double x = 2 * ph - 1; out = (1 - c) * (1 - 2 * x * x) + c * saw (ph, dt); break; }
            }
            break;
        case 4: // Shape Sine: del seno hacia la onda indicada
        {
            const double s = sine (ph);
            double target = s;
            switch (w)
            {
                case 0: target = saw (ph, dt); break;
                case 1: target = pulse (ph, dt, 0.5); break;
                case 2: target = pulse (ph, dt, 0.2); break;
                case 3: target = std::sin (kTwoPi * std::pow (ph, 1.0 + 3.0 * c)); break;
                case 4: target = std::sin (kPi * (1.0 + 4.0 * c) * s); break;
                case 5: target = std::tanh (3.0 * saw (ph, dt)) / std::tanh (3.0); break;
                default: target = std::tanh (3.0 * pulse (ph, dt, 0.5)) / std::tanh (3.0); break;
            }
            out = (w == 3 || w == 4) ? target : (1 - c) * s + c * target;
            break;
        }
        case 5: // Multi: varias sierras (desafinadas o a intervalos)
        {
            static const double intervals[6][3] = { { 0, 0, 0 }, { 0, 12, 0 }, { 0, 7, 0 }, { 0, 7, 12 }, { 0, 12, 24 }, { 0, 12, 19 } };
            static const int counts[6] = { 2, 2, 2, 3, 3, 3 };
            const int ww = std::clamp (w, 0, 5);
            const double det = c * 0.3; // desafinación: hasta 30 cents
            for (int k = 0; k < counts[ww]; ++k)
            {
                const double semis = intervals[ww][k] + (k % 2 == 0 ? -det : det);
                auto& pk = v.phase[size_t (std::min (7, u * 3 + k))];
                const double d = std::min (0.49, dt * std::pow (2.0, semis / 12.0));
                out += saw (pk, d);
                advance (pk, d);
            }
            return out / std::sqrt (double (counts[ww]));
        }
        case 6: // Super: siete osciladores desafinados
        {
            static const double spread[7] = { 0, -0.11, 0.11, -0.06, 0.06, -0.02, 0.02 };
            const double amount = 0.05 + c * 0.95; // hasta ~1,1 semitonos en los extremos
            for (int k = 0; k < 7; ++k)
            {
                auto& pk = v.phase[size_t (k)];
                const double d = std::min (0.49, dt * std::pow (2.0, spread[k] * amount * 10.0 / 12.0));
                double s = 0;
                switch (w)
                {
                    case 1: case 4: s = pulse (pk, d, 0.5); break;
                    case 2: s = sine (pk) + 0.5 * sine (2 * pk) + 0.25 * sine (4 * pk); break;
                    default: s = saw (pk, d); break;
                }
                if (w == 3 || w == 4) s = std::tanh (1.6 * s);
                out += s;
                advance (pk, d);
            }
            return out / 2.6;
        }
        default: // Misc: ruido rosa, ruido rojo, campana
            if (w == 0)
            {
                const double x = white (v.rng);
                v.pink[0] = 0.99765 * v.pink[0] + x * 0.0990460;
                v.pink[1] = 0.96300 * v.pink[1] + x * 0.2965164;
                v.pink[2] = 0.57000 * v.pink[2] + x * 1.0526913;
                out = (v.pink[0] + v.pink[1] + v.pink[2] + x * 0.1848) * 0.25;
            }
            else if (w == 1) { v.red = 0.985 * v.red + 0.15 * white (v.rng); out = v.red; }
            else
            {
                auto& mph = v.subPhase[size_t (u)];
                advance (mph, std::min (0.49, dt * 3.5));
                out = std::sin (kTwoPi * ph + (1.0 + 3.0 * c) * std::sin (kTwoPi * mph));
            }
            break;
    }
    advance (ph, dt);
    return out;
}

// ── Filtro: SVF de retardo cero (LP12, BP, HP; dos en serie para LP24), escalera de 4 polos (LPM) ──
void SynthEngine::filterSample (Voice& v, double fc, double& l, double& r)
{
    const auto& p = params_;
    if (! p.filterOn) return;
    fc = std::clamp (fc, 15.0, 0.45 * sr_);
    const double g = std::tan (kPi * fc / sr_);
    const double k = resonanceDamping (p.reso);
    const double drive = 1.0 + 1.2 * p.drive;
    double* io[2] = { &l, &r };
    for (int ch = 0; ch < 2; ++ch)
    {
        double x = *io[ch];
        if (p.drive > 0) x = std::tanh (x * drive) / std::tanh (drive) * 1.2;
        auto svf = [&] (double in, double kk, double& ic1, double& ic2, double& lp, double& bp, double& hp)
        {
            const double a1 = 1.0 / (1.0 + g * (g + kk)), a2 = g * a1, a3 = g * a2;
            const double v3 = in - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2;
            lp = v2; bp = v1; hp = in - kk * v1 - v2;
        };
        double lp, bp, hp;
        // Con resonancia alta entra menos señal, para que no recorte (resonanceInputGain de OpenNord).
        const double inGain = std::max (0.3, std::pow (std::clamp (k, 0.0, 2.0) / 2.0, 0.25));
        switch (p.filterType)
        {
            case FilterType::LP12: svf (x * inGain, k, v.filter.s1[ch], v.filter.s2[ch], lp, bp, hp); x = lp; break;
            case FilterType::BP: svf (x * inGain, k, v.filter.s1[ch], v.filter.s2[ch], lp, bp, hp); x = std::tanh (bp * std::max (0.5, k)); break;
            case FilterType::HP: svf (x * inGain, k, v.filter.s1[ch], v.filter.s2[ch], lp, bp, hp); x = hp; break;
            case FilterType::LP24:
            {
                double l1, b1, h1;
                svf (x, 1.41421356, v.filter.s1[ch], v.filter.s2[ch], l1, b1, h1);
                svf (l1 * inGain, k, v.filter.t1[ch], v.filter.t2[ch], lp, bp, hp);
                x = lp;
                break;
            }
            case FilterType::LPHP:
            {
                // Paso bajo a la frecuencia y paso alto a la del mando de resonancia, sin resonancia.
                double l1, b1, h1;
                svf (x, 1.41421356, v.filter.s1[ch], v.filter.s2[ch], l1, b1, h1);
                const double gh = std::tan (kPi * std::clamp (cutoffHz (p.reso), 15.0, 0.45 * sr_) / sr_);
                const double a1 = 1.0 / (1.0 + gh * (gh + 1.41421356)), a2 = gh * a1, a3 = gh * a2;
                double& ic1 = v.filter.t1[ch];
                double& ic2 = v.filter.t2[ch];
                const double v3 = l1 - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
                ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2;
                x = l1 - 1.41421356 * v1 - v2;
                break;
            }
            case FilterType::LPM:
            {
                // Escalera de cuatro polos con realimentación (resolución sin retardo) y saturación suave.
                const double G = g / (1.0 + g);
                const double res = 2.0 * (2.0 - k); // 0 sin resonancia, 4 autooscilación
                auto& s = v.filter.lad[ch];
                const double S = (G * G * G * s[0] + G * G * s[1] + G * s[2] + s[3]) / (1.0 + g);
                double u = (x - res * S) / (1.0 + res * G * G * G * G);
                u = std::tanh (u);
                for (int i = 0; i < 4; ++i)
                {
                    const double vv = (u - s[i]) * G;
                    const double y = vv + s[i];
                    s[i] = y + vv;
                    u = y;
                }
                x = u * (1.0 + 0.25 * res);
                break;
            }
        }
        *io[ch] = x;
    }
}

void SynthEngine::render (float* left, float* right, int frames)
{
    const auto& p = params_;
    // LFO compartido (libre).
    const double lfoInc = lfoRateHz (p.lfoRate) / sr_;
    const int unisonCount = (p.source == Source::Sample || (p.source == Source::Analog && p.category >= 5)) ? 1 : 1 + std::clamp (p.unison, 0, 3);
    static const double unisonCents[4][4] = { { 0 }, { -6, 6 }, { -10, 0, 10 }, { -16, -6, 6, 16 } };
    const double* det = unisonCents[unisonCount - 1];
    const double uniNorm = 1.0 / std::sqrt (double (unisonCount));
    const double glideTau = p.glide > 0 ? 3.0 * std::pow (p.glide / 127.0, 2.0) / 4.0 : 0.0;
    const double glideMul = glideTau > 0 ? std::exp (-1.0 / (glideTau * sr_)) : 0.0;
    const double vibHz = 2.0 + 6.0 * p.vibRate / 127.0;
    const double vibDepth = p.vibAmt / 50.0 * 0.5;

    for (int start = 0; start < frames; start += kControl)
    {
        const int n = std::min (kControl, frames - start);
        // LFO para este tramo.
        double lfo = 0;
        switch (p.lfoShape)
        {
            case 1: lfo = 2 * lfoPhase_ - 1; break;
            case 2: lfo = 1 - 2 * lfoPhase_; break;
            case 3: lfo = lfoPhase_ < 0.5 ? 1 : -1; break;
            case 4: lfo = lfoHold_; break;
            default: lfo = 1 - 4 * std::abs (lfoPhase_ - 0.5); break;
        }
        lfoPhase_ += lfoInc * n;
        if (lfoPhase_ >= 1) { lfoPhase_ -= std::floor (lfoPhase_); lfoHold_ = white (lfoRng_); }

        for (auto& v : voices_)
        {
            if (! v.active) continue;
            // Modulación de este tramo (envolventes al principio del tramo).
            const double oenv = v.oenv.level, fenv = v.fenv.level;
            double ctrl = p.oscCtrl / 127.0;
            double pitchMod = 0;
            const double oscEnv = p.oscEnvAmt / 63.0 * oenv * (p.oscEnvVelocity ? v.velocity / 127.0 : 1.0);
            if (p.oscEnvToPitch) pitchMod += oscEnv * 24.0; else ctrl += oscEnv;
            if (p.lfoTarget == 1) ctrl += lfo * p.lfoAmt / 127.0 * 0.5;
            if (p.lfoTarget == 2) pitchMod += lfo * depthCurveSemitones (p.lfoAmt);
            // Vibrato.
            double vib = 0;
            if (p.vibMode > 0)
            {
                double amount = 1.0;
                if (p.vibMode == 2) amount = std::clamp ((v.vibTime - 0.4) / 0.6, 0.0, 1.0);
                if (p.vibMode == 3) amount = modWheel_;
                if (p.vibMode == 4) amount = aftertouch_;
                vib = std::sin (kTwoPi * vibHz * v.vibTime) * vibDepth * amount;
            }
            v.vibTime += double (n) / sr_;
            ctrl = clamp01 (ctrl);
            // Glide (portamento) en semitonos.
            if (glideTau > 0) v.pitch = v.targetPitch + (v.pitch - v.targetPitch) * std::pow (glideMul, n);
            else v.pitch = v.targetPitch;
            const double semis = v.pitch + p.coarse + p.fineCents / 100.0 + 12.0 * p.octave + p.tuneCents / 100.0 + pitchMod + vib;
            const double freq = 440.0 * std::pow (2.0, (semis - 69.0) / 12.0);
            // Corte: valor del panel + seguimiento de teclado + envolvente (hasta 10 octavas) + LFO.
            double cut = cutoffSemitones (p.cutoff) + (p.track / 3.0) * (v.pitch - 61.0)
                       + p.filterEnvAmt / 127.0 * 120.0 * fenv * (p.filterVelocity ? v.velocity / 127.0 : 1.0);
            if (p.lfoTarget == 3) cut += lfo * p.lfoAmt / 127.0 * 48.0;
            const double fc = 277.1826 * std::pow (2.0, cut / 12.0);
            // Velocidad → nivel: hasta 36 dB según "amp env velocity".
            const double velDb = -12.0 * p.ampVelocity * (1.0 - v.velocity / 127.0);
            const double gain = kVoiceLevel * std::pow (10.0, velDb / 20.0) * uniNorm;

            for (int i = 0; i < n; ++i)
            {
                double l = 0, r = 0;
                for (int u = 0; u < unisonCount; ++u)
                {
                    double rr;
                    const double f = freq * std::pow (2.0, det[u] / 1200.0);
                    const double s = oscillator (v, u, f, ctrl, rr);
                    // Unison abierto en estéreo.
                    const double pan = unisonCount > 1 ? double (u) / (unisonCount - 1) : 0.5;
                    if (! std::isnan (rr)) { l += s; r += rr; }
                    else { l += s * std::sqrt (1.0 - pan * 0.6); r += s * std::sqrt (0.4 + pan * 0.6); }
                }
                filterSample (v, fc, l, r);
                const double a = v.amp.next() * gain;
                v.fenv.next();
                v.oenv.next();
                left[start + i] += float (l * a);
                right[start + i] += float (r * a);
            }
            // Fin: envolvente apagada, o un sample sin bucle que ya terminó.
            if (v.amp.stage == Env::Off || (v.amp.stage == Env::Decay && v.amp.level < 1.0e-5)
                || (v.stroke && ! v.stroke->loops && v.samplePos >= v.stroke->frames))
                v.active = false;
        }
    }
}

SynthParams paramsFromLayer (const SynthLayer& l)
{
    SynthParams p;
    p.source = l.usesSample() ? Source::Sample : Source (std::clamp (int (l[F_oscType]), 0, 3));
    p.category = int (l[F_oscCat]);
    p.wave = int (l[F_oscWave]);
    p.oscCtrl = float (l[F_oscCtrl]);
    p.coarse = l.coarse();
    p.fineCents = l.fineCents();
    p.octave = l.octave();
    p.oscEnvA = float (l[F_oscEnvA]); p.oscEnvD = float (l[F_oscEnvD]); p.oscEnvR = float (l[F_oscEnvR]);
    p.oscEnvAmt = float (l.oscEnvAmount());
    p.oscEnvToPitch = l[F_oscEnvPitch] == 1;
    p.oscEnvVelocity = l[F_oscEnvVel] == 1;
    p.filterOn = l[F_filterOn] == 1;
    p.filterType = FilterType (std::clamp (int (l[F_filterType]), 0, 5));
    p.cutoff = float (l[F_cutoff]);
    p.reso = float (l[F_reso]);
    p.track = int (l[F_track]);
    p.drive = int (l[F_drive]);
    p.filterEnvAmt = float (l[F_fEnvAmt]);
    p.fEnvA = float (l[F_fEnvA]); p.fEnvD = float (l[F_fEnvD]); p.fEnvR = float (l[F_fEnvR]);
    p.filterVelocity = l[F_fVel] == 1;
    p.ampA = float (l[F_ampA]); p.ampD = float (l[F_ampD]); p.ampR = float (l[F_ampR]);
    p.ampVelocity = int (l[F_ampVel]);
    p.lfoTarget = int (l[F_lfoTarget]);
    p.lfoShape = int (l[F_lfoShape]);
    p.lfoRate = float (l[F_lfoRate]);
    p.lfoAmt = float (l[F_lfoAmt]);
    p.vibMode = int (l[F_vibMode]);
    p.vibRate = float (l[F_vibRate]);
    p.vibAmt = float (l[F_vibAmt]);
    p.mono = l[F_mono] == 1;
    p.legato = l[F_legato] == 1;
    p.glide = float (l[F_glide]);
    p.unison = int (l[F_unison]);
    return p;
}

int SynthEngine::activeVoices() const
{
    int n = 0;
    for (const auto& v : voices_) n += v.active;
    return n;
}
} // namespace bk::synth
