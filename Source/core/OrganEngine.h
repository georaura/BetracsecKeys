// BeTracks Keys — motor de órgano.
//
// Port a C++ del núcleo de órgano de OpenNord (src/lib/engine/organ: core.ts, voicing.ts, scanner.ts,
// tonewheel.ts; AGPL-3.0), preparado para el hilo de audio: todas las tablas y generadores se crean en
// prepare(), y render() no reserva memoria.
//
// Un banco fijo de generadores siempre girando (las 91 ruedas del B3; divisores o filas de tubos en
// los otros modelos) alimenta un bus; cada tecla cierra un contacto por drawbar. Percusión de disparo
// único, click de tecla, chiff de tubo, vibrato/chorus del scanner, nivelador de polifonía y soft clip.
// Solo el B3 sigue de cerca al instrumento real; Vox, Farfisa y Pipe son aproximaciones (OpenNord).
// La salida es mono: el rotary (Rotary.h) va detrás.
#pragma once

#include "OrganPreset.h"

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <vector>

namespace bk::organ
{
class OrganEngine
{
public:
    OrganEngine();
    ~OrganEngine();

    /** Crea las tablas de onda y los bancos de generadores (fuera del hilo de audio). */
    void prepare (double sampleRate);
    /** Ajustes (hilo de audio, entre bloques). Cambiar de modelo hace un fundido de 20 ms. */
    void setParams (const OrganParams& p);
    const OrganParams& params() const { return params_; }

    void noteOn (int note);
    void noteOff (int note);
    void allNotesOff();
    /** Escribe `frames` muestras mono en `out` (sobrescribe). */
    void render (float* out, int frames);

    /** Envolvente de percusión actual (0..1), para pruebas. */
    float percussionEnvelope() const;
    int activeGenerators() const { return activeCount_; }
    bool isSilent() const { return activeCount_ == 0 && clickEnv_ < 1.0e-5 && chiffEnv_ < 1.0e-5; }

    static constexpr int kMaxGenId = 9 * 128 + 1;

    /** Comportamiento de cada modelo (buses, ataque, percusión, vibrato…). */
    struct Voicing;

private:
    struct Gen
    {
        double inc = 0, phase0 = 0;
        const float* table = nullptr;
        float gain = 0, target = 0, step = 0;
        float pgain = 0, ptarget = 0, pstep = 0;
        float nt = 0, np = 0;
        bool active = false, orphan = false;
    };
    struct Key { uint32_t mask = 0; bool down = false, exists = false; };
    struct Event { int64_t at; int note; int bus; bool close; };

    const Voicing& voicing() const;
    Gen& gen (int id);
    void revoice();
    void recompute();
    void updateLeveller();
    void scheduleContacts (int note, bool close);
    void cancelEvents (int note);
    void applyEvent (const Event& e);
    void renderSpan (float* out, int n);
    float nextRandom();

    double sampleRate_ = 48000;
    uint32_t seed_ = 1, rng_ = 1;
    OrganParams params_;
    bool prepared_ = false;

    // Tablas de onda compartidas: (timbre, nº de armónicos) → un ciclo + 1 muestra de guarda.
    std::map<std::pair<int, int>, std::vector<float>> tables_;
    // Por modelo y generador: tabla y frecuencia (precalculado en prepare).
    struct GenSpec { const float* table = nullptr; double freq = 0; };
    std::array<std::vector<GenSpec>, 6> specs_;

    // Dos bancos: el del modelo actual y el del anterior, que se apaga en 20 ms al cambiar de modelo.
    std::array<std::vector<Gen>, 2> banks_;
    int bank_ = 0;
    std::vector<Gen*> active_;
    int activeCount_ = 0;

    std::array<Key, 128> keys_ {};
    std::vector<Event> queue_;  // ordenada por instante
    int64_t clock_ = 0;
    bool dirty_ = false, lvlDirty_ = true;
    int downCount_ = 0;

    // Scanner (vibrato/chorus): retardo modulado.
    std::array<float, 1024> scanBuf_ {};
    int scanW_ = 0;
    double scanLfo_ = 0, scanInc_ = 0, scanBase_ = 0, scanDepth_ = 0;
    bool scanOn_ = false, scanChorus_ = false, scanTriangle_ = true;

    double percCharge_ = 1, percDecay_ = 1, percDrain_ = 0, percRecharge_ = 0;
    float clickEnv_ = 0, clickDecay_ = 0, lastNoise_ = 0;
    float chiffEnv_ = 0, chiffDecay_ = 0, chiffLp_ = 0;
    float manualGain_ = 1, percLevel_ = 0, vol_ = 0;
    float lvl_, lvlTarget_, lvlStep_ = 0, lvlRise_ = 0;

    std::vector<float> main_, perc_;
};
} // namespace bk::organ
