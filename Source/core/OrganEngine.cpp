// BeTracks Keys — motor de órgano. Port de OpenNord (AGPL-3.0); ver OrganEngine.h.
#include "OrganEngine.h"

// Las comparaciones exactas de ganancias son intencionadas (¿cambió el objetivo?), como en OpenNord.
#pragma clang diagnostic ignored "-Wfloat-equal"

#include <algorithm>
#include <cmath>

namespace bk::organ
{
namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr int kTableBits = 11, kTableSize = 1 << kTableBits;
constexpr int kSpanMax = 256;
constexpr int kControlPeriod = 64;

constexpr double kLevelMakeup = 3.981071705534972; // 10^(12/20)
constexpr double kLevelKnee = 6, kLevelSlope = 0.5, kLevelReleaseMs = 150, kModelXfadeMs = 20;

constexpr double kPercFastTau = 0.252, kPercFastDrain = 2.865;
constexpr double kPercSlowTau = 0.869, kPercSlowDrain = 0.967;
constexpr double kPercRechargeTau = 0.0258, kPercRechargeTarget = 1.0112;
constexpr float kPercLevelNormal = 1.0f, kPercLevelSoft = 0.231f, kPercLevelSlow = 0.69f, kPercNormalManualDrop = 0.7f;
constexpr double kClickTau = 0.0012, kChiffTau = 0.03;
constexpr float kClickScale = 0.35f;
constexpr float kVolumeOffDb = -120.0f;

// ── Ruedas fónicas (conocimiento público del Hammond B-3; tonewheel.ts) ─────────────────────
constexpr double kMotorRps = 20;
constexpr int kGear[12][2] = { { 85, 104 }, { 71, 82 }, { 67, 73 }, { 105, 108 }, { 103, 100 }, { 84, 77 },
                               { 74, 64 }, { 98, 80 }, { 96, 74 }, { 88, 64 }, { 67, 46 }, { 108, 70 } };
constexpr int kWheelCount = 91, kManualLowestWheel = 13, kComplexWheels = 12, kWheelMidiOffset = 23;
constexpr int kFootageSemis[9] = { -12, 7, 0, 12, 19, 24, 28, 31, 36 };
constexpr int kDrawbar1ft = 8, kPerc2nd = 3, kPerc3rd = 4;
constexpr int kDrawbarSteps[9] = { 0, 3, 6, 13, 22, 39, 62, 87, 127 };

double tonewheelFrequency (int wheel)
{
    const int i = wheel - 1;
    if (wheel <= 84) return kMotorRps * double (2 << (i / 12)) * kGear[i % 12][0] / kGear[i % 12][1];
    const auto& g = kGear[(i % 12) + 5];
    return kMotorRps * 192.0 * g[0] / g[1];
}

double equalTempered (int midi) { return 440.0 * std::pow (2.0, (midi - 69) / 12.0); }

int wheelFor (int midi, int drawbar, int lowestWheel)
{
    int w = midi - kWheelMidiOffset + kFootageSemis[drawbar];
    while (w > kWheelCount) w -= 12;
    while (w < lowestWheel) w += 12;
    return w;
}

float drawbarGain (float level)
{
    if (! (level > 0)) return 0;
    const float l = std::min (8.0f, level);
    const int i = int (l);
    const float lo = float (kDrawbarSteps[i]), hi = float (kDrawbarSteps[std::min (8, i + 1)]);
    return (lo + (hi - lo) * (l - float (i))) / 127.0f;
}

int foldMidi (int m, int lo, int hi)
{
    while (m > hi) m -= 12;
    while (m < lo) m += 12;
    return m;
}

// ── Timbres (amplitud de cada armónico; voicing.ts) ─────────────────────────────────────────
std::vector<float> series (int n, double (*f) (int))
{
    std::vector<float> v;
    for (int h = 1; h <= n; ++h) v.push_back (float (f (h)));
    return v;
}

enum Timbre { WheelComplex, WheelLow, WheelLowMid, WheelUpper, VoxFlute, VoxReed,
              FarfFlute, FarfOboe, FarfTrumpet, FarfStrings, Pipe1Rank0, Pipe2Rank0 = Pipe1Rank0 + 9, TimbreCount = Pipe2Rank0 + 9 };

const std::vector<float>& harmonics (int timbre)
{
    static const std::vector<float> complex { 1, 0.15f, 0.525f, 0.061f, 0.243f, 0.009f, 0.11f, 0.013f, 0.05f, 0.014f, 0.021f };
    static const std::vector<float> low { 1, 0.164f, 0.027f, 0.006f };
    static const std::vector<float> lowMid { 1, 0.112f, 0.014f, 0.006f };
    static const std::vector<float> upper { 1, 0.018f, 0.0018f };
    static const std::vector<float> flute { 1, 0.12f, 0.05f, 0.02f };
    static const std::vector<float> stopped { 1, 0, 0.3f, 0, 0.12f, 0, 0.05f };
    static const std::vector<float> principal { 1, 0.55f, 0.35f, 0.22f, 0.14f, 0.09f, 0.06f, 0.04f };
    static const std::vector<float> reed = series (24, [] (int h) { return 1.0 / h; });
    static const std::vector<float> mellow = series (12, [] (int h) { return h % 2 ? 1.0 / (h * h) : 0.0; });
    static const std::vector<float> oboe = series (16, [] (int h) { return ((h >= 2 && h <= 4) ? 0.9 : 1.0 / h) * (h == 1 ? 0.6 : 1.0); });
    static const std::vector<float> trumpet = series (20, [] (int h) { return h <= 6 ? 0.8 + 0.05 * h : 3.0 / h; });
    static const std::vector<float> strings = series (32, [] (int h) { return (h == 1 ? 0.35 : 1.0 / std::sqrt (double (h))) * 0.6; });
    static const std::vector<float>* pipe1[9] = { &stopped, &flute, &principal, &principal, &flute, &principal, &flute, &flute, &principal };
    static const std::vector<float>* pipe2[9] = { &stopped, &stopped, &flute, &flute, &flute, &flute, &flute, &flute, &flute };
    switch (timbre)
    {
        case WheelComplex: return complex;
        case WheelLow: return low;
        case WheelLowMid: return lowMid;
        case WheelUpper: return upper;
        case VoxFlute: return mellow;
        case VoxReed: return reed;
        case FarfFlute: return flute;
        case FarfOboe: return oboe;
        case FarfTrumpet: return trumpet;
        case FarfStrings: return strings;
        default: break;
    }
    if (timbre >= Pipe2Rank0) return *pipe2[timbre - Pipe2Rank0];
    return *pipe1[timbre - Pipe1Rank0];
}

constexpr double kHeadroom = 0.07943282347242814; // 10^(-22/20)

constexpr int voxFootages[4][4] = { { -12 }, { 0 }, { 12 }, { 19, 24, 28, 36 } };
constexpr int voxParts[4] = { 1, 1, 1, 4 };
struct FarfTab { int semis, timbre; float gain; };
constexpr FarfTab farfTabs[9] = { { -12, 0, 0.8f }, { -12, 3, 0.7f }, { 0, 0, 0.8f }, { 0, 1, 0.7f }, { 0, 2, 0.7f },
                                  { 0, 3, 0.7f }, { 12, 0, 0.7f }, { 12, 3, 0.6f }, { 19, 0, 0.6f } };
constexpr double pipeRatio[9] = { 0.5, 1.5, 1, 2, 3, 4, 5, 6, 8 };

/** Id de generador fuera del B3: timbre local × 128 + nota. */
constexpr int genId (int timbre, int midi) { return timbre * 128 + midi; }

/** Lo que alimenta un generador: frecuencia y timbre global. freq 0 = no existe. */
struct Tone { double freq; int timbre; };
Tone toneFor (Model m, int id)
{
    switch (m)
    {
        case Model::B3:
        case Model::B3Bass:
        {
            if (id < 1 || id > kWheelCount) return { 0, 0 };
            const int t = id <= kComplexWheels ? WheelComplex : id <= 16 ? WheelLow : id <= 24 ? WheelLowMid : WheelUpper;
            return { tonewheelFrequency (id), t };
        }
        case Model::Vox:
            if (id / 128 > 1) return { 0, 0 };
            return { equalTempered (id % 128), id / 128 == 0 ? VoxFlute : VoxReed };
        case Model::Farf:
            if (id / 128 > 3) return { 0, 0 };
            return { equalTempered (id % 128), FarfFlute + id / 128 };
        case Model::Pipe1:
        case Model::Pipe2:
        {
            const int rank = id / 128;
            if (rank > 8) return { 0, 0 };
            return { equalTempered (id % 128) * pipeRatio[rank], (m == Model::Pipe1 ? Pipe1Rank0 : Pipe2Rank0) + rank };
        }
    }
    return { 0, 0 };
}

/** Cierra un contacto: añade [gen, ganancia, …] a `out`; devuelve cuántos pares. */
int route (Model m, int midi, int bus, const OrganParams& p, int* gens, float* gains)
{
    int n = 0;
    switch (m)
    {
        case Model::B3:
        case Model::B3Bass:
        {
            // La percusión toma el contacto de 1′: ese drawbar calla mientras está encendida.
            if (bus == kDrawbar1ft && p.percussion.on && m == Model::B3) return 0;
            const float g = drawbarGain (p.drawbars[size_t (bus)]);
            if (g > 0) { gens[n] = wheelFor (midi, bus, m == Model::B3Bass ? 1 : kManualLowestWheel); gains[n++] = g; }
            return n;
        }
        case Model::Vox:
        {
            const float foot = drawbarGain (p.drawbars[size_t (bus)]);
            if (foot <= 0) return 0;
            const float flute = drawbarGain (p.drawbars[4]);
            const float reed = drawbarGain (p.drawbars[5]) * 0.6f;
            const int parts = voxParts[bus];
            const float norm = 1.0f / std::sqrt (float (parts));
            for (int k = 0; k < parts; ++k)
            {
                const int mm = foldMidi (midi + voxFootages[bus][k], 24, 120);
                if (flute > 0) { gens[n] = genId (0, mm); gains[n++] = foot * flute * norm; }
                if (reed > 0) { gens[n] = genId (1, mm); gains[n++] = foot * reed * norm; }
            }
            return n;
        }
        case Model::Farf:
        {
            if (p.drawbars[size_t (bus)] < 4) return 0; // pestaña: arriba o abajo
            const auto& tab = farfTabs[bus];
            gens[n] = genId (tab.timbre, foldMidi (midi + tab.semis, 24, 120));
            gains[n++] = tab.gain;
            return n;
        }
        case Model::Pipe1:
        case Model::Pipe2:
        {
            const float g = drawbarGain (p.drawbars[size_t (bus)]);
            if (g <= 0) return 0;
            int mm = midi;
            while (equalTempered (mm) * pipeRatio[bus] > 8000) mm -= 12;
            while (equalTempered (mm) * pipeRatio[bus] < 30) mm += 12;
            gens[n] = genId (bus, foldMidi (mm, 0, 127));
            gains[n++] = g;
            return n;
        }
    }
    return 0;
}

uint32_t hashPhaseBits (uint32_t seed, uint32_t id)
{
    uint32_t h = ((id + 1u) * 0x85ebca6bu) ^ (seed * 0xc2b2ae35u);
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    return h;
}

float softClip (float x)
{
    const float a = std::abs (x);
    if (a <= 0.6f) return x;
    return std::copysign (0.6f + 0.4f * std::tanh ((a - 0.6f) / 0.4f), x);
}

bool sameParams (const OrganParams& a, const OrganParams& b)
{
    return a.model == b.model && a.drawbars == b.drawbars && a.percussion.on == b.percussion.on
        && a.percussion.harm3rd == b.percussion.harm3rd && a.percussion.decayFast == b.percussion.decayFast
        && a.percussion.volSoft == b.percussion.volSoft && a.vibOn == b.vibOn && a.vibType == b.vibType
        && a.volumeDb == b.volumeDb && a.octaveShift == b.octaveShift && a.sustain == b.sustain
        && a.sustainPedal == b.sustainPedal && a.keyClick == b.keyClick;
}
} // namespace

