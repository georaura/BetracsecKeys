#include "FmxEngine.h"
#include "FmxAlgorithms.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace bk::fmx
{
namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr int kBlock = 64;                 // muestras por paso de envolvente (como msfa, N = 64)
constexpr float kOutLevel = 0.04f;         // nivel de un portador a tope (amplitud 2 del DX7)

// ── Tablas ──────────────────────────────────────────────────────────────────────────────────────
constexpr int kSinBits = 10, kSinSize = 1 << kSinBits;
constexpr int kFormBits = 10, kFormSize = 1 << kFormBits;
float gSin[kSinSize + 1];
std::once_flag gTablesOnce;

// Formas espectrales según las medidas del FS1R de FSVR (docs/skirt.md y docs/formant.md, GPL-3.0):
//  all1/all2: un grupo que empieza en el parcial 1 (con skirt 0, un solo parcial: un seno);
//  odd1/odd2: lo mismo en los parciales impares (con skirt 0, el 1 y el 3 a −9,6 dB);
//  res1/res2: un grupo de tres líneas centrado en el armónico resonancia + 1 (laterales a −6 dB).
// El skirt abre el grupo; los "2" mucho más que los "1". Anchura equivalente medida (potencia total
// respecto a la del parcial más fuerte) para skirt 0..7; aquí se reparte como una gaussiana con ese ancho.
constexpr double kWidth[6][8] = {
    { 1.00, 1.01, 1.05, 1.11, 1.18, 1.24, 1.29, 1.34 },   // all1
    { 1.00, 1.05, 1.25, 1.58, 2.14, 3.13, 4.35, 6.52 },   // all2
    { 1.13, 1.20, 1.26, 1.35, 1.38, 1.55, 1.53, 1.70 },   // odd1
    { 1.13, 1.29, 1.43, 1.97, 2.65, 3.42, 5.06, 7.31 },   // odd2
    { 1.45, 1.80, 1.84, 2.24, 2.46, 2.34, 2.47, 2.57 },   // res1
    { 1.48, 1.87, 2.46, 4.00, 4.57, 7.43, 9.08, 13.41 },  // res2
};
std::mutex gFormMutex;
std::map<int, std::unique_ptr<float[]>> gForms;   // nunca se borran: el audio guarda punteros

/** σ (en líneas) de una gaussiana de potencia exp(−k²/σ²) cuya suma es `width`. */
double sigmaForWidth (double width, bool twoSided)
{
    auto sum = [twoSided] (double sg)
    {
        double t = 1;
        for (int k = 1; k < 200; ++k) { const double v = std::exp (-double (k * k) / (sg * sg)); t += twoSided ? 2 * v : v; if (v < 1e-9) break; }
        return t;
    };
    if (width <= 1.0001) return 0;
    double lo = 1e-3, hi = 100;
    for (int i = 0; i < 60; ++i) { const double mid = 0.5 * (lo + hi); (sum (mid) < width ? lo : hi) = mid; }
    return 0.5 * (lo + hi);
}

std::unique_ptr<float[]> makeForm (int form, int skirt, int reso)
{
    const bool res = form >= 5, odd = form == 3 || form == 4;
    const double sg = sigmaForWidth (kWidth[form - 1][skirt], res);
    std::vector<double> amp (200, 0.0);  // por parcial
    const int centre = res ? reso + 1 : 1;
    const int span = sg <= 0 ? 0 : int (std::ceil (sg * 4.5));
    for (int k = res ? -span : 0; k <= span; ++k)
    {
        const double a = sg <= 0 ? (k == 0 ? 1.0 : 0.0) : std::exp (-double (k * k) / (2 * sg * sg));
        int m = centre + (odd ? 2 * k : k);
        if (m == 0) continue;
        m = std::abs (m);                 // lo que cae por debajo se refleja y suma
        if (m < int (amp.size())) amp[size_t (m)] += a;
    }
    std::vector<double> w (kFormSize, 0.0);
    for (int m = 1; m < kFormSize / 4 && m < int (amp.size()); ++m)
        if (amp[size_t (m)] > 1e-4)
            for (int i = 0; i < kFormSize; ++i) w[size_t (i)] += amp[size_t (m)] * std::sin (2 * kPi * m * i / kFormSize);
    double peak = 1e-9;
    for (double v : w) peak = std::max (peak, std::abs (v));
    auto t = std::make_unique<float[]> (kFormSize + 1);
    for (int i = 0; i < kFormSize; ++i) t[size_t (i)] = float (w[size_t (i)] / peak);
    t[kFormSize] = t[0];
    return t;
}

/** Tabla de una forma espectral (nullptr = seno). La crea la primera vez; no llamar desde el audio. */
const float* formTable (int form, int skirt, int resonance)
{
    if (form <= 0 || form > 6) return nullptr;
    skirt = std::clamp (skirt, 0, 7);
    const int reso = form >= 5 ? std::clamp (resonance, 0, 99) : 0;
    const int key = (form * 8 + skirt) * 100 + reso;
    const std::lock_guard<std::mutex> l (gFormMutex);
    auto& t = gForms[key];
    if (t == nullptr) t = makeForm (form, skirt, reso);
    return t.get();
}

inline float lookup (const float* t, int bits, uint32_t ph)
{
    const int shift = 32 - bits;
    const uint32_t i = ph >> shift;
    const float f = float (ph & ((1u << shift) - 1)) * (1.0f / float (1u << shift));
    return t[i] + (t[i + 1] - t[i]) * f;
}

// ── DX7 (msfa / Dexed, Apache-2.0) ─────────────────────────────────────────────────────────────
int scaleOutLevel (int l)
{
    static const int lut[20] = { 0, 5, 9, 13, 17, 20, 23, 25, 27, 29, 31, 33, 35, 37, 39, 41, 42, 43, 45, 46 };
    l = std::clamp (l, 0, 99);
    return l >= 20 ? 28 + l : lut[l];
}

int scaleCurve (int group, int depth, int curve)
{
    static const int expData[33] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 11, 14, 16, 19, 23, 27, 33, 39, 47, 56, 66,
                                     80, 94, 110, 126, 142, 158, 174, 190, 206, 222, 238, 250 };
    int scale;
    if (curve == 0 || curve == 3) scale = (group * depth * 329) >> 12;
    else scale = (expData[std::min (group, 32)] * depth * 329) >> 15;
    return curve < 2 ? -scale : scale;
}

