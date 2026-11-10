// BeTracks Keys — Keys se muestrea a sí misma: toca su motor de órgano, synth o FM nota a nota (sin tiempo real
// ni hardware), recorta cada toma y busca el bucle de la parte sostenida. El resultado es un sample (el mismo
// tipo que la sección SAMPLES) listo para exportar a BeTracks Pro con Source/core/SfgrWriter.
#pragma once

#include "../core/OrganEngine.h"
#include "../core/Rotary.h"
#include "../core/SynthEngine.h"
#include "../core/FmxEngine.h"
#include "../core/AwmEngine.h"

#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace bk::selfsample
{
struct Plan
{
    int lowNote = 21, highNote = 108, step = 3;
    std::vector<int> velocities { 100 };
    double holdSec = 3.0, tailSec = 2.0;
    bool loops = true;
    double loopFromSec = 0;      ///< el bucle no empieza antes de esto (lo que se asienta tarda en hacerlo)
    /** Sonidos que se van apagando despacio: a cada toma se le quita su caída (medida en la propia toma) para que
     *  el bucle no salte de nivel, y la caída va al archivo para que BeTracks Pro la vuelva a poner (cambio 008). */
    bool decayLoops = false;
};

/** Cómo se comporta un sonido, medido tocando un C4 a fuerza 100 (para los que no se pueden deducir de sus
 *  parámetros, como una performance FM con varias partes). */
struct Probe
{
    bool silent = true;
    bool sustains = false;       ///< con la tecla pisada acaba estable (entre 4 s y 7,5 s varía menos de 3 dB)
    bool slowDecay = false;      ///< ni se estabiliza ni cae 60 dB en 8 s: se va apagando despacio
    double settleSec = 0;        ///< cuándo se asienta (si se sostiene)
    double peakSec = 0;          ///< cuándo llega a su máximo
    double heldTo60Sec = 8;      ///< cuánto tarda en caer 60 dB con la tecla pisada (8 = no llega)
    double releaseSec = 0.3;     ///< al soltar tras 1 s: cuánto tarda en caer 80 dB (el release de BeTracks Pro)
};

/** Una performance FM/AWM2 de MONTAGE/MODX tal como suena en Keys (brillo, ataque, release y nivel igualado). */
struct FmSound
{
    const bk::ysfc::Performance* performance = nullptr;
    const bk::awm::SampleSet* samples = nullptr;
    bk::fmx::Tweaks tweaks;
    float gain = 1.0f;
};

Probe probeFm (const FmSound& sound);

/** `progress` 0..1. nullptr con `error` si no suena nada. */
std::unique_ptr<bk::nsmp::SampleSound> renderOrgan (const bk::organ::OrganParams& params, bool rotary, float rotaryDrive,
                                                    const Plan& plan, std::atomic<double>* progress, std::string& error);
std::unique_ptr<bk::nsmp::SampleSound> renderSynth (const bk::synth::SynthParams& params, const bk::nsmp::SampleSound* sample,
                                                    const Plan& plan, std::atomic<double>* progress, std::string& error);
std::unique_ptr<bk::nsmp::SampleSound> renderFm (const FmSound& sound, const Plan& plan, std::atomic<double>* progress,
                                                 std::string& error);
} // namespace bk::selfsample