// ── Voicing por modelo (voicing.ts) ─────────────────────────────────────────────────────────
struct OrganEngine::Voicing
{
    Model model;
    int buses;
    double attackMs, releaseMs, sustainReleaseMs, contactScatterMs;
    bool keyClick;
    float chiff;
    bool percussion;
    int lowestWheel;
    double vibRateHz;
    bool vibTriangle;
    float level;
};

static const OrganEngine::Voicing* voicings()
{
    using V = OrganEngine::Voicing;
    static const V v[6] = {
        { Model::B3,     9, 0.15, 0.3, 500, 3, true,  0,     true,  kManualLowestWheel, 6.9, true,  float (0.11 * kHeadroom) },
        { Model::Vox,    4, 2,    8,   500, 0, false, 0,     false, kManualLowestWheel, 6.1, true,  float (0.10 * kHeadroom) },
        { Model::Farf,   9, 1.5,  6,   500, 0, false, 0,     false, kManualLowestWheel, 6.2, false, float (0.10 * kHeadroom) },
        { Model::Pipe1,  9, 35,   90,  900, 0, false, 0.05f, false, kManualLowestWheel, 5.2, false, float (0.09 * kHeadroom) },
        { Model::Pipe2,  9, 55,   90,  900, 0, false, 0.03f, false, kManualLowestWheel, 5.2, false, float (0.09 * kHeadroom) },
        { Model::B3Bass, 9, 0.15, 0.3, 500, 3, true,  0,     false, 1,                  6.9, true,  float (0.11 * kHeadroom) },
    };
    return v;
}