int scaleLevel (int note, int bp, int dl, int dr, int cl, int cr)
{
    const int offset = note - bp - 17;
    if (offset >= 0) return scaleCurve ((offset + 1) / 3, dr, cr);
    return scaleCurve (-(offset - 1) / 3, dl, cl);
}

int scaleVelocity (int velocity, int sensitivity)
{
    static const int data[64] = { 0, 70, 86, 97, 106, 114, 121, 126, 132, 138, 142, 148, 152, 156, 160, 163,
                                   166, 170, 173, 174, 178, 181, 184, 186, 189, 190, 194, 196, 198, 200, 202, 205,
                                   206, 209, 211, 214, 216, 218, 220, 222, 224, 225, 227, 229, 230, 232, 233, 235,
                                   237, 238, 240, 241, 242, 243, 244, 246, 246, 248, 249, 250, 251, 252, 253, 254 };
    const int v = data[std::clamp (velocity, 0, 127) >> 1] - 239;
    const int s = std::abs (sensitivity);
    const int scaled = ((s * v + 7) >> 3) << 4;
    return sensitivity >= 0 ? scaled : -scaled;  // sensibilidad negativa de FM-X: más fuerte cuanto más suave
}

int scaleRate (int note, int sensitivity)
{
    const int x = std::clamp (note / 3 - 7, 0, 31);
    return (sensitivity * x) >> 3;
}

/** Nivel del DX7 equivalente: Yamaha importa las voces del DX7 con el nivel + 2 (6 voces comparadas). */
int dxLevel (int fmxLevel) { return fmxLevel >= 99 ? 99 : std::max (0, fmxLevel - 2); }

