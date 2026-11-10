// BeTracks Keys — motor polifónico del piano. Sin JUCE: lo usan el plugin y las pruebas.
//
// Comportamiento (de las notas de OpenNord sobre el Stage 4):
//  - cada tecla toca la grabación de su zona, elegida por la fuerza, transportada a la nota
//    (100·(tecla − raíz − 4) cents), sin mezcla entre capas;
//  - nivel: la atenuación de la fuerza a 0,2 dB por paso; cada golpe está normalizado a su pico;
//  - antes del bucle, el decaimiento extra por semitono bajo la raíz (+0x36); dentro, el del golpe;
//  - al soltar: el apagador de la tecla (T60 ≈ 1,5 ms · 10^(i/20)); las teclas desde la primera
//    sin apagador (+0x40D) siguen sonando, como las cuerdas agudas de un piano de cola;
//  - pedal de sustain (CC 64).
#pragma once

#include "PianoSound.h"

#include <array>
#include <cstdint>

namespace bk
{
struct EngineParams
{
    float tuneCents = 0.0f;       ///< afinación global
    int transpose = 0;            ///< semitonos
    int touchOffset = 0;          ///< pasos de 0,2 dB sumados antes de elegir capa (+ = toque más suave)
    float dynamics = 1.0f;        ///< 0 = todas las notas igual de fuertes, 1 = rango completo (40 dB)
    float releaseScale = 1.0f;    ///< multiplica el tiempo de apagador
    bool softRelease = false;     ///< usa el apagador lento del golpe (+0x73)
    bool stringRes = true;        ///< con el pedal pisado, las grabaciones con pedal (si están cargadas)
    float releaseNoise = 1.0f;    ///< ganancia del ruido de soltar tecla (1 = nivel del archivo, 0 = sin ruido)
};

class PianoEngine
{
public:
    static constexpr int kMaxVoices = 96;

    void prepare (double sampleRate);
    /** Cambia de piano (hilo de audio). Las notas que suenan se cortan. nullptr = silencio. */
    void setSound (const PianoSound* sound);
    const PianoSound* sound() const { return sound_; }
    void setParams (const EngineParams& p) { params_ = p; }

    void noteOn (int key, int velocity);
    void noteOff (int key);
    void setSustain (bool down);
    void allNotesOff (bool immediate);

    /** Suma la salida a L y R (no borra). */
    void render (float* left, float* right, int numFrames);

    int activeVoices() const;
    /** Teclas que suenan ahora (para el teclado en pantalla), bit por tecla. */
    std::array<uint64_t, 2> soundingKeys() const;

private:
    enum class Stage : uint8_t { Off, Held, Released, Killed };
    struct Voice
    {
        Stage stage = Stage::Off;
        const StrokeAudio* stroke = nullptr;
        int key = 0;
        bool sustained = false;   ///< tecla suelta con el pedal pisado
        bool undamped = false;
        bool noise = false;       ///< ruido de soltar tecla (no es una nota)
        int zone = 0, velocity = 0;
        double pos = 0, inc = 1;
        double level = 1, gain = 1;
        double preLoopMul = 1, loopMul = 1, releaseMul = 1;
        float meter = 0, meterMax = 0; ///< volumen que suena ahora y el máximo (para el ruido de soltar tecla)
        uint64_t age = 0;
    };

    void release (Voice& v);
    void kill (Voice& v);
    void startReleaseNoise (const Voice& note);
    /** Una voz libre (o la que menos se nota al robarla), nunca `except`. */
    Voice& allocate (const Voice* except = nullptr);

    const PianoSound* sound_ = nullptr;
    EngineParams params_;
    double sampleRate_ = 48000;
    double envStep_ = 22050.0 / 48000.0; ///< actualizaciones de envolvente por muestra
    double killMul_ = 0.99;
    float meterFall_ = 0.999f;  ///< caída del medidor por muestra (~50 ms)
    bool sustain_ = false;
    std::array<bool, 128> keyDown_ {};
    std::array<Voice, kMaxVoices> voices_ {};
    uint64_t counter_ = 0;
};
} // namespace bk