const OrganEngine::Voicing& OrganEngine::voicing() const { return voicings()[int (params_.model)]; }

OrganEngine::OrganEngine() : lvl_ (float (kLevelMakeup)), lvlTarget_ (float (kLevelMakeup)) {}
OrganEngine::~OrganEngine() = default;

void OrganEngine::prepare (double sampleRate)
{
    sampleRate_ = sampleRate > 0 ? sampleRate : 48000;
    tables_.clear();
    // Tabla de un ciclo, limitada a los armónicos que caben, con el RMS de un seno unidad.
    auto tableFor = [this] (int timbre, double freq) -> const float*
    {
        const auto& h = harmonics (timbre);
        int n = 0;
        for (int k = 1; k <= int (h.size()); ++k)
            if (k * freq < 0.45 * sampleRate_ && h[size_t (k - 1)] != 0) n = k;
        auto& t = tables_[{ timbre, n }];
        if (t.empty())
        {
            t.assign (kTableSize + 1, 0.0f);
            double sq = 0;
            for (int i = 0; i < kTableSize; ++i)
            {
                double v = 0;
                for (int k = 1; k <= n; ++k) v += h[size_t (k - 1)] * std::sin (2 * kPi * k * i / kTableSize);
                t[size_t (i)] = float (v);
                sq += v * v;
            }
            const double rms = std::sqrt (sq / kTableSize);
            const double norm = rms > 0 ? std::sqrt (0.5) / rms : 0;
            for (int i = 0; i < kTableSize; ++i) t[size_t (i)] = float (t[size_t (i)] * norm);
            t[kTableSize] = t[0];
        }
        return t.data();
    };
    for (int m = 0; m < 6; ++m)
    {
        auto& s = specs_[size_t (m)];
        s.assign (kMaxGenId, {});
        for (int id = 0; id < kMaxGenId; ++id)
        {
            const auto tone = toneFor (Model (m), id);
            if (tone.freq > 0) s[size_t (id)] = { tableFor (tone.timbre, tone.freq), tone.freq };
        }
    }
    for (auto& b : banks_) b.assign (kMaxGenId, {});
    active_.assign (2 * kMaxGenId, nullptr);
    activeCount_ = 0;
    queue_.clear();
    queue_.reserve (4096);
    main_.assign (kSpanMax, 0);
    perc_.assign (kSpanMax, 0);
    keys_ = {};
    clock_ = 0;
    downCount_ = 0;
    rng_ = seed_ ? seed_ : 0x9e3779b9u;
    clickDecay_ = float (std::exp (-1.0 / (kClickTau * sampleRate_)));
    chiffDecay_ = float (std::exp (-1.0 / (kChiffTau * sampleRate_)));
    lvlRise_ = float (1.0 - std::exp (-1.0 / (kLevelReleaseMs * sampleRate_ / 1000.0)));
    scanBase_ = 0.05 * sampleRate_ / 1000.0;
    scanBuf_.fill (0);
    prepared_ = true;
    auto p = params_;
    params_.model = p.model == Model::B3 ? Model::Vox : Model::B3; // fuerza el cálculo completo
    setParams (p);
}

