// BeTracks Keys — procesador del plugin (AU, VST3 y Standalone).
#pragma once

#include "../core/PianoEngine.h"
#include "../core/OrganEngine.h"
#include "../core/Rotary.h"
#include "../core/PianoPreset.h"
#include "../core/SampleEngine.h"
#include "../core/SynthEngine.h"
#include "../core/FmxEngine.h"
#include "../core/AwmEngine.h"
#include "PianoLibrary.h"
#include "Text.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>

class KeysProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    KeysProcessor();
    ~KeysProcessor() override;

    // ---- AudioProcessor
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "BeTracks Keys"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 3.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- Piano
    /** Carga un .npno en segundo plano; el que suena sigue sonando hasta que el nuevo esté listo. */
    void loadPiano (const juce::File& file);
    juce::File currentPianoFile() const;
    /** Carga un preset de piano del Stage 4 (.ns4n): busca su piano en la biblioteca y aplica sus ajustes. */
    bool loadPianoPreset (const juce::File& file);
    juce::File currentPianoPreset() const { return pianoPreset_; }
    /** Pone el nombre del preset (y su capa B) sin cargar nada. */
    void describePianoPreset (const juce::File& file);
    juce::String currentPianoName() const;
    juce::String currentPianoDetails() const;
    bool isLoading() const { return loading_.load(); }
    bool isImporting() const { return importing_.load(); }
    float loadProgress() const { return progress_.load(); }
    juce::String lastError() const;
    int activeVoices() const { return voices_.load(); }

    // ---- Samples
    /** Carga un Nord Sample (.nsmp3/.nsmp4) en segundo plano. */
    void loadSample (const juce::File& file);
    juce::File currentSampleFile() const { return sampleFile_; }
    juce::String currentSampleName() const;
    juce::String currentSampleDetails() const;
    bool isLoadingSample() const { return sampleLoading_.load(); }

    /** Abre cualquier archivo de la biblioteca en su sección. Devuelve la sección (0 piano, 1 órgano, 2 samples, 3 synth, 4 FM). */
    int loadAny (const juce::File& file);
    /** La sección del último archivo abierto con loadAny (o −1), para que la interfaz la muestre. */
    int takePanelRequest() { return panelRequest_.exchange (-1); }

    // ---- Synth
    /** Carga un preset de synth del Stage 4 (.ns4y): sus ajustes y, si usa sample, su Nord Sample. */
    /** `layer` −1: la primera capa encendida. Keys toca una capa (A, B o C) del preset. */
    bool loadSynthPreset (const juce::File& file, int layer = -1);
    /** Capas encendidas del preset actual (bit 0 = A) y la que suena. */
    int synthLayersOn() const { return synthLayersOn_; }
    int synthLayer() const { return synthLayer_; }
    /** Sample para la fuente "Sample" del synth. */
    void loadSynthSample (const juce::File& file);
    juce::File currentSynthPreset() const { return synthPreset_; }
    juce::File currentSynthSample() const { return synthSampleFile_; }
    juce::String currentSynthName() const;
    juce::String currentSynthDetails() const;
    bk::synth::SynthParams synthParamsFromState() const;
    bk::organ::OrganParams organParamsFromState() const;

    // ---- FM (performances de MONTAGE/MODX: partes FM-X y AWM2 con ondas de la librería)
    /** Carga la performance `index` (de la librería .X7L/.X8L) en la sección FM; −1 = la primera que suena. */
    bool loadFmPerformance (const juce::File& file, int index = -1);
    juce::File currentFmFile() const { return fmFile_; }
    int currentFmIndex() const { return fmIndex_; }
    juce::String currentFmName() const;
    juce::String currentFmDetails() const;
    /** Performances de la librería actual que la sección FM puede tocar (índices). */
    const std::vector<int>& fmPlayable() const { return fmPlayable_; }
    const bk::ysfc::Library* fmLibrary() const { return fmLib_.get(); }
    /** Índices de las performances de `lib` con alguna parte FM-X que suene. */
    static std::vector<int> playableFm (const bk::ysfc::Library& lib);

    // ---- Órgano
    /** Carga un preset de órgano del Stage 4 (.ns4o): pasa sus ajustes a los parámetros del órgano. */
    bool loadOrganPreset (const juce::File& file);
    juce::File currentOrganPreset() const { return organPreset_; }
    juce::String currentOrganName() const;
    juce::String currentOrganDetails() const;

    juce::AudioProcessorValueTreeState params;
    juce::MidiKeyboardState keyboardState;
    PianoLibrary library;

    /** Avisa a la interfaz cuando cambia el piano, la carga o un error. */
    juce::ChangeBroadcaster pianoChanged;

