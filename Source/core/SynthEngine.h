// BeTracks Keys — synth (sección SYNTH).
//
// Motor propio, guiado por lo que OpenNord documenta del synth del Stage 4 (synth-scaling.ts: curvas de
// corte, resonancia, envolventes, LFO y profundidad de modulación; synth-preview.ts: estructura de la voz).
// No es una emulación: las formas de onda de las categorías Sub, Sync, Shape, Multi, Super y Misc, el FM y
// el vibrato son diseños propios a partir de sus nombres; las ondas WAVE del Stage 4 son tablas del firmware
// y no se incluyen (se usa una aproximación). Los parámetros llegan en valores crudos del panel (0..127),
// como en los presets .ns4y.
#pragma once

#include "SampleEngine.h"
#include "SynthPreset.h"

#include <array>
#include <cstdint>

namespace bk::synth
{
// ── Escalas del Stage 4 (synth-scaling.ts de OpenNord) ─────────────────────────────────────────
double cutoffSemitones (double raw);              ///< sobre 277,18 Hz
double cutoffHz (double raw);
double resonanceDamping (double raw);             ///< k = 2·(1 − r/128)²; 2 = sin resonancia
double attackSeconds (double raw);                ///< subida lineal
double decaySeconds (double raw, bool isDecay);   ///< tiempo en caer 40 dB; decay 127 = ∞
double lfoRateHz (double raw);
double depthCurveSemitones (double raw);
double fmHarmonicRatio (int wave);
double fmInharmonicRatio (int wave);
/** Limitador suave de la salida del synth: transparente hasta ±0,85, rodilla tanh hacia ±1. Solo es una red
 * de seguridad: el nivel por voz deja margen para que los acordes no lleguen a él (si no, intermodulación). */
float softLimit (float x);

enum class Source : uint8_t { Analog, FmH, FmI, Wave, Sample };
enum class FilterType : uint8_t { LP12, LP24, LPM, LPHP, BP, HP };

/** Todo en valores crudos del panel salvo donde se indica. */
struct SynthParams
{
    Source source = Source::Analog;
    int category = 0, wave = 2;            ///< analógico: categoría 0..7 y onda; FM/WAVE: onda
    float oscCtrl = 0;                     ///< 0..127
    int coarse = 0;                        ///< semitonos −24..24
    int fineCents = 0;
    int octave = 0;                        ///< −2..2
    float oscEnvA = 0, oscEnvD = 60, oscEnvR = 0, oscEnvAmt = 0; ///< amt −63..63
    bool oscEnvToPitch = false, oscEnvVelocity = false;
    bool filterOn = true;
    FilterType filterType = FilterType::LP24;
    float cutoff = 100, reso = 0;          ///< 0..127 (en LP+HP, `reso` es el corte del paso alto)
    int track = 2, drive = 0;              ///< 0..3
    float filterEnvAmt = 0;                ///< 0..127 (0..10 octavas)
    float fEnvA = 0, fEnvD = 60, fEnvR = 40;
    bool filterVelocity = false;
    float ampA = 0, ampD = 127, ampR = 40;
    int ampVelocity = 1;                   ///< 0..3
    int lfoTarget = 0;                     ///< 0 nada, 1 osc ctrl, 2 tono, 3 filtro
    int lfoShape = 0;                      ///< 0 triángulo, 1 sierra, 2 sierra ↓, 3 cuadrada, 4 S/H
    float lfoRate = 40, lfoAmt = 0;
    int vibMode = 0;                       ///< 0 no, 1 sí, 2 con retardo, 3 rueda, 4 aftertouch
    float vibRate = 60, vibAmt = 10;       ///< rate 0..127, amount 0..50
    bool mono = false, legato = false;
    float glide = 0;
    int unison = 0;                        ///< 0..3
    float tuneCents = 0;                   ///< afinación general de Keys
};

/** Parámetros de motor para una capa de un preset .ns4y (valores crudos del panel). */
SynthParams paramsFromLayer (const SynthLayer& l);

class SynthEngine
{
public:
    static constexpr int kMaxVoices = 24;
    void prepare (double sampleRate);
    void setParams (const SynthParams& p) { params_ = p; }
    const SynthParams& params() const { return params_; }
    /** Sample para la fuente "Sample" (lo posee el procesador). */
    void setSample (const bk::nsmp::SampleSound* s);

    void noteOn (int key, int velocity);
    void noteOff (int key);
    void setSustain (bool down);
    void setModWheel (float v) { modWheel_ = v; }
    void setAftertouch (float v) { aftertouch_ = v; }
    void allNotesOff (bool immediate);
    /** Suma a L y R. */
    void render (float* left, float* right, int frames);
    int activeVoices() const;

private:
    struct Env
    {
        enum Stage : uint8_t { Off, Attack, Decay, Release } stage = Off;
        double level = 0, attackStep = 0, decayMul = 1, releaseMul = 1;
        void start (double attackSec, double decaySec, double sr);
        void release (double releaseSec, double sr);
        double next();
    };
    struct Filter
    {
        double s1[2] {}, s2[2] {}, t1[2] {}, t2[2] {}, lad[2][4] {};
        void reset() { *this = Filter {}; }
    };
    struct Voice
    {
        bool active = false, released = false, sustained = false;
        int key = 0, velocity = 100;
        double pitch = 60, targetPitch = 60;   ///< semitonos MIDI (con glide)
        std::array<double, 8> phase {}, subPhase {};
        double fmPhase = 0, noise = 0, pink[3] {}, red = 0, shPhase = 0;
        double samplePos = 0;
        const bk::nsmp::SampleSound::Stroke* stroke = nullptr;
        int sampleRoot = 60;
        Env amp, fenv, oenv;
        Filter filter;
        double vibTime = 0;
        uint32_t rng = 1;
        uint64_t age = 0;
    };

    Voice& allocate();
    void startVoice (Voice& v, int key, int velocity, bool retrigger);
    void releaseVoice (Voice& v);
    double oscillator (Voice& v, int unisonIndex, double freq, double oscCtrl, double& right);
    void filterSample (Voice& v, double cutoffHz, double& l, double& r);

    SynthParams params_;
    const bk::nsmp::SampleSound* sample_ = nullptr;
    double sr_ = 48000;
    bool sustain_ = false;
    float modWheel_ = 0, aftertouch_ = 0;
    std::array<bool, 128> keyDown_ {};
    std::array<Voice, kMaxVoices> voices_ {};
    std::array<int, 128> monoStack_ {};
    int monoCount_ = 0;
    double lfoPhase_ = 0, lfoHold_ = 0;
    uint32_t lfoRng_ = 12345;
    uint64_t counter_ = 0;
};
} // namespace bk::synth