float OrganEngine::nextRandom()
{
    rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5;
    return float (double (rng_) / 4294967296.0);
}

void OrganEngine::setParams (const OrganParams& p)
{
    if (! prepared_) { params_ = p; return; }
    if (sameParams (p, params_)) return;
    const bool modelChanged = p.model != params_.model;
    params_ = p;
    if (modelChanged) revoice();
    const auto& v = voicing();
    const double tau = p.percussion.decayFast ? kPercFastTau : kPercSlowTau;
    const double drain = p.percussion.decayFast ? kPercFastDrain : kPercSlowDrain;
    percDecay_ = std::exp (-1.0 / (tau * sampleRate_));
    percDrain_ = drain / sampleRate_;
    percRecharge_ = std::exp (-1.0 / (kPercRechargeTau * sampleRate_));
    // Scanner: retardo modulado, profundidad 1–3; vibrato solo húmedo, chorus mezclado con el seco.
    static const double depthMs[3] = { 0.35, 0.65, 1.0 };
    scanOn_ = p.vibOn;
    scanChorus_ = int (p.vibType) >= int (VibType::C1);
    scanDepth_ = depthMs[int (p.vibType) % 3] * sampleRate_ / 1000.0;
    scanInc_ = v.vibRateHz / sampleRate_;
    scanTriangle_ = v.vibTriangle;
    const bool percOn = v.percussion && p.percussion.on;
    manualGain_ = percOn && ! p.percussion.volSoft ? kPercNormalManualDrop : 1.0f;
    percLevel_ = percOn ? (p.percussion.volSoft ? kPercLevelSoft : kPercLevelNormal) * (p.percussion.decayFast ? 1.0f : kPercLevelSlow) : 0.0f;
    vol_ = (std::isfinite (p.volumeDb) && p.volumeDb > kVolumeOffDb ? std::pow (10.0f, p.volumeDb / 20.0f) : 0.0f) * v.level;
    dirty_ = true;
}