/**
 * Paso de envolvente (Q24 por bloque de 64 muestras a 44,1 kHz) para un tiempo de FM-X.
 * Ajustado con 86 tramos de 6 voces del DX7 idénticas a su versión FM-X (Lute, Harp 2, Pick Guitar,
 * Bass 1, Ice Galaxy, Claves; error rms de 2 unidades): el tiempo de FM-X es la duración de recorrer
 * todo el rango audible del operador, T = 43,1 + 6,08 · log2(segundos), y ese rango crece con el nivel
 * del operador (en el DX7 la envolvente se suma al nivel y se corta en el suelo).
 */
double incFromTime (int t, int dxOutLevel)
{
    const double span = std::max (64, 4032 + scaleOutLevel (dxOutLevel) * 32 - 4272);
    const double seconds = std::exp2 ((t - 43.1) / 6.08);
    return span * 65536.0 / (seconds * 44100.0 / kBlock);
}

/** La conversión de Yamaha ya incluye el escalado de tiempos del DX7 en Mi♭4 (nota 63): solo la diferencia. */
double rateScaleFactor (int note, int sensitivity)
{
    return std::exp2 ((scaleRate (note, sensitivity) - scaleRate (63, sensitivity)) / 4.0);
}

/** Tiempo de hold (FS1R, medido en FSVR docs/aeg.md) en segundos. */
double holdSeconds (int t)
{
    static const double pts[11] = { 0, 0.019, 0.0255, 0.045, 0.15, 0.40, 1.363, 3.647, 9.7, 26, 63 };
    if (t <= 0) return 0;
    const double x = std::clamp (t, 0, 100) / 10.0;
    const int i = std::min (int (x), 9);
    return pts[i] + (pts[i + 1] - pts[i]) * (x - i);
}

double pegSemitones (int level)
{
    // DX7 pitch EG (msfa pitchenv_tab), 1/32 de octava por unidad; 50 = 0.
    static const int tab[100] = { -128, -116, -104, -95, -85, -76, -68, -61, -56, -52, -49, -46, -43, -41, -39, -37, -35, -33,
                                  -32, -31, -30, -29, -28, -27, -26, -25, -24, -23, -22, -21, -20, -19, -18, -17, -16, -15,
                                  -14, -13, -12, -11, -10, -9, -8, -7, -6, -5, -4, -3, -2, -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
                                  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32,
                                  33, 34, 35, 38, 40, 43, 46, 49, 53, 58, 65, 73, 82, 92, 103, 115, 127 };
    return tab[std::clamp (level, 0, 99)] / 32.0 * 12.0;
}

double pegSeconds (int t) { return t <= 0 ? 0.0 : 0.002 * std::pow (2.0, t / 8.0); }

double lfoHz (int speed)
{
    speed = std::clamp (speed, 0, 99);
    return speed <= 60 ? 0.0626 + 0.15 * speed : 9.06 * std::pow (1.0432, speed - 60);
}

double lfoDelaySeconds (int d) { return d <= 0 ? 0.0 : 0.00005 * std::pow (double (d), 2.5); }

constexpr double kPmsSemis[8] = { 0, 0.47, 0.94, 1.55, 2.6, 4.3, 7.2, 12 };
constexpr double kAms[8] = { 0, 0.26, 0.43, 1, 1, 1, 1, 1 };

uint32_t nextRand (uint32_t& s) { s = s * 1664525u + 1013904223u; return s; }
} // namespace

void FmxEngine::buildTables()
{
    std::call_once (gTablesOnce, [] { for (int i = 0; i <= kSinSize; ++i) gSin[i] = float (std::sin (2 * kPi * i / kSinSize)); });
}

void FmxEngine::prepareTables (const bk::ysfc::Performance& p)
{
    buildTables();
    for (const auto& part : p.fm)
        for (const auto& o : part.ops) formTable (o.form, o.skirt, o.resonance);
}

