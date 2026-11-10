// BeTracks Keys — motor AWM2 (partes de muestras de las performances de MONTAGE/MODX).
//
// Toca los elementos AWM2 cuyas ondas vienen en la propia librería (PCM de 16 bits). Las ondas de la ROM del
// instrumento no están en el archivo y no suenan. Cada elemento: keybanks por nota y velocidad (los estéreo
// son dos keybanks mono, izquierda y derecha), bucle, envolvente de amplitud, filtro (los tipos de la serie
// Motif/MONTAGE aproximados con filtros de estado variable) con su envolvente, LFO y seguimiento de teclado.
// Las escalas de tiempo (0..127 → segundos) y de corte (0..255 → Hz) no están publicadas: son aproximadas.
// Sin efectos, escenas ni partes de batería.
#pragma once

#include "Ysfc.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <vector>

namespace bk::awm
{
/** Muestras cargadas de las ondas que usa una performance. */
struct SampleSet
{
    struct Wave
    {
        std::vector<bk::ysfc::Keybank> keybanks;
        std::vector<std::vector<int16_t>> data;   ///< una por keybank
    };
    std::map<int, Wave> waves;                    ///< por número de onda
    size_t bytes = 0;
};

/** Lee del archivo las muestras de las ondas de la librería que usa `perf`. nullptr si no hay ninguna. */
std::unique_ptr<SampleSet> loadSamples (const std::string& path, const bk::ysfc::Performance& perf,
                                        const std::vector<bk::ysfc::Waveform>& waves, const std::atomic<bool>* cancel = nullptr);

class AwmEngine
{
public:
    static constexpr int kMaxVoices = 64;
    static constexpr int kMaxParts = 8;

    void prepare (double sampleRate);
    /** Partes AWM2 de la performance con sus muestras (se llama con el audio bloqueado; `samples` debe vivir). */
    void setPerformance (const bk::ysfc::Performance* p, const SampleSet* samples);
    void setGain (float g) { gain_ = g; }
    void setTuneCents (float c) { tuneCents_ = c; }

    void noteOn (int key, int velocity);
    void noteOff (int key);
    void setSustain (bool down);
    void setPitchBend (float v) { bend_ = v; }
    void allNotesOff (bool immediate);
    void render (float* left, float* right, int frames);
    int activeVoices() const;

private:
    struct Zone
    {
        const int16_t* data = nullptr;
        uint32_t length = 0, loopStart = 0, loopEnd = 0;
        bool loop = false;
        int keyLo = 0, keyHi = 127, velLo = 1, velHi = 127;
        double rootSemis = 60;    ///< nota a la que la muestra suena a su frecuencia, con la afinación del keybank
        double rate = 44100;
        float gain = 1, panL = 0.7f, panR = 0.7f;
    };
    struct Element
    {
        bk::ysfc::AwmElement e;
        std::vector<Zone> zones;
    };
    struct Part
    {
        bk::ysfc::PartInfo info;
        std::vector<Element> elements;
    };
    struct Env
    {
        enum Stage : uint8_t { Off, Hold, Seg, Sustain, Release } stage = Off;
        int seg = 0;
        double value = 0, from = 0, to = 0, t = 0, dur = 0;
    };
    struct Voice
    {
        bool active = false, released = false, sustained = false;
        int key = 0, velocity = 100, part = 0, element = 0;
        const Zone* zone = nullptr;
        double pos = 0, step = 1;
        Env amp, feg;
        double ampTimeScale = 1;
        float gainL = 0, gainR = 0;
        double s1[2] {}, s2[2] {}, h1 = 0, h2 = 0, lp1 = 0;   // filtros
        double lfoPhase = 0, lfoTime = 0;
        uint64_t age = 0;
    };

    void startVoice (Voice& v, int part, int element, const Zone& z, int key, int velocity);
    void releaseVoice (Voice& v);
    void control (Voice& v, double& ampGain, double& cutoffHz, double& hpfHz, double& pitchSemis);
    static double stepEnv (Env& env, const bk::ysfc::AwmElement& e, bool amp, double dt, double timeScale, bool released);

    std::vector<Part> parts_;
    std::array<Voice, kMaxVoices> voices_ {};
    std::array<bool, 128> keyDown_ {};
    double sr_ = 48000;
    float gain_ = 1, tuneCents_ = 0, bend_ = 0;
    bool sustain_ = false;
    uint64_t counter_ = 0;
};

/**
 * Sonoridad de una performance completa (FM-X + AWM2 con sus muestras): el máximo del RMS en ventanas de
 * 50 ms de un acorde durante 2 s, en dBFS. Para igualar el volumen entre performances.
 */
float measureLoudness (const bk::ysfc::Performance& p, const SampleSet* samples);
} // namespace bk::awm