OrganEngine::Gen& OrganEngine::gen (int id)
{
    auto& g = banks_[size_t (bank_)][size_t (id)];
    if (! g.active)
    {
        const auto& spec = specs_[size_t (int (params_.model))][size_t (id)];
        g = Gen {};
        g.table = spec.table;
        g.inc = spec.freq / sampleRate_;
        g.phase0 = hashPhaseBits (seed_, uint32_t (id)) / 4294967296.0;
        g.active = true;
        if (activeCount_ < int (active_.size())) active_[size_t (activeCount_++)] = &g;
    }
    return g;
}

void OrganEngine::revoice()
{
    // El banco del modelo anterior se apaga en 20 ms; el nuevo empieza vacío.
    const int next = 1 - bank_;
    int w = 0;
    for (int i = 0; i < activeCount_; ++i)
    {
        Gen* g = active_[size_t (i)];
        const bool inNext = g >= banks_[size_t (next)].data() && g < banks_[size_t (next)].data() + kMaxGenId;
        if (inNext) { g->active = false; continue; } // huérfano de un cambio anterior: se corta
        g->orphan = true;
        active_[size_t (w++)] = g;
    }
    activeCount_ = w;
    for (auto& g : banks_[size_t (next)]) g = Gen {};
    bank_ = next;
    const uint32_t all = (1u << voicing().buses) - 1u;
    for (auto& k : keys_)
    {
        if (! k.exists) continue;
        if (k.down) k.mask = all;
        else k = Key {};
    }
    queue_.clear();
}

