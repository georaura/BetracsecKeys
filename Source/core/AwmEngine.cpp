#include "AwmEngine.h"
#include "FileUtf8.h"
#include "FmxEngine.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <set>

namespace bk::awm
{
namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr int kControl = 32;          // muestras por actualización de envolventes, filtro y tono
constexpr float kOutLevel = 0.1f;     // una muestra a fondo de escala con el elemento a 127

/** Tiempo de envolvente AWM2 0..127 → segundos (aproximado: 64 ≈ 0,2 s, 99 ≈ 3,3 s, 127 ≈ 26 s). */
double seconds (int t) { return t <= 0 ? 0.0 : 0.0015 * std::exp2 (t / 9.0); }

/** Nivel de envolvente de amplitud 0..127 → ganancia (0,6 dB por paso). */
double ampLevel (int l) { return l <= 0 ? 0.0 : std::pow (10.0, (l - 127) * 0.6 / 20.0); }

/** Corte 0..255 → Hz (aproximado: 9,6 octavas desde 25 Hz). */
double cutoffHz (double steps) { return 25.0 * std::exp2 (std::clamp (steps, 0.0, 300.0) * 9.6 / 255.0); }
constexpr double kStepsPerOctave = 255.0 / 9.6;

double lfoHz (int speed) { return 0.1 * std::exp2 (speed / 7.0); }

struct Svf
{
    double a1 = 0, a2 = 0, a3 = 0, k = 1.4;
    void set (double hz, double q, double sr)
    {
        const double g = std::tan (kPi * std::clamp (hz, 10.0, sr * 0.45) / sr);
        k = 1.0 / q;
        a1 = 1.0 / (1.0 + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    /** Devuelve paso bajo, banda y paso alto. */
    void tick (double x, double& s1, double& s2, double& lp, double& bp, double& hp) const
    {
        const double v3 = x - s2;
        const double v1 = a1 * s1 + a2 * v3;
        const double v2 = s2 + a2 * s1 + a3 * v3;
        s1 = 2 * v1 - s1;
        s2 = 2 * v2 - s2;
        lp = v2; bp = v1; hp = x - k * v1 - v2;
    }
};

enum class Kind { LP24, LP18, LP12HP12, LP6HP12, HP24, HP12, BP, BPWide, Notch, NotchWide, DualLP, DualHP, DualBP, DualNotch, Thru, LP12 };

Kind kindOf (int type)
{
    switch (type)
    {
        case 0: case 1: return Kind::LP24;
        case 2: case 3: return Kind::LP18;
        case 4: return Kind::LP12HP12;
        case 5: return Kind::LP6HP12;
        case 6: return Kind::HP24;
        case 7: return Kind::HP12;
        case 8: case 9: return Kind::BP;
        case 10: return Kind::BPWide;
        case 11: return Kind::Notch;
        case 12: return Kind::NotchWide;
        case 13: return Kind::DualLP;
        case 14: return Kind::DualHP;
        case 15: return Kind::DualBP;
        case 16: return Kind::DualNotch;
        case 17: return Kind::Thru;
        default: return Kind::LP12;
    }
}
} // namespace

// ── Carga de muestras ───────────────────────────────────────────────────────────────────────────
std::unique_ptr<SampleSet> loadSamples (const std::string& path, const bk::ysfc::Performance& perf,
                                        const std::vector<bk::ysfc::Waveform>& waves, const std::atomic<bool>* cancel)
{
    std::set<int> used;
    for (const auto& part : perf.awm)
        for (const auto& e : part.elements)
            if (e.on && e.bank == 1 && e.wave >= 1 && size_t (e.wave) <= waves.size() && waves[size_t (e.wave - 1)].readable())
                used.insert (e.wave);
    if (used.empty()) return nullptr;
    auto f = bk::io::openRead (path);
    if (! f) return nullptr;
    auto set = std::make_unique<SampleSet>();
    for (int w : used)
    {
        auto& out = set->waves[w];
        out.keybanks = waves[size_t (w - 1)].keybanks;
        for (const auto& kb : out.keybanks)
        {
            if (cancel != nullptr && cancel->load()) return nullptr;
            std::vector<int16_t> data (kb.length);
            f.seekg (std::streamoff (kb.offset));
            std::vector<uint8_t> raw (size_t (kb.length) * 2);
            f.read (reinterpret_cast<char*> (raw.data()), std::streamsize (raw.size()));
            if (! f) { f.clear(); data.clear(); }
            else for (size_t i = 0; i < data.size(); ++i) data[i] = int16_t (raw[2 * i] | (raw[2 * i + 1] << 8));
            set->bytes += data.size() * 2;
            out.data.push_back (std::move (data));
        }
    }
    return set;
}

// ── Motor ───────────────────────────────────────────────────────────────────────────────────────
void AwmEngine::prepare (double sampleRate)
{
    sr_ = sampleRate > 0 ? sampleRate : 48000;
    allNotesOff (true);
}

void AwmEngine::setPerformance (const bk::ysfc::Performance* p, const SampleSet* samples)
{
    for (auto& v : voices_) v.active = false;
    parts_.clear();
    if (p == nullptr || samples == nullptr) return;
    for (const auto& ap : p->awm)
    {
        if (int (parts_.size()) >= kMaxParts) break;
        Part part;
        part.info = ap;
        for (const auto& e : ap.elements)
        {
            if (! e.on) continue;
            const auto it = samples->waves.find (e.wave);
            if (e.bank != 1 || it == samples->waves.end()) continue;
            Element el;
            el.e = e;
            const auto& w = it->second;
            for (size_t k = 0; k < w.keybanks.size() && k < w.data.size(); ++k)
            {
                const auto& kb = w.keybanks[k];
                const auto& d = w.data[k];
                if (d.size() < 2) continue;
                Zone z;
                z.data = d.data();
                z.length = uint32_t (d.size());
                z.loop = kb.loopMode != 1 && kb.loopEnd > kb.loopStart && kb.loopEnd < z.length;
                z.loopStart = kb.loopStart;
                z.loopEnd = kb.loopEnd;
                z.keyLo = kb.keyLo; z.keyHi = kb.keyHi; z.velLo = kb.velLo; z.velHi = kb.velHi;
                z.rootSemis = kb.root - ((kb.coarse - 64) + (kb.fine - 64) * 1.5625 / 100.0);
                z.rate = kb.rate > 0 ? double (kb.rate) : 44100.0;
                z.gain = kb.level <= 0 ? 0.0f : float (std::pow (10.0, (-95.25 + (kb.level - 1) * 0.375) / 20.0));
                const double pan = std::clamp ((kb.pan - 64) / 63.0 + (e.pan - 64) / 63.0 + (ap.pan - 64) / 63.0, -1.0, 1.0);
                z.panL = float (std::cos ((pan + 1) * kPi / 4));
                z.panR = float (std::sin ((pan + 1) * kPi / 4));
                el.zones.push_back (z);
            }
            if (! el.zones.empty()) part.elements.push_back (std::move (el));
        }
        if (! part.elements.empty()) parts_.push_back (std::move (part));
    }
}

void AwmEngine::startVoice (Voice& v, int part, int element, const Zone& z, int key, int velocity)
{
    const auto& p = parts_[size_t (part)];
    const auto& e = p.elements[size_t (element)].e;
    v = Voice {};
    v.active = true;
    v.key = key;
    v.velocity = velocity;
    v.part = part;
    v.element = element;
    v.zone = &z;
    v.age = ++counter_;
    // Nivel: elemento (curva de volumen MIDI), velocidad, volumen de la parte.
    const double vel = velocity / 127.0;
    const double sens = (e.levelVelSens - 64) / 63.0;
    double g = std::pow (e.level / 127.0, 2.0) * std::pow (std::max (vel, 0.01), 2.0 * sens);
    const double vol = std::clamp (p.info.volume, 0, 127) / 100.0;
    g *= vol * vol * z.gain * kOutLevel;
    v.gainL = float (g * z.panL);
    v.gainR = float (g * z.panR);
    const int note = std::clamp (key + p.info.noteShift, 0, 127);
    v.ampTimeScale = std::exp2 (-(note - e.aegKeyCentre) / 12.0 * (e.aegTimeKeyFollow - 64) / 32.0);
    v.amp.stage = Env::Seg;
    v.amp.seg = 0;
    v.amp.value = v.amp.from = ampLevel (e.aegLevel[0]);
    v.amp.to = ampLevel (e.aegLevel[1]);
    v.amp.dur = seconds (e.aegTime[0]) * v.ampTimeScale;
    v.feg.stage = e.fegTime[0] > 0 ? Env::Hold : Env::Seg;
    v.feg.seg = 0;
    v.feg.value = v.feg.from = e.fegLevel[0] - 128;
    v.feg.to = e.fegLevel[1] - 128;
    v.feg.dur = v.feg.stage == Env::Hold ? seconds (e.fegTime[0]) : seconds (e.fegTime[1]);
    v.lfoPhase = e.lfoSync ? 0.0 : (counter_ % 997) / 997.0;
}

void AwmEngine::releaseVoice (Voice& v)
{
    if (v.released) return;
    v.released = true;
    const auto& e = parts_[size_t (v.part)].elements[size_t (v.element)].e;
    v.amp.stage = Env::Release;
    v.amp.from = v.amp.value;
    v.amp.to = 0;
    v.amp.t = 0;
    v.amp.dur = seconds (e.aegTime[3]) * v.ampTimeScale;
    v.feg.stage = Env::Release;
    v.feg.from = v.feg.value;
    v.feg.to = e.fegLevel[4] - 128;
    v.feg.t = 0;
    v.feg.dur = seconds (e.fegTime[4]);
}

double AwmEngine::stepEnv (Env& env, const bk::ysfc::AwmElement& e, bool amp, double dt, double timeScale, bool)
{
    auto interp = [amp] (double from, double to, double x)
    {
        if (! amp) return from + (to - from) * x;
        // Ataque lineal en amplitud; caídas lineales en dB.
        if (to >= from) return from + (to - from) * x;
        const double a = 20 * std::log10 (std::max (from, 1e-5)), b = 20 * std::log10 (std::max (to, 1e-5));
        const double db = a + (b - a) * x;
        return to <= 0 && x >= 1 ? 0.0 : std::pow (10.0, db / 20.0);
    };
    switch (env.stage)
    {
        case Env::Off: return env.value;
        case Env::Sustain: return env.value;
        case Env::Hold:
            env.t += dt;
            if (env.t >= env.dur)
            {
                env.stage = Env::Seg;
                env.seg = 0;
                env.t = 0;
                env.from = env.value;
                env.to = e.fegLevel[1] - 128;
                env.dur = seconds (e.fegTime[1]);
            }
            return env.value;
        case Env::Seg:
        case Env::Release:
        {
            env.t += dt;
            const double x = env.dur <= 0 ? 1.0 : std::min (1.0, env.t / env.dur);
            env.value = interp (env.from, env.to, x);
            if (x < 1.0) return env.value;
            if (env.stage == Env::Release) { env.stage = Env::Off; return env.value; }
            if (env.seg >= 2) { env.stage = Env::Sustain; return env.value; }
            ++env.seg;
            env.t = 0;
            env.from = env.value;
            if (amp)
            {
                env.to = ampLevel (e.aegLevel[size_t (env.seg + 1)]);
                env.dur = seconds (e.aegTime[size_t (env.seg)]) * timeScale;
            }
            else
            {
                env.to = e.fegLevel[size_t (env.seg + 1)] - 128;
                env.dur = seconds (e.fegTime[size_t (env.seg + 1)]);
            }
            return env.value;
        }
    }
    return env.value;
}

void AwmEngine::control (Voice& v, double& ampGain, double& cutoff, double& hpf, double& pitch)
{
    const auto& p = parts_[size_t (v.part)];
    const auto& e = p.elements[size_t (v.element)].e;
    const double dt = kControl / sr_;
    ampGain = stepEnv (v.amp, e, true, dt, v.ampTimeScale, v.released);
    const double feg = stepEnv (v.feg, e, false, dt, 1.0, v.released);

    // LFO del elemento.
    double lfo = 0;
    v.lfoTime += dt;
    if (e.lfoAmp > 0 || e.lfoPitch > 0 || e.lfoFilter > 0)
    {
        v.lfoPhase += lfoHz (e.lfoSpeed) * dt;
        v.lfoPhase -= std::floor (v.lfoPhase);
        const double x = v.lfoPhase;
        lfo = e.lfoWave == 0 ? 2 * x - 1 : e.lfoWave == 2 ? (x < 0.5 ? 1.0 : -1.0) : (x < 0.5 ? 4 * x - 1 : 3 - 4 * x);
        const double delay = seconds (e.lfoDelay), fade = seconds (e.lfoFadeIn);
        lfo *= std::clamp ((v.lfoTime - delay) / std::max (fade, 0.01), 0.0, 1.0);
    }
    if (e.lfoAmp > 0) ampGain *= 1.0 - e.lfoAmp / 127.0 * (0.5 + 0.5 * lfo);

    const int note = std::clamp (v.key + p.info.noteShift, 0, 127);
    const double vel = v.velocity / 127.0;
    const double fegVel = 1.0 - (e.fegLevelVelSens - 64) / 63.0 * (1.0 - vel);
    cutoff = e.cutoff
           + (e.cutoffVelSens - 64) * (v.velocity - 64) / 63.0
           + (e.cutoffKeyFollow - 64) / 32.0 * (note - 60) / 12.0 * kStepsPerOctave
           + feg / 128.0 * (e.fegDepth - 64) / 64.0 * 128.0 * fegVel
           + lfo * e.lfoFilter / 127.0 * 64.0;
    hpf = e.hpfCutoff + (note - 60) / 12.0 * kStepsPerOctave;

    const double bend = bend_ >= 0 ? bend_ * p.info.bendUp : -bend_ * p.info.bendDown;
    const double kf = (e.pitchKeyFollow - 64) / 32.0;
    pitch = e.pitchKeyCentre + (note - e.pitchKeyCentre) * kf + (e.coarse - 64) + (e.fine - 64) / 100.0
          + bend + tuneCents_ / 100.0 + lfo * e.lfoPitch / 127.0 * 2.0;
}

void AwmEngine::noteOn (int key, int velocity)
{
    if (key < 0 || key > 127 || parts_.empty()) return;
    if (velocity <= 0) { noteOff (key); return; }
    keyDown_[size_t (key)] = true;
    bool anyKeyboard = false;
    for (const auto& p : parts_) anyKeyboard = anyKeyboard || (p.info.keyboard && p.info.partOn);
    for (int pi = 0; pi < int (parts_.size()); ++pi)
    {
        const auto& p = parts_[size_t (pi)];
        if (! p.info.partOn || (anyKeyboard ? ! p.info.keyboard : pi != 0)) continue;
        const auto& in = p.info;
        const bool inRange = in.noteLo <= in.noteHi ? (key >= in.noteLo && key <= in.noteHi) : (key >= in.noteLo || key <= in.noteHi);
        const bool inVel = in.velLo <= in.velHi ? (velocity >= in.velLo && velocity <= in.velHi) : (velocity >= in.velLo || velocity <= in.velHi);
        if (! inRange || ! inVel) continue;
        const int note = std::clamp (key + in.noteShift, 0, 127);
        for (int ei = 0; ei < int (p.elements.size()); ++ei)
        {
            const auto& el = p.elements[size_t (ei)];
            const auto& e = el.e;
            const bool eNote = e.noteLo <= e.noteHi ? (note >= e.noteLo && note <= e.noteHi) : (note >= e.noteLo || note <= e.noteHi);
            const bool eVel = e.velLo <= e.velHi ? (velocity >= e.velLo && velocity <= e.velHi) : (velocity >= e.velLo || velocity <= e.velHi);
            if (! eNote || ! eVel) continue;
            for (const auto& z : el.zones)
            {
                if (note < z.keyLo || note > z.keyHi || velocity < z.velLo || velocity > z.velHi) continue;
                Voice* slot = nullptr;
                for (auto& v : voices_) if (! v.active) { slot = &v; break; }
                if (slot == nullptr)
                {
                    for (auto& v : voices_) if (v.released && (slot == nullptr || v.age < slot->age)) slot = &v;
                    if (slot == nullptr) for (auto& v : voices_) if (slot == nullptr || v.age < slot->age) slot = &v;
                }
                startVoice (*slot, pi, ei, z, key, velocity);
            }
        }
    }
}

void AwmEngine::noteOff (int key)
{
    if (key < 0 || key > 127) return;
    keyDown_[size_t (key)] = false;
    for (auto& v : voices_)
    {
        if (! v.active || v.key != key || v.released) continue;
        if (sustain_) { v.sustained = true; continue; }
        releaseVoice (v);
    }
}

void AwmEngine::setSustain (bool down)
{
    sustain_ = down;
    if (down) return;
    for (auto& v : voices_)
        if (v.active && v.sustained && ! keyDown_[size_t (v.key)]) { v.sustained = false; releaseVoice (v); }
}

void AwmEngine::allNotesOff (bool immediate)
{
    sustain_ = false;
    keyDown_.fill (false);
    for (auto& v : voices_)
    {
        if (! v.active) continue;
        if (immediate) v.active = false;
        else releaseVoice (v);
    }
}

int AwmEngine::activeVoices() const
{
    int n = 0;
    for (const auto& v : voices_) n += v.active;
    return n;
}

void AwmEngine::render (float* left, float* right, int frames)
{
    for (auto& v : voices_)
    {
        if (! v.active) continue;
        const auto& e = parts_[size_t (v.part)].elements[size_t (v.element)].e;
        const Kind kind = kindOf (e.filterType);
        const auto& z = *v.zone;
        for (int start = 0; start < frames && v.active; start += kControl)
        {
            const int n = std::min (kControl, frames - start);
            double amp, cut, hpfSteps, pitch;
            control (v, amp, cut, hpfSteps, pitch);
            if (v.amp.stage == Env::Off || (v.released && amp < 1.0e-5)) { v.active = false; break; }
            v.step = std::exp2 ((pitch - z.rootSemis) / 12.0) * z.rate / sr_;
            const double q = 0.707 + e.resonance / 127.0 * 12.0;
            const double fc = cutoffHz (cut);
            const double dist = (e.distance - 128) / 128.0 * 2.0;
            Svf a, b, h;
            a.set (fc, q, sr_);
            b.set (kind >= Kind::DualLP && kind <= Kind::DualNotch ? fc * std::exp2 (dist) : fc,
                   kind == Kind::LP24 || kind == Kind::HP24 ? 0.707 : q, sr_);
            if (kind == Kind::BPWide || kind == Kind::NotchWide) a.set (fc, 0.5, sr_);
            const bool useHpf = (kind == Kind::LP12HP12 || kind == Kind::LP6HP12) && e.hpfCutoff > 0;
            if (useHpf) h.set (cutoffHz (hpfSteps), 0.707, sr_);
            const double onePole = 1.0 - std::exp (-2 * kPi * std::min (fc, sr_ * 0.45) / sr_);
            const float gL = float (amp) * v.gainL * gain_, gR = float (amp) * v.gainR * gain_;
            for (int i = 0; i < n; ++i)
            {
                // Muestra con interpolación lineal.
                const auto idx = uint32_t (v.pos);
                const double frac = v.pos - idx;
                uint32_t nxt = idx + 1;
                if (z.loop && nxt > z.loopEnd) nxt = z.loopStart;
                if (nxt >= z.length) nxt = z.length - 1;
                double x = (z.data[idx] + (z.data[nxt] - z.data[idx]) * frac) * (1.0 / 32768.0);
                v.pos += v.step;
                if (z.loop) { while (v.pos > double (z.loopEnd) + 1.0) v.pos -= double (z.loopEnd - z.loopStart + 1); }
                else if (v.pos >= double (z.length - 1)) { v.active = false; }

                double lp, bp, hp, lp2, bp2, hp2;
                switch (kind)
                {
                    case Kind::Thru: break;
                    case Kind::LP24: a.tick (x, v.s1[0], v.s2[0], lp, bp, hp); b.tick (lp, v.s1[1], v.s2[1], lp2, bp2, hp2); x = lp2; break;
                    case Kind::LP18: a.tick (x, v.s1[0], v.s2[0], lp, bp, hp); v.lp1 += (lp - v.lp1) * onePole; x = v.lp1; break;
                    case Kind::LP12: case Kind::LP12HP12: a.tick (x, v.s1[0], v.s2[0], lp, bp, hp); x = lp; break;
                    case Kind::LP6HP12: v.lp1 += (x - v.lp1) * onePole; x = v.lp1; break;
                    case Kind::HP24: a.tick (x, v.s1[0], v.s2[0], lp, bp, hp); b.tick (hp, v.s1[1], v.s2[1], lp2, bp2, hp2); x = hp2; break;
                    case Kind::HP12: a.tick (x, v.s1[0], v.s2[0], lp, bp, hp); x = hp; break;
                    case Kind::BP: case Kind::BPWide: a.tick (x, v.s1[0], v.s2[0], lp, bp, hp); x = bp; break;
                    case Kind::Notch: case Kind::NotchWide: a.tick (x, v.s1[0], v.s2[0], lp, bp, hp); x = lp + hp; break;
                    case Kind::DualLP: case Kind::DualHP: case Kind::DualBP: case Kind::DualNotch:
                    {
                        a.tick (x, v.s1[0], v.s2[0], lp, bp, hp);
                        b.tick (x, v.s1[1], v.s2[1], lp2, bp2, hp2);
                        x = kind == Kind::DualLP ? 0.5 * (lp + lp2) : kind == Kind::DualHP ? 0.5 * (hp + hp2)
                          : kind == Kind::DualBP ? bp + bp2 : 0.5 * (lp + hp + lp2 + hp2);
                        break;
                    }
                }
                if (useHpf) { double l3, b3, h3; h.tick (x, v.h1, v.h2, l3, b3, h3); x = h3; }
                left[start + i] += float (x) * gL;
                right[start + i] += float (x) * gR;
                if (! v.active) break;
            }
        }
    }
}

// ── Sonoridad ───────────────────────────────────────────────────────────────────────────────────
float measureLoudness (const bk::ysfc::Performance& p, const SampleSet* samples)
{
    constexpr int sr = 48000, win = 2400, total = 2 * sr;
    auto fm = std::make_unique<bk::fmx::FmxEngine>();
    auto awm = std::make_unique<AwmEngine>();
    fm->prepare (sr);
    awm->prepare (sr);
    fm->setPerformance (&p);
    awm->setPerformance (&p, samples);
    for (int k : { 48, 55, 60, 64 }) { fm->noteOn (k, 100); awm->noteOn (k, 100); }
    std::vector<float> l (static_cast<size_t> (win)), r (static_cast<size_t> (win));
    double best = 0;
    for (int done = 0; done < total; done += win)
    {
        std::fill (l.begin(), l.end(), 0.0f);
        std::fill (r.begin(), r.end(), 0.0f);
        fm->render (l.data(), r.data(), win);
        awm->render (l.data(), r.data(), win);
        double sum = 0;
        for (int i = 0; i < win; ++i) sum += double (l[size_t (i)]) * l[size_t (i)] + double (r[size_t (i)]) * r[size_t (i)];
        best = std::max (best, sum / (2.0 * win));
    }
    return float (10.0 * std::log10 (best + 1.0e-12));
}
} // namespace bk::awm