float FmxEngine::measureLoudness (const bk::ysfc::Performance& p)
{
    constexpr int sr = 48000, win = 2400, total = 2 * sr;
    auto e = std::make_unique<FmxEngine>();
    e->prepare (sr);
    e->setPerformance (&p);
    for (int k : { 36, 43, 48, 52 }) e->noteOn (k + 12, 100);
    std::vector<float> l (size_t (win), 0.0f), r (size_t (win), 0.0f);
    double best = 0;
    for (int done = 0; done < total; done += win)
    {
        std::fill (l.begin(), l.end(), 0.0f);
        std::fill (r.begin(), r.end(), 0.0f);
        e->render (l.data(), r.data(), win);
        double sum = 0;
        for (int i = 0; i < win; ++i) sum += double (l[size_t (i)]) * l[size_t (i)] + double (r[size_t (i)]) * r[size_t (i)];
        best = std::max (best, sum / (2.0 * win));
    }
    return float (10.0 * std::log10 (best + 1.0e-12));
}

// ── Envolvente (msfa Env) ───────────────────────────────────────────────────────────────────────
void FmxEngine::Eg::start (const double inc4[4], const int l[4], int ol, int hb, double mul)
{
    for (int i = 0; i < 4; ++i) { incs[i] = inc4[i]; levels[i] = l[i]; }
    outlevel = ol;
    holdBlocks = hb;
    srMul = mul;
    level = 0;
    down = true;
    advance (0);
}

void FmxEngine::Eg::advance (int newIx)
{
    ix = newIx;
    if (ix >= 4) return;
    int actual = scaleOutLevel (levels[ix]) >> 1;
    actual = (actual << 6) + outlevel - 4256;
    actual = std::max (actual, 16);
    target = int64_t (actual) << 16;
    rising = target > level;
    inc = std::max<int64_t> (1, int64_t (std::min (incs[ix], 1.0e15) * srMul));
}

void FmxEngine::Eg::keyUp()
{
    if (! down) return;
    down = false;
    holdBlocks = 0;
    advance (3);
}

int64_t FmxEngine::Eg::next()
{
    if (holdBlocks > 0 && ix == 0) { --holdBlocks; return level; }
    if (ix < 3 || (ix < 4 && ! down))
    {
        if (rising)
        {
            const int64_t jump = int64_t (1716) << 16;
            if (level < jump) level = jump;
            level += std::max<int64_t> (1, ((int64_t (17) << 24) - level) >> 24) * inc;
            if (level >= target) { level = target; advance (ix + 1); }
        }
        else
        {
            level -= inc;
            if (level <= target) { level = target; advance (ix + 1); }
        }
    }
    return level;
}

// ── Motor ───────────────────────────────────────────────────────────────────────────────────────
void FmxEngine::prepare (double sampleRate)
{
    buildTables();
    sr_ = sampleRate > 0 ? sampleRate : 48000;
    srMul_ = 44100.0 / sr_;
    allNotesOff (true);
}

void FmxEngine::setPerformance (const bk::ysfc::Performance* perf)
{
    for (auto& v : voices_) v.active = false;
    partCount_ = 0;
    if (perf == nullptr) return;
    for (const auto& fp : perf->fm)
    {
        if (partCount_ >= kMaxParts) break;
        auto& part = parts_[size_t (partCount_++)];
        part.p = fp;
        for (int s = 0; s < 8; ++s)
        {
            const auto& o = fp.ops[size_t (s)];
            part.table[s] = formTable (o.form, o.skirt, o.resonance);
        }
    }
}