void OrganEngine::noteOn (int note)
{
    if (note < 0 || note > 127) return;
    auto& k = keys_[size_t (note)];
    if (k.exists && k.down) return;
    k.exists = true;
    k.down = true;
    ++downCount_;
    cancelEvents (note);
    scheduleContacts (note, true);
    if (voicing().chiff > 0) chiffEnv_ += voicing().chiff;
    dirty_ = true;
}

void OrganEngine::noteOff (int note)
{
    if (note < 0 || note > 127) return;
    auto& k = keys_[size_t (note)];
    if (! k.exists || ! k.down) return;
    k.down = false;
    --downCount_;
    cancelEvents (note);
    scheduleContacts (note, false);
    dirty_ = true;
}

void OrganEngine::allNotesOff()
{
    keys_ = {};
    downCount_ = 0;
    queue_.clear();
    dirty_ = true;
}

void OrganEngine::cancelEvents (int note)
{
    queue_.erase (std::remove_if (queue_.begin(), queue_.end(), [note] (const Event& e) { return e.note == note; }), queue_.end());
}

void OrganEngine::scheduleContacts (int note, bool close)
{
    const double scatter = voicing().contactScatterMs * sampleRate_ / 1000.0;
    for (int bus = 0; bus < voicing().buses; ++bus)
    {
        const Event e { clock_ + int64_t (std::lround (nextRandom() * scatter)), note, bus, close };
        if (queue_.size() >= queue_.capacity()) return; // no debería pasar: 128 teclas × 9 contactos
        queue_.insert (std::upper_bound (queue_.begin(), queue_.end(), e, [] (const Event& a, const Event& b) { return a.at < b.at; }), e);
    }
}

void OrganEngine::applyEvent (const Event& e)
{
    auto& k = keys_[size_t (e.note)];
    if (! k.exists) return;
    const uint32_t bit = 1u << e.bus;
    const bool was = (k.mask & bit) != 0;
    k.mask = e.close ? (k.mask | bit) : (k.mask & ~bit);
    if (was != e.close && voicing().keyClick)
    {
        // Un contacto con señal hace más click que uno en vacío.
        const float live = params_.drawbars[size_t (e.bus)] > 0 ? 1.0f : 0.25f;
        clickEnv_ += params_.keyClick * kClickScale * live / float (voicing().buses);
    }
    if (! k.down && k.mask == 0) k = Key {};
}

