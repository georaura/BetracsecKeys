// BeTracks Keys — reproductor de Nord Samples (sección SAMPLES).
//
// Diseño propio sobre lo que el archivo describe (OpenNord): cada tecla toca la zona que la cubre en
// su capa de velocidad; el golpe suena a su propia frecuencia (≈ 35.002 Hz con la afinación fina de la
// zona) transportado `tecla − raíz` semitonos; si el golpe tiene bucle, lo recorre y cae a su ritmo
// (dB/s); las zonas "one-shot" suenan enteras aunque se suelte la tecla. Encima, una envolvente sencilla
// (ataque y liberación) y la sensibilidad a la velocidad. Sin JUCE.
#pragma once

#include "NordSample.h"

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace bk::nsmp
{
/** Un sample listo para el hilo de audio: PCM de 16 bits y zonas que apuntan a su golpe. */
struct SampleSound
{
    struct Stroke
    {
        std::vector<int16_t> pcm;          // canales intercalados; los de fábrica tienen pico 8192
        uint32_t frames = 0;
        int channels = 1;
        double sampleRate = 35002;
        bool loops = false;
        uint32_t loopStart = 0, loopEnd = 0;
        double loopDecayDbPerSecond = 0;
        /** Caída antes del bucle (dB/s). Solo la usa la exportación a BeTracks Pro: tomas generadas por Keys a las que
         *  se les quitó la caída para poder hacer bucle (BeTracks Pro la vuelve a poner). El reproductor de Keys la ignora. */
        double preLoopDecayDbPerSecond = 0;
    };
    struct Zone
    {
        int root, low, high, velLow, velHigh, stroke;
        bool oneShot;
        /** Velocidad con la que se grabó la capa (librerías muestreadas con varias capas), −1 si no se sabe.
            Con ella, la sensibilidad a la velocidad solo ajusta dentro de la capa: la dinámica ya está grabada. */
        int velRoot = -1;
    };

    std::string path, name;
    int codec = 4;                         ///< 3 o 4: Nord Sample; 0: librería propia de Keys (.bkm)
    /** Los golpes sin bucle se apagan al soltar la tecla (librerías muestreadas); si no, suenan enteros (Nord). */
    bool releaseUnlooped = false;
    std::vector<Stroke> strokes;
    std::vector<Zone> zones;
    size_t memoryBytes() const;
    bool stereo() const;
};

/** Lee y decodifica un .nsmp3/.nsmp4. nullptr con `error` si no se puede. */
std::unique_ptr<SampleSound> loadSample (const std::string& path, std::string& error);

struct SampleParams
{
    float attackSec = 0.002f;
    float releaseSec = 0.25f;
    float velocity = 0.6f;     ///< 0 = todas las notas igual, 1 = rango completo (unos 36 dB)
    int octaveShift = 0;       ///< −2..+2
    float tuneCents = 0;
};

class SampleEngine
{
public:
    static constexpr int kMaxVoices = 64;
    void prepare (double sampleRate);
    void setSound (const SampleSound* s);
    const SampleSound* sound() const { return sound_; }
    void setParams (const SampleParams& p) { params_ = p; }

    void noteOn (int key, int velocity);
    void noteOff (int key);
    void setSustain (bool down);
    void allNotesOff (bool immediate);
    /** Suma a L y R. */
    void render (float* left, float* right, int frames);
    int activeVoices() const;

private:
    enum class Stage : uint8_t { Off, Attack, Hold, Release, Kill };
    struct Voice
    {
        Stage stage = Stage::Off;
        const SampleSound::Stroke* stroke = nullptr;
        int key = 0;
        bool oneShot = false, sustained = false;
        double pos = 0, inc = 1;
        float gain = 1, env = 0, attackStep = 1, releaseMul = 0.99f;
        double loopMul = 1, loopLevel = 1;
        uint64_t age = 0;
    };
    void release (Voice& v);
    Voice& allocate();

    const SampleSound* sound_ = nullptr;
    SampleParams params_;
    double sr_ = 48000;
    bool sustain_ = false;
    std::array<bool, 128> keyDown_ {};
    std::array<Voice, kMaxVoices> voices_ {};
    uint64_t counter_ = 0;
};
} // namespace bk::nsmp