void FmxEngine::startVoice (Voice& v, int partIndex, int key, int velocity)
{
    const auto& part = parts_[size_t (partIndex)];
    const auto& p = part.p;
    const auto& alg = kAlgorithms[std::clamp (p.algorithm, 0, 87)];
    v.active = true;
    v.released = v.sustained = false;
    v.key = key;
    v.velocity = velocity;
    v.part = partIndex;
    v.noteOffset = p.noteShift;
    v.age = ++counter_;
    const double pan = std::clamp ((p.pan - 64) / 63.0, -1.0, 1.0);
    v.panL = float (std::cos ((pan + 1) * kPi / 4));
    v.panR = float (std::sin ((pan + 1) * kPi / 4));
    const double vol = std::clamp (p.volume, 0, 127) / 100.0;
    v.level = float (vol * vol);
    v.lfoTime = 0;
    v.lfoPhase = p.lfoSync ? 0u : globalLfo_;
    v.lfoHold = 0;
    const int note = std::clamp (key + p.noteShift, 0, 127);
    for (int s = 0; s < 8; ++s)
    {
        const auto& o = p.ops[size_t (s)];
        auto& op = v.ops[size_t (s)];
        const bool carrier = (alg.carriers >> s) & 1;
        int level = o.level;
        if (! carrier) level = std::clamp (level + int (std::lround (tweaks_.brightness * 24)), 0, 99);
        int outlevel = scaleOutLevel (dxLevel (level)) + scaleLevel (note, o.breakPoint, o.depthLo, o.depthHi, o.curveLo, o.curveHi);
        outlevel = std::clamp (outlevel, 0, 127) << 5;
        outlevel += scaleVelocity (velocity, std::clamp (o.levelVelocity - 7, -7, 7));
        outlevel = std::max (outlevel, 0);
        double incs[4];
        const double keyScale = rateScaleFactor (note, o.timeKeyFollow);
        for (int i = 0; i < 4; ++i)
        {
            int t = o.times[size_t (i)];
            if (i == 0) t = std::clamp (t + int (std::lround (tweaks_.attack * 30)), 0, 99);
            if (i == 3) t = std::clamp (t + int (std::lround (tweaks_.release * 30)), 0, 99);
            incs[i] = incFromTime (t, dxLevel (o.level)) * keyScale;
        }
        const int holdBlocks = int (holdSeconds (o.hold) * sr_ / kBlock);
        op.eg.start (incs, o.levels.data(), outlevel, holdBlocks, srMul_);
        if (o.keyOnReset) op.phase = 0;
        op.gain = 0;
        op.gainStep = 0;
        op.y1 = op.y2 = 0;
        op.pegTime = 0;
    }
    controlUpdate (v);
}