void OrganEngine::recompute()
{
    for (int i = 0; i < activeCount_; ++i) { active_[size_t (i)]->nt = 0; active_[size_t (i)]->np = 0; }
    const auto& v = voicing();
    const int shift = 12 * params_.octaveShift;
    int gens[16];
    float gains[16];
    for (int note = 0; note < 128; ++note)
    {
        const auto& k = keys_[size_t (note)];
        if (! k.exists) continue;
        const int midi = note + shift;
        for (int bus = 0; bus < v.buses; ++bus)
        {
            if (! (k.mask & (1u << bus))) continue;
            const int n = route (params_.model, midi, bus, params_, gens, gains);
            for (int i = 0; i < n; ++i)
                if (gens[i] > 0 && gens[i] < kMaxGenId && specs_[size_t (int (params_.model))][size_t (gens[i])].table)
                    gen (gens[i]).nt += gains[i];
        }
        if (k.down && v.percussion && params_.percussion.on)
            gen (wheelFor (midi, params_.percussion.harm3rd ? kPerc3rd : kPerc2nd, v.lowestWheel)).np += 1;
    }
    const double sr = sampleRate_ / 1000.0;
    const double rel = (params_.sustain ? v.sustainReleaseMs : v.releaseMs) * sr;
    const double att = v.attackMs * sr;
    const double xfade = kModelXfadeMs * sr;
    for (int i = 0; i < activeCount_; ++i)
    {
        Gen& g = *active_[size_t (i)];
        if (g.orphan)
        {
            if (g.target != 0 || g.ptarget != 0)
            {
                g.target = 0; g.step = float (g.gain / xfade);
                g.ptarget = 0; g.pstep = float (g.pgain / xfade);
            }
            continue;
        }
        if (g.nt != g.target)
        {
            g.step = float (std::abs (g.nt - g.gain) / std::max (1.0, g.nt > g.gain ? att : rel));
            g.target = g.nt;
        }
        if (g.np != g.ptarget)
        {
            g.pstep = float (std::abs (g.np - g.pgain) / std::max (1.0, g.np > g.pgain ? att : v.releaseMs * sr));
            g.ptarget = g.np;
        }
    }
}

void OrganEngine::updateLeveller()
{
    double manual = 0, perc = 0;
    for (int i = 0; i < activeCount_; ++i)
    {
        const Gen& g = *active_[size_t (i)];
        manual += std::max (g.gain, g.target);
        perc += std::max (g.pgain, g.ptarget);
    }
    const double drive = manual * manualGain_ + perc * percLevel_ * percCharge_ * percCharge_;
    const double target = kLevelMakeup / (drive > kLevelKnee ? std::pow (drive / kLevelKnee, kLevelSlope) : 1.0);
    if (target < lvl_)
    {
        const double a = std::max (1.0, voicing().attackMs * sampleRate_ / 4000.0);
        lvlStep_ = float ((lvl_ - target) / a);
    }
    lvlTarget_ = float (target);
}

void OrganEngine::render (float* out, int frames)
{
    if (! prepared_) { std::fill (out, out + frames, 0.0f); return; }
    int pos = 0;
    while (pos < frames)
    {
        while (! queue_.empty() && queue_.front().at <= clock_)
        {
            const Event e = queue_.front();
            queue_.erase (queue_.begin());
            applyEvent (e);
            dirty_ = true;
        }
        if (dirty_) { recompute(); dirty_ = false; lvlDirty_ = true; }
        if (lvlDirty_ || clock_ % kControlPeriod == 0) { updateLeveller(); lvlDirty_ = false; }
        int n = std::min ({ frames - pos, kSpanMax, int (kControlPeriod - clock_ % kControlPeriod) });
        if (! queue_.empty()) n = std::min (n, int (std::max<int64_t> (1, queue_.front().at - clock_)));
        renderSpan (out + pos, n);
        pos += n;
        clock_ += n;
    }
}

