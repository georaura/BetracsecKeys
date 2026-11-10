// BeTracks Keys — motor FM-X (sección FM): reproduce las partes FM-X de las performances de MONTAGE/MODX.
//
// Ocho operadores, las 88 conexiones del FS1R (FmxAlgorithms.h) y realimentación. La envolvente, el
// escalado de nivel por tecla, la velocidad y la escala de tiempos por tecla siguen al DX7 tal como lo
// documenta msfa / Dexed (Apache-2.0): las voces del DX7 que Yamaha importó a FM-X guardan los mismos
// números. Los tiempos de FM-X son duraciones (T = 43,1 + 6,08·log2 s), ajustadas comparando 6 voces del
// DX7 con su versión importada en la DX7 Selection. Las formas espectrales (All, Odd, Res) siguen lo que FSVR midió
// en el FS1R (de donde viene FM-X); las de FM-X no están publicadas. No es una emulación del MONTAGE: sin efectos, escenas,
// arpegios ni controles asignables.
#pragma once

#include "Ysfc.h"

#include <array>
#include <cstdint>

namespace bk::fmx
{
/** Retoques de Keys sobre la performance, −1..1 (0 = como viene). */
struct Tweaks
{
    float brightness = 0;   ///< nivel de los moduladores (±18 dB)
    float attack = 0;       ///< tiempo de ataque
    float release = 0;      ///< tiempo de release
};

class FmxEngine
{
public:
    static constexpr int kMaxVoices = 32;
    static constexpr int kMaxParts = 8;

    void prepare (double sampleRate);
    /** Copia las partes FM-X (o ninguna con nullptr). Se llama con el audio bloqueado. */
    void setPerformance (const bk::ysfc::Performance* p);
    void setTweaks (const Tweaks& t) { tweaks_ = t; }
    void setTuneCents (float c) { tuneCents_ = c; }
    /** Ganancia de salida (lineal), p. ej. para igualar el volumen entre performances. */
    void setGain (float g) { gain_ = g; }

    void noteOn (int key, int velocity);
    void noteOff (int key);
    void setSustain (bool down);
    void setPitchBend (float v) { bend_ = v; }      ///< −1..1
    void setModWheel (float v) { modWheel_ = v; }   ///< 0..1
    void allNotesOff (bool immediate);
    /** Suma a L y R. */
    void render (float* left, float* right, int frames);
    int activeVoices() const;
    int partCount() const { return partCount_; }

    /** Tabla del seno; la crea prepare() la primera vez. */
    static void buildTables();
    /** Crea las tablas de las formas espectrales que usa `p` (fuera del hilo de audio, antes de setPerformance). */
    static void prepareTables (const bk::ysfc::Performance& p);
    /**
     * Sonoridad de una performance: el máximo del RMS en ventanas de 50 ms de un acorde (Do2 Sol2 Do3 Mi3,
     * velocidad 100) durante 2 s, en dBFS. Las performances de MONTAGE se equilibran con sus efectos, que
     * Keys no tiene, así que su volumen guardado deja diferencias de más de 15 dB. Llama a prepareTables antes.
     */
    static float measureLoudness (const bk::ysfc::Performance& p);

private:
    struct Eg
    {
        int64_t level = 0, target = 0, inc = 0;
        int ix = 0;
        bool rising = false, down = true;
        double incs[4] {};
        int levels[4] {};
        int outlevel = 0;
        int holdBlocks = 0;
        double srMul = 1;
        void start (const double incs[4], const int l[4], int outlevel, int holdBlocks, double srMul);
        void keyUp();
        int64_t next();
        void advance (int newIx);
    };
    struct Op
    {
        uint32_t phase = 0, inc = 0;
        float gain = 0, gainStep = 0;
        float y1 = 0, y2 = 0;
        Eg eg;
        double pegTime = 0;
    };
    struct Voice
    {
        bool active = false, released = false, sustained = false;
        int key = 0, velocity = 100, part = 0;
        double noteOffset = 0;      ///< semitonos de la parte (note shift)
        std::array<Op, 8> ops {};
        uint32_t lfoPhase = 0;
        double lfoTime = 0, lfoHold = 0;
        uint64_t age = 0;
        float panL = 0.7f, panR = 0.7f, level = 1;
    };
    struct Part
    {
        bk::ysfc::FmPart p;
        const float* table[8] {};   ///< forma espectral de cada operador (nullptr = seno)
    };

    void startVoice (Voice& v, int part, int key, int velocity);
    void controlUpdate (Voice& v);
    bool voiceSilent (const Voice& v) const;

    std::array<Part, kMaxParts> parts_ {};
    int partCount_ = 0;
    std::array<Voice, kMaxVoices> voices_ {};
    std::array<bool, 128> keyDown_ {};
    Tweaks tweaks_;
    double sr_ = 48000, srMul_ = 44100.0 / 48000.0;
    float tuneCents_ = 0, bend_ = 0, modWheel_ = 0, gain_ = 1;
    bool sustain_ = false;
    int blockPos_ = 0;
    uint32_t globalLfo_ = 0;
    uint64_t counter_ = 0;
    uint32_t rng_ = 22222;
};
} // namespace bk::fmx