void FmxEngine::controlUpdate (Voice& v)
{
    const auto& part = parts_[size_t (v.part)];
    const auto& p = part.p;
    const double dt = kBlock / sr_;

    // LFO
    const double hz = lfoHz (p.lfoSpeed);
    const auto ph = v.lfoPhase;
    double lfo;  // −1..1
    const double x = ph / 4294967296.0;
    switch (p.lfoWave)
    {
        case 0: lfo = x < 0.5 ? 4 * x - 1 : 3 - 4 * x; break;
        case 1: lfo = 1 - 2 * x; break;
        case 2: lfo = 2 * x - 1; break;
        case 3: lfo = x < 0.5 ? 1 : -1; break;
        case 5: lfo = v.lfoHold; break;
        default: lfo = std::sin (2 * kPi * x); break;
    }
    const uint32_t step = uint32_t (std::min (hz * dt, 0.49) * 4294967296.0);
    if (p.lfoWave == 5 && uint64_t (ph) + step > 0xFFFFFFFFull) v.lfoHold = (nextRand (rng_) >> 8) / 8388608.0 - 1;
    v.lfoPhase = ph + step;
    v.lfoTime += dt;
    const double delay = lfoDelaySeconds (p.lfoDelay);
    const double fade = std::clamp ((v.lfoTime - delay) / (delay * 0.5 + 0.01), 0.0, 1.0);
    const double pmd = std::min (1.0, p.lfoPmd / 99.0 + modWheel_ * 0.5) * fade;
    const double amd = p.lfoAmd / 99.0 * fade;

    const double bend = bend_ >= 0 ? bend_ * p.bendUp : -bend_ * p.bendDown;
    const double base = v.key + v.noteOffset + bend + tuneCents_ / 100.0;
    const auto& alg = kAlgorithms[std::clamp (p.algorithm, 0, 87)];
    for (int s = 0; s < 8; ++s)
    {
        const auto& o = p.ops[size_t (s)];
        auto& op = v.ops[size_t (s)];
        // EG de tono del operador.
        double peg = 0;
        if (o.pegInit != 50 || o.pegAttack != 50)
        {
            const double ta = pegSeconds (o.pegAttackTime), td = pegSeconds (o.pegDecayTime);
            const double l0 = pegSemitones (o.pegInit), l1 = pegSemitones (o.pegAttack);
            const double t = op.pegTime;
            if (t < ta) peg = l0 + (l1 - l0) * t / ta;
            else if (t < ta + td) peg = l1 * (1 - (t - ta) / td);
            op.pegTime += dt;
        }
        const double vib = lfo * pmd * kPmsSemis[std::clamp (o.pitchModSens, 0, 7)];
        double hzOp;
        if (o.fixed)
        {
            hzOp = 0.006719 * std::pow (2.0, o.coarse + o.fine / 128.0);
            hzOp *= std::pow (2.0, (o.pitchKeyFollow / 99.0) * (v.key - 60) / 12.0 + (vib + peg) / 12.0);
        }
        else
        {
            int coarse = o.coarse, fine = o.fine;
            if (fine >= 100) { coarse = std::min (coarse + 1, 31); fine = 0; }
            const double ratio = (coarse == 0 ? 0.5 : double (coarse)) * (1 + fine / 100.0);
            const double detCents = (o.detune - 15) * std::clamp (1.2 * std::pow (2.0, -(v.key - 48) / 24.0), 0.3, 2.5);
            hzOp = 440.0 * std::pow (2.0, (base + vib + peg - 69) / 12.0 + detCents / 1200.0) * ratio;
        }
        hzOp = std::clamp (hzOp, 0.0, sr_ * 0.45);
        op.inc = uint32_t (hzOp / sr_ * 4294967296.0);

        const int64_t lvl = op.eg.next();
        double g = std::exp2 (double (lvl) / double (1 << 24) - 14.0);
        if (amd > 0 && o.ampModSens > 0)
        {
            const double am = amd * kAms[std::clamp (o.ampModSens, 0, 7)] * (lfo * 0.5 + 0.5);
            g *= std::pow (10.0, -30.0 * am / 20.0);
        }
        if (! ((alg.carriers >> s) & 1) && lvl <= (int64_t (16) << 16)) g = 0;
        op.gainStep = float ((g - op.gain) / kBlock);
    }
}

bool FmxEngine::voiceSilent (const Voice& v) const
{
    const auto& alg = kAlgorithms[std::clamp (parts_[size_t (v.part)].p.algorithm, 0, 87)];
    for (int s = 0; s < 8; ++s)
    {
        if (! ((alg.carriers >> s) & 1)) continue;
        const auto& op = v.ops[size_t (s)];
        if (op.gain > 2.0e-5f || op.gainStep > 0) return false;
        const auto& eg = op.eg;
        if (eg.rising) return false;
        if (! v.released && eg.ix < 2) return false;
    }
    return true;
}

void FmxEngine::noteOn (int key, int velocity)
{
    if (key < 0 || key > 127 || partCount_ == 0) return;
    if (velocity <= 0) { noteOff (key); return; }
    keyDown_[size_t (key)] = true;
    bool anyKeyboard = false;
    for (int i = 0; i < partCount_; ++i) anyKeyboard = anyKeyboard || (parts_[size_t (i)].p.keyboard && parts_[size_t (i)].p.partOn);
    for (int i = 0; i < partCount_; ++i)
    {
        const auto& p = parts_[size_t (i)].p;
        if (! p.partOn) continue;
        if (anyKeyboard ? ! p.keyboard : i != 0) continue;
        const bool inRange = p.noteLo <= p.noteHi ? (key >= p.noteLo && key <= p.noteHi) : (key >= p.noteLo || key <= p.noteHi);
        const bool inVel = p.velLo <= p.velHi ? (velocity >= p.velLo && velocity <= p.velHi) : (velocity >= p.velLo || velocity <= p.velHi);
        if (! inRange || ! inVel) continue;
        // La misma tecla en la misma parte: se reutiliza la voz.
        Voice* slot = nullptr;
        for (auto& v : voices_) if (v.active && v.key == key && v.part == i) { slot = &v; break; }
        if (slot == nullptr) for (auto& v : voices_) if (! v.active) { slot = &v; break; }
        if (slot == nullptr)
        {
            Voice* oldest = nullptr;
            for (auto& v : voices_) if (v.released && (oldest == nullptr || v.age < oldest->age)) oldest = &v;
            if (oldest == nullptr) for (auto& v : voices_) if (oldest == nullptr || v.age < oldest->age) oldest = &v;
            slot = oldest;
        }
        startVoice (*slot, i, key, velocity);
    }
}