private:
    void timerCallback() override;
    /** Lee la librería (o usa la cargada) y pasa la performance al motor FM, sin tocar los parámetros. */
    bool openFm (const juce::File& file, int index);
    void applyParams();
    void renderEngine (float* l, float* r, int n);
    void organNoteOn (int note);
    void organNoteOff (int note);
    void organSustainPedal (bool down);
    void updateFilters();

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    bk::PianoEngine engine_;
    juce::SpinLock engineLock_;               // el hilo de audio solo lo intenta, nunca espera
    std::unique_ptr<bk::PianoSound> sound_;   // el que suena (lo cambia el hilo de mensajes)

    std::thread loader_;
    std::thread importer_;
    std::atomic<bool> importing_ { false };
    juce::File imported_;   // primer piano importado, pendiente de abrir (lo abre el temporizador)
    std::atomic<bool> loading_ { false }, cancelLoad_ { false };
    std::atomic<float> progress_ { 0 };
    std::unique_ptr<bk::PianoSound> loaded_;  // recién cargado, pendiente de entrar
    juce::File pendingFile_, currentFile_;
    juce::String error_;
    mutable juce::CriticalSection infoLock_;
    std::atomic<int> voices_ { 0 };
    bool triedRecent_ = false;

    // Samples
    bk::nsmp::SampleEngine samples_;
    std::unique_ptr<bk::nsmp::SampleSound> sample_, sampleLoaded_;
    std::thread sampleLoader_;
    std::atomic<bool> sampleLoading_ { false };
    juce::File sampleFile_, samplePending_;
    juce::HeapBlock<float> sampleL_, sampleR_;

    std::atomic<int> panelRequest_ { -1 };
    // Synth
    bk::synth::SynthEngine synth_;
    std::unique_ptr<bk::nsmp::SampleSound> synthSample_, synthSampleLoaded_;
    std::thread synthSampleLoader_;
    std::atomic<bool> synthSampleLoading_ { false };
    juce::File synthPreset_, synthSampleFile_, synthSamplePending_;
    int synthLayersOn_ = 0, synthLayer_ = 0;
    juce::HeapBlock<float> synthL_, synthR_;
    // FM
    bk::fmx::FmxEngine fm_;
    bk::awm::AwmEngine awm_;                    // partes AWM2 de la misma performance
    std::unique_ptr<bk::awm::SampleSet> awmSamples_;
    std::unique_ptr<bk::ysfc::Library> fmLib_;   // librería de la performance actual (hilo de mensajes)
    std::vector<int> fmPlayable_;
    juce::File fmFile_;
    int fmIndex_ = -1;
    std::atomic<float> fmNormGain_ { 1.0f };
    juce::HeapBlock<float> fmL_, fmR_;

    // Órgano: motor mono → rotary estéreo, sumado al piano antes de la EQ y la reverb.
    bk::organ::OrganEngine organ_;
    bk::organ::Rotary rotary_;
    juce::HeapBlock<float> organMono_, organL_, organR_;
    int organBlock_ = 0;
    bool organWasOn_ = false;
    bool organPedal_ = false;
    std::array<bool, 128> organHeld_ {}, organSustained_ {};
    juce::File organPreset_;
    juce::File pianoPreset_;          // preset de piano activo (vacío si se eligió un piano suelto)
    juce::String pianoPresetName_, pianoPresetPiano_, pianoPresetNote_;
    juce::String organPresetName_;

    // Efectos
    double sampleRate_ = 48000;
    juce::dsp::IIR::Filter<float> bassL_, bassR_, trebleL_, trebleR_;
    float lastBass_ = 1000, lastTreble_ = 1000;
    juce::dsp::Reverb reverb_;
    juce::dsp::Limiter<float> limiter_;
    juce::SmoothedValue<float> volume_;
    juce::HeapBlock<float> scratch_;
    int scratchSize_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KeysProcessor)
};
