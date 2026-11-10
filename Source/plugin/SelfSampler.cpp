#include "SelfSampler.h"
#include "AutosampleDsp.h"

#include <algorithm>
#include <cmath>
#include <map>

using namespace juce;

namespace bk::selfsample
{
namespace
{
constexpr double kRate = 48000;

/** Lo que hace falta de un motor para tocarlo nota a nota. */
struct Player
{
    virtual ~Player() = default;
    virtual void on (int note, int velocity) = 0;
    virtual void off (int note) = 0;
    virtual void render (float* l, float* r, int n) = 0;   ///< escribe (no suma)
    virtual bool silent() const = 0;
    virtual int channels() const = 0;
};

struct OrganPlayer : Player
{
    bk::organ::OrganEngine organ;
    bk::organ::Rotary rotary;
    bool useRotary = false;
    std::vector<float> mono;
    OrganPlayer (const bk::organ::OrganParams& p, bool rot, float drive)
    {
        organ.prepare (kRate);
        auto q = p;
        q.volumeDb = 0;
        q.sustainPedal = false;
        organ.setParams (q);
        useRotary = rot;
        rotary.prepare (kRate);
        rotary.setEnabled (rot);
        rotary.setSpeed (bk::organ::Rotary::Speed::Slow);
        rotary.setDrive (drive);
    }
    void on (int note, int) override { organ.noteOn (note); }
    void off (int note) override { organ.noteOff (note); }
    void render (float* l, float* r, int n) override
    {
        mono.assign (size_t (n), 0.0f);
        organ.render (mono.data(), n);
        if (useRotary) rotary.process (mono.data(), l, r, n);
        else { std::copy (mono.begin(), mono.end(), l); std::copy (mono.begin(), mono.end(), r); }
    }
    bool silent() const override { return organ.isSilent(); }
    int channels() const override { return useRotary ? 2 : 1; }
};

struct SynthPlayer : Player
{
    bk::synth::SynthEngine synth;
    SynthPlayer (const bk::synth::SynthParams& p, const bk::nsmp::SampleSound* sample)
    {
        synth.prepare (kRate);
        synth.setSample (sample);
        auto q = p;
        q.tuneCents = 0;
        synth.setParams (q);
    }
    void on (int note, int velocity) override { synth.noteOn (note, velocity); }
    void off (int note) override { synth.noteOff (note); }
    void render (float* l, float* r, int n) override
    {
        std::fill (l, l + n, 0.0f);
        std::fill (r, r + n, 0.0f);
        synth.render (l, r, n);
        for (int i = 0; i < n; ++i) { l[i] = bk::synth::softLimit (l[i]); r[i] = bk::synth::softLimit (r[i]); }
    }
    bool silent() const override { return synth.activeVoices() == 0; }
    int channels() const override { return 2; }
};

struct FmPlayer : Player
{
    bk::fmx::FmxEngine fm;
    bk::awm::AwmEngine awm;
    explicit FmPlayer (const FmSound& s)
    {
        fm.prepare (kRate);
        awm.prepare (kRate);
        fm.setPerformance (s.performance);
        awm.setPerformance (s.performance, s.samples);
        fm.setTweaks (s.tweaks);
        fm.setGain (s.gain);
        awm.setGain (s.gain);
    }
    void on (int note, int velocity) override { fm.noteOn (note, velocity); awm.noteOn (note, velocity); }
    void off (int note) override { fm.noteOff (note); awm.noteOff (note); }
    void render (float* l, float* r, int n) override
    {
        std::fill (l, l + n, 0.0f);
        std::fill (r, r + n, 0.0f);
        fm.render (l, r, n);
        awm.render (l, r, n);
        // El mismo limitador suave que pone Keys a la salida de FM.
        for (int i = 0; i < n; ++i) { l[i] = bk::synth::softLimit (l[i]); r[i] = bk::synth::softLimit (r[i]); }
    }
    bool silent() const override { return fm.activeVoices() == 0 && awm.activeVoices() == 0; }
    int channels() const override { return 2; }
};

/** Nivel (dB) cada 50 ms de lo que toca `player` durante `seconds`. */
std::vector<double> levels (Player& player, double seconds)
{
    const int win = int (0.05 * kRate);
    std::vector<float> l (static_cast<size_t> (win)), r (static_cast<size_t> (win));
    std::vector<double> out;
    for (int w = 0; w < int (seconds / 0.05); ++w)
    {
        player.render (l.data(), r.data(), win);
        double e = 0;
        for (int i = 0; i < win; ++i) e += double (l[size_t (i)]) * l[size_t (i)] + double (r[size_t (i)]) * r[size_t (i)];
        out.push_back (10.0 * std::log10 (e / (2.0 * win) + 1.0e-20));
    }
    return out;
}

Probe probeWith (const std::function<std::unique_ptr<Player>()>& make)
{
    Probe p;
    constexpr int note = 60, vel = 100;
    // Con la tecla pisada 8 s.
    auto a = make();
    a->on (note, vel);
    const auto held = levels (*a, 8.0);
    const auto peakIt = std::max_element (held.begin(), held.end());
    const double peak = *peakIt;
    if (peak < -90) return p;
    p.silent = false;
    p.peakSec = double (peakIt - held.begin()) * 0.05;
    for (size_t i = size_t (peakIt - held.begin()); i < held.size(); ++i)
        if (held[i] < peak - 60.0) { p.heldTo60Sec = double (i) * 0.05; break; }
    // Nivel medio de 0,5 s alrededor de un instante (los pads se mueven).
    auto around = [&] (double sec)
    {
        double acc = 0;
        int n = 0;
        for (size_t i = size_t (std::max (0.0, (sec - 0.25) / 0.05)); i < held.size() && double (i) * 0.05 <= sec + 0.25; ++i) { acc += held[i]; ++n; }
        return n > 0 ? acc / n : -200.0;
    };
    const double l4 = around (4.0), late = around (7.5);
    p.sustains = std::abs (l4 - late) < 3.0 && late > peak - 50.0;
    p.slowDecay = ! p.sustains && p.heldTo60Sec >= 8.0;
    p.settleSec = p.peakSec;
    if (p.sustains)
        for (size_t i = size_t (peakIt - held.begin()); i < held.size(); ++i)
            if (std::abs (around (double (i) * 0.05) - late) < 1.5) { p.settleSec = double (i) * 0.05; break; }
    // Soltada tras 1 s (o tras llegar al máximo, si tarda más): lo que tarda en caer 60 dB, pasado a 80.
    auto b = make();
    b->on (note, vel);
    const auto before = levels (*b, std::max (1.0, p.peakSec + 0.2));
    b->off (note);
    const auto after = levels (*b, 10.0);
    const double atOff = before.back();
    double t60 = 10.0;
    for (size_t i = 0; i < after.size(); ++i)
        if (after[i] < atOff - 60.0) { t60 = double (i + 1) * 0.05; break; }
    p.releaseSec = std::clamp (t60 * 80.0 / 60.0, 0.02, 30.0);
    return p;
}

std::unique_ptr<bk::nsmp::SampleSound> renderWith (const std::function<std::unique_ptr<Player>()>& make, const Plan& plan,
                                                   std::atomic<double>* progress, std::string& error)
{
    std::vector<int> notes;
    for (int k = plan.lowNote; k <= plan.highNote; k += std::max (1, plan.step)) notes.push_back (k);
    if (notes.empty() || notes.back() != plan.highNote) notes.push_back (plan.highNote);
    const auto& vels = plan.velocities;
    struct Take { AudioBuffer<float> audio; int note, vel, velLow, velHigh; bool loops; int loopStart, loopEnd; double decay = 0; };
    std::vector<Take> takes;
    const int total = int (notes.size() * vels.size());
    int done = 0;
    float peak = 0;
    const int block = 512, pre = int (0.01 * kRate);
    for (size_t vi = 0; vi < vels.size(); ++vi)
        for (int note : notes)
        {
            auto player = make();
            const int chans = player->channels();
            const int hold = int (plan.holdSec * kRate), tailMax = int (plan.tailSec * kRate);
            bk::autosample::Take t;
            t.sampleRate = kRate;
            t.preRoll = pre;
            t.audio.setSize (chans, pre + hold + tailMax + block, false, true, false);
            t.audio.clear();
            std::vector<float> l (static_cast<size_t> (block)), r (static_cast<size_t> (block));
            int pos = pre;
            player->on (note, vels[vi]);
            auto run = [&] (int frames)
            {
                for (int done2 = 0; done2 < frames; done2 += block)
                {
                    const int n = std::min (block, frames - done2);
                    player->render (l.data(), r.data(), n);
                    t.audio.copyFrom (0, pos, l.data(), n);
                    if (chans > 1) t.audio.copyFrom (1, pos, r.data(), n);
                    pos += n;
                }
            };
            run (hold);
            t.noteOffAt = pos;
            player->off (note);
            for (int tail = 0; tail < tailMax; tail += block)
            {
                run (block);
                if (player->silent()) break;
            }
            t.audio.setSize (chans, pos, true, false, true);
            // Caída lenta: se mide en la toma (recta de mínimos cuadrados del nivel cada 50 ms, desde que empieza a
            // caer hasta soltar) y se le quita a toda la toma; BeTracks Pro la pone de nuevo antes y dentro del bucle,
            // así que la nota suena igual y el bucle no salta de nivel.
            double slope = 0;
            if (plan.decayLoops)
            {
                const int win = int (0.05 * kRate), from = pre + int (std::max (1.0, plan.loopFromSec) * kRate);
                double sx = 0, sy = 0, sxx = 0, sxy = 0;
                int n = 0;
                for (int a = from; a + win <= t.noteOffAt; a += win)
                {
                    const double x = double (a - pre) / kRate, y = 20.0 * std::log10 (std::max (1.0e-9f, bk::autosample::rmsOf (t.audio, a, win)));
                    sx += x; sy += y; sxx += x * x; sxy += x * y; ++n;
                }
                if (n > 4) slope = std::clamp (-(n * sxy - sx * sy) / (n * sxx - sx * sx), 0.0, 40.0);   // dB/s que cae
                if (slope > 0.2)
                    for (int c = 0; c < chans; ++c)
                    {
                        float* d = t.audio.getWritePointer (c);
                        for (int i = pre; i < pos; ++i) d[i] *= float (std::pow (10.0, slope * double (i - pre) / kRate / 20.0));
                    }
                else slope = 0;
            }
            // Lo que Keys genera con la nota sostenida se sostiene: el bucle es obligatorio (si no, la nota se cortaría).
            const auto res = bk::autosample::process (t, plan.loops, plan.loops ? -1.0 : 0.9,
                                                      plan.loopFromSec > 0 ? pre + int (plan.loopFromSec * kRate) : -1);
            ++done;
            if (progress != nullptr) *progress = double (done) / total;
            if (res.silent) continue;
            Take tk;
            tk.audio = res.audio;
            tk.note = note;
            tk.vel = vels[vi];
            tk.velLow = vi == 0 ? 1 : (vels[vi - 1] + vels[vi]) / 2 + 1;
            tk.velHigh = vi + 1 == vels.size() ? 127 : (vels[vi] + vels[vi + 1]) / 2;
            tk.loops = res.loops;
            tk.loopStart = res.loopStart;
            tk.loopEnd = res.loopEnd;
            tk.decay = res.loops ? slope : 0.0;
            for (int c = 0; c < tk.audio.getNumChannels(); ++c) peak = std::max (peak, tk.audio.getMagnitude (c, 0, tk.audio.getNumSamples()));
            takes.push_back (std::move (tk));
        }
    if (takes.empty()) { error = "No suena ninguna nota con estos ajustes."; return nullptr; }

    auto s = std::make_unique<bk::nsmp::SampleSound>();
    s->codec = 0;
    s->releaseUnlooped = true;
    const float scale = 30000.0f / std::max (peak, 1.0e-6f);
    for (size_t i = 0; i < takes.size(); ++i)
    {
        const auto& tk = takes[i];
        bk::nsmp::SampleSound::Stroke st;
        st.channels = tk.audio.getNumChannels();
        st.frames = uint32_t (tk.audio.getNumSamples());
        st.sampleRate = kRate;
        st.loops = tk.loops;
        st.loopStart = uint32_t (tk.loopStart);
        st.loopEnd = uint32_t (tk.loopEnd);
        st.loopDecayDbPerSecond = tk.decay;
        st.preLoopDecayDbPerSecond = tk.decay;
        st.pcm.resize (size_t (st.frames) * size_t (st.channels));
        for (uint32_t f = 0; f < st.frames; ++f)
            for (int c = 0; c < st.channels; ++c)
                st.pcm[size_t (f) * size_t (st.channels) + size_t (c)] = int16_t (std::lround (jlimit (-32767.0f, 32767.0f, tk.audio.getSample (c, int (f)) * scale)));
        s->strokes.push_back (std::move (st));
    }
    // Zonas: cada nota grabada cubre hasta la mitad del camino a sus vecinas, por capa de velocidad.
    std::map<int, std::vector<size_t>> byLayer;
    for (size_t i = 0; i < takes.size(); ++i) byLayer[takes[i].velLow].push_back (i);
    for (auto& [v, idx] : byLayer)
        for (size_t j = 0; j < idx.size(); ++j)
        {
            const auto& tk = takes[idx[j]];
            bk::nsmp::SampleSound::Zone z { tk.note, j == 0 ? 0 : (takes[idx[j - 1]].note + tk.note) / 2 + 1,
                                            j + 1 == idx.size() ? 127 : (tk.note + takes[idx[j + 1]].note) / 2,
                                            tk.velLow, tk.velHigh, int (idx[j]), false };
            if (vels.size() > 1) z.velRoot = tk.vel;
            s->zones.push_back (z);
        }
    return s;
}

} // namespace

Probe probeFm (const FmSound& sound)
{
    return probeWith ([&] { return std::make_unique<FmPlayer> (sound); });
}

std::unique_ptr<bk::nsmp::SampleSound> renderFm (const FmSound& sound, const Plan& plan, std::atomic<double>* progress,
                                                 std::string& error)
{
    return renderWith ([&] { return std::make_unique<FmPlayer> (sound); }, plan, progress, error);
}

std::unique_ptr<bk::nsmp::SampleSound> renderOrgan (const bk::organ::OrganParams& params, bool rotary, float drive,
                                                    const Plan& plan, std::atomic<double>* progress, std::string& error)
{
    return renderWith ([&] { return std::make_unique<OrganPlayer> (params, rotary, drive); }, plan, progress, error);
}

std::unique_ptr<bk::nsmp::SampleSound> renderSynth (const bk::synth::SynthParams& params, const bk::nsmp::SampleSound* sample,
                                                    const Plan& plan, std::atomic<double>* progress, std::string& error)
{
    return renderWith ([&] { return std::make_unique<SynthPlayer> (params, sample); }, plan, progress, error);
}

} // namespace bk::selfsample