void FmxEngine::noteOff (int key)
{
    if (key < 0 || key > 127) return;
    keyDown_[size_t (key)] = false;
    for (auto& v : voices_)
    {
        if (! v.active || v.key != key || v.released) continue;
        if (sustain_) { v.sustained = true; continue; }
        v.released = true;
        for (auto& op : v.ops) op.eg.keyUp();
    }
}

void FmxEngine::setSustain (bool down)
{
    sustain_ = down;
    if (down) return;
    for (auto& v : voices_)
        if (v.active && v.sustained && ! keyDown_[size_t (v.key)])
        {
            v.sustained = false;
            v.released = true;
            for (auto& op : v.ops) op.eg.keyUp();
        }
}

void FmxEngine::allNotesOff (bool immediate)
{
    sustain_ = false;
    keyDown_.fill (false);
    for (auto& v : voices_)
    {
        if (immediate) { v.active = false; continue; }
        if (! v.active) continue;
        v.released = true;
        v.sustained = false;
        for (auto& op : v.ops) op.eg.keyUp();
    }
}

int FmxEngine::activeVoices() const
{
    int n = 0;
    for (const auto& v : voices_) n += v.active;
    return n;
}

void FmxEngine::render (float* left, float* right, int frames)
{
    int done = 0;
    while (done < frames)
    {
        if (blockPos_ == 0)
        {
            globalLfo_ += uint32_t (std::min (lfoHz (35) * kBlock / sr_, 0.49) * 4294967296.0);
            for (auto& v : voices_)
            {
                if (! v.active) continue;
                if (voiceSilent (v)) { v.active = false; continue; }
                controlUpdate (v);
            }
        }
        const int n = std::min (frames - done, kBlock - blockPos_);
        for (auto& v : voices_)
        {
            if (! v.active) continue;
            const auto& part = parts_[size_t (v.part)];
            const auto& alg = kAlgorithms[std::clamp (part.p.algorithm, 0, 87)];
            const float fbScale = part.p.feedback > 0 ? 0.5f * std::ldexp (1.0f, part.p.feedback - 8) : 0.0f;
            const float gL = v.panL * v.level * kOutLevel * gain_, gR = v.panR * v.level * kOutLevel * gain_;
            float* L = left + done;
            float* R = right + done;
            for (int i = 0; i < n; ++i)
            {
                float out[8];
                float sum = 0;
                for (int s = 0; s < 8; ++s)
                {
                    auto& op = v.ops[size_t (s)];
                    float mod = 0;
                    for (uint8_t m = alg.mods[s], j = 0; m != 0; m >>= 1, ++j)
                        if (m & 1) mod += out[j];
                    if (s == alg.fbTo && fbScale > 0)
                    {
                        const auto& src = v.ops[size_t (alg.fbFrom)];
                        mod += (src.y1 + src.y2) * fbScale;
                    }
                    const uint32_t ph = op.phase + uint32_t (int64_t (mod * 4294967296.0f));
                    const float* t = part.table[s];
                    const float y = (t != nullptr ? lookup (t, kFormBits, ph) : lookup (gSin, kSinBits, ph)) * op.gain;
                    op.phase += op.inc;
                    op.gain = std::max (0.0f, op.gain + op.gainStep);
                    op.y2 = op.y1;
                    op.y1 = y;
                    out[s] = y;
                    if ((alg.carriers >> s) & 1) sum += y;
                }
                L[i] += sum * gL;
                R[i] += sum * gR;
            }
        }
        blockPos_ = (blockPos_ + n) % kBlock;
        done += n;
    }
}
} // namespace bk::fmx