void OrganEngine::renderSpan (float* out, int n)
{
    float* main = main_.data();
    float* perc = perc_.data();
    std::fill (main, main + n, 0.0f);
    std::fill (perc, perc + n, 0.0f);
    for (int a = 0; a < activeCount_; ++a)
    {
        Gen& g = *active_[size_t (a)];
        double ph = g.phase0 + g.inc * double (clock_);
        ph -= std::floor (ph);
        const float* t = g.table;
        const double inc = g.inc;
        float gain = g.gain, pg = g.pgain;
        const float target = g.target, step = g.step, pt = g.ptarget, ps = g.pstep;
        const bool hasPerc = pg > 0 || pt > 0;
        for (int i = 0; i < n; ++i)
        {
            const double x = ph * kTableSize;
            const int j = int (x);
            const float s0 = t[j];
            const float s = s0 + (t[j + 1] - s0) * float (x - j);
            if (gain != target) gain = gain < target ? std::min (target, gain + step) : std::max (target, gain - step);
            main[i] += s * gain;
            if (hasPerc)
            {
                if (pg != pt) pg = pg < pt ? std::min (pt, pg + ps) : std::max (pt, pg - ps);
                perc[i] += s * pg;
            }
            ph += inc;
            if (ph >= 1) ph -= std::floor (ph);
        }
        g.gain = gain;
        g.pgain = pg;
    }
    // Fuera los generadores que callaron.
    int w = 0;
    for (int a = 0; a < activeCount_; ++a)
    {
        Gen* g = active_[size_t (a)];
        if (g->gain > 0 || g->target > 0 || g->pgain > 0 || g->ptarget > 0) active_[size_t (w++)] = g;
        else g->active = false;
    }
    activeCount_ = w;

    const bool draining = downCount_ > 0;
    double charge = percCharge_;
    float lvl = lvl_;
    for (int i = 0; i < n; ++i)
    {
        // Scanner (vibrato/chorus).
        const float x = main[i] * manualGain_;
        scanBuf_[size_t (scanW_)] = x;
        float s = x;
        if (scanOn_)
        {
            const double l = scanLfo_;
            const double u = scanTriangle_ ? (l < 0.5 ? 2 * l : 2 - 2 * l) : 0.5 - 0.5 * std::cos (2 * kPi * l);
            const double r = scanW_ - (scanBase_ + scanDepth_ * u);
            const int ri = int (std::floor (r));
            const float f = float (r - ri);
            const float sa = scanBuf_[size_t (ri & 1023)], sb = scanBuf_[size_t ((ri + 1) & 1023)];
            const float wet = sa + (sb - sa) * f;
            scanLfo_ += scanInc_;
            if (scanLfo_ >= 1) scanLfo_ -= 1;
            s = scanChorus_ ? 0.6f * (x + wet) : wet;
        }
        scanW_ = (scanW_ + 1) & 1023;

        // Percusión: la carga se gasta mientras haya teclas pulsadas y se recarga con el teclado libre.
        if (draining) { charge = charge * percDecay_ - percDrain_; if (charge < 0) charge = 0; }
        else { charge = kPercRechargeTarget + (charge - kPercRechargeTarget) * percRecharge_; if (charge > 1) charge = 1; }
        if (percLevel_ > 0) s += perc[i] * float (charge * charge) * percLevel_;

        if (clickEnv_ > 1.0e-5f)
        {
            const float noise = nextRandom() * 2 - 1;
            s += (noise - lastNoise_) * clickEnv_; // ruido derivado: brillante y corto
            lastNoise_ = noise;
            clickEnv_ *= clickDecay_;
        }
        if (chiffEnv_ > 1.0e-5f)
        {
            chiffLp_ += 0.25f * (nextRandom() * 2 - 1 - chiffLp_);
            s += chiffLp_ * chiffEnv_;
            chiffEnv_ *= chiffDecay_;
        }
        if (lvl > lvlTarget_) lvl = std::max (lvlTarget_, lvl - lvlStep_);
        else if (lvl < lvlTarget_) lvl += (lvlTarget_ - lvl) * lvlRise_;
        out[i] = softClip (s * vol_ * lvl);
    }
    percCharge_ = charge;
    lvl_ = lvl;
}

float OrganEngine::percussionEnvelope() const
{
    return voicing().percussion && params_.percussion.on ? float (percCharge_ * percCharge_) : 0.0f;
}
} // namespace bk::organ
