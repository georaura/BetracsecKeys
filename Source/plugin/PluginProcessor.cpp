#include "PluginProcessor.h"
#include "KeysSampleFile.h"
#include "PluginEditor.h"

namespace id
{
static const juce::String volume = "volume", tune = "tune", transpose = "transpose", touch = "touch",
                          dynamics = "dynamics", release = "release", softRelease = "softRelease",
                          bass = "bass", treble = "treble", reverb = "reverb", room = "room",
                          stringRes = "stringRes", releaseNoise = "releaseNoise",
                          pianoOn = "pianoOn", organOn = "organOn", organVolume = "organVolume", organModel = "organModel",
                          organVib = "organVib", organVibType = "organVibType", percOn = "percOn", perc3rd = "perc3rd",
                          percFast = "percFast", percSoft = "percSoft", organClick = "organClick", organSustain = "organSustain",
                          organPedal = "organPedal", organOctave = "organOctave", rotaryOn = "rotaryOn", rotaryFast = "rotaryFast",
                          rotaryDrive = "rotaryDrive", pianoVolume = "pianoVolume",
                          samplesOn = "samplesOn", samplesVolume = "samplesVolume", samplesAttack = "samplesAttack",
                          samplesRelease = "samplesRelease", samplesVelocity = "samplesVelocity", samplesOctave = "samplesOctave",
                          synthOn = "synthOn", synthVolume = "synthVolume",
                          fmOn = "fmOn", fmVolume = "fmVolume", fmNormalize = "fmNormalize", fmBright = "fmBright", fmAttack = "fmAttack", fmRelease = "fmRelease";
static juce::String drawbar (int i) { return "drawbar" + juce::String (i + 1); }
}

static const juce::StringArray kOrganModels { "B3", "Vox", "Farfisa", "Pipe 1", "Pipe 2", "B3 Bass" };
static const juce::StringArray kVibTypes { "V1", "V2", "V3", "C1", "C2", "C3" };

// Parámetros del synth: id, nombre, rango (crudo) y cómo se muestran.
struct SynthParamSpec
{
    juce::String id, name;
    float min, max, step, def;
    std::function<juce::String (float, int)> text;
    juce::StringArray choices;
};

static juce::String secondsText (double s)
{
    if (std::isinf (s)) return juce::String::fromUTF8 ("∞");
    return s < 1.0 ? juce::String (juce::roundToInt (s * 1000)) + " ms" : juce::String (s, s < 10 ? 2 : 1) + " s";
}

static const std::vector<SynthParamSpec>& synthParamSpecs()
{
    using namespace bk::synth;
    static const std::vector<SynthParamSpec> specs = [] {
        auto tenth = [] (float v, int) { return juce::String (v / 12.7f, 1); };
        auto atk = [] (float v, int) { return secondsText (attackSeconds (v)); };
        auto dec = [] (float v, int) { return secondsText (decaySeconds (v, true)); };
        auto rel = [] (float v, int) { return secondsText (decaySeconds (v, false)); };
        auto hz = [] (float v, int) { if (v >= 127) return juce::String ("Abierto"); const double f = cutoffHz (v); return f >= 1000 ? juce::String (f / 1000, 1) + " kHz" : juce::String (juce::roundToInt (f)) + " Hz"; };
        auto plain = [] (float v, int) { return juce::String (juce::roundToInt (v)); };
        std::vector<SynthParamSpec> s;
        s.push_back ({ "synthSource", juce::String::fromUTF8 ("Oscilador"), 0, 4, 1, 0, {}, { "Analog", "FM-H", "FM-I", "Wave", "Sample" } });
        s.push_back ({ "synthCategory", juce::String::fromUTF8 ("Categoría"), 0, 7, 1, 0, plain, {} });
        s.push_back ({ "synthWave", "Onda", 0, 60, 1, 2, plain, {} });
        s.push_back ({ "synthOscCtrl", "Osc Ctrl", 0, 127, 1, 0, tenth, {} });
        s.push_back ({ "synthCoarse", "Tono", -24, 24, 1, 0, [] (float v, int) { return (v > 0 ? "+" : "") + juce::String (juce::roundToInt (v)) + " st"; }, {} });
        s.push_back ({ "synthFine", "Fino", -50, 50, 1, 0, plain, {} });
        s.push_back ({ "synthOctave", "Octava synth", -2, 2, 1, 0, plain, {} });
        s.push_back ({ "synthOscEnvA", "Osc env A", 0, 127, 1, 0, atk, {} });
        s.push_back ({ "synthOscEnvD", "Osc env D", 0, 127, 1, 60, dec, {} });
        s.push_back ({ "synthOscEnvR", "Osc env R", 0, 127, 1, 0, rel, {} });
        s.push_back ({ "synthOscEnvAmt", "Osc env amt", -63, 63, 1, 0, plain, {} });
        s.push_back ({ "synthOscEnvPitch", "Osc env a tono", 0, 1, 1, 0, {}, { "Off", "On" } });
        s.push_back ({ "synthOscEnvVel", "Osc env velocidad", 0, 1, 1, 0, {}, { "Off", "On" } });
        s.push_back ({ "synthFilterOn", "Filtro", 0, 1, 1, 1, {}, { "Off", "On" } });
        s.push_back ({ "synthFilterType", "Tipo de filtro", 0, 5, 1, 1, {}, { "LP12", "LP24", "LPM", "LP+HP", "BP", "HP" } });
        s.push_back ({ "synthCutoff", "Frecuencia", 0, 127, 1, 100, hz, {} });
        s.push_back ({ "synthReso", "Resonancia", 0, 127, 1, 0, tenth, {} });
        s.push_back ({ "synthTrack", "Seguimiento", 0, 3, 1, 2, {}, { "Off", "1/3", "2/3", "1" } });
        s.push_back ({ "synthDrive", "Drive", 0, 3, 1, 0, {}, { "0", "1", "2", "3" } });
        s.push_back ({ "synthFEnvAmt", "Env filtro", 0, 127, 1, 0, tenth, {} });
        s.push_back ({ "synthFEnvA", "Filtro A", 0, 127, 1, 0, atk, {} });
        s.push_back ({ "synthFEnvD", "Filtro D", 0, 127, 1, 60, dec, {} });
        s.push_back ({ "synthFEnvR", "Filtro R", 0, 127, 1, 40, rel, {} });
        s.push_back ({ "synthFilterVel", "Filtro velocidad", 0, 1, 1, 0, {}, { "Off", "On" } });
        s.push_back ({ "synthAmpA", "Amp A", 0, 127, 1, 0, atk, {} });
        s.push_back ({ "synthAmpD", "Amp D", 0, 127, 1, 127, dec, {} });
        s.push_back ({ "synthAmpR", "Amp R", 0, 127, 1, 40, rel, {} });
        s.push_back ({ "synthAmpVel", "Velocidad", 0, 3, 1, 1, {}, { "0", "1", "2", "3" } });
        s.push_back ({ "synthLfoTarget", "LFO destino", 0, 3, 1, 0, {}, { "Off", "Osc", "Tono", "Filtro" } });
        s.push_back ({ "synthLfoShape", "LFO forma", 0, 4, 1, 0, {}, { "Triangle", "Saw", "Saw down", "Square", "S/H" } });
        s.push_back ({ "synthLfoRate", "LFO velocidad", 0, 127, 1, 40, [] (float v, int) { return juce::String (lfoRateHz (v), 2) + " Hz"; }, {} });
        s.push_back ({ "synthLfoAmt", "LFO cantidad", 0, 127, 1, 0, tenth, {} });
        s.push_back ({ "synthVibMode", "Vibrato", 0, 4, 1, 0, {}, { "Off", "On", "Retardo", "Rueda", "A.T." } });
        s.push_back ({ "synthVibRate", "Vibrato velocidad", 0, 127, 1, 60, [] (float v, int) { return juce::String (2.0 + 6.0 * v / 127.0, 1) + " Hz"; }, {} });
        s.push_back ({ "synthVibAmt", "Vibrato cantidad", 0, 50, 1, 10, [] (float v, int) { return juce::String (v / 5.0f, 1); }, {} });
        s.push_back ({ "synthMono", "Mono", 0, 1, 1, 0, {}, { "Off", "On" } });
        s.push_back ({ "synthLegato", "Legato", 0, 1, 1, 0, {}, { "Off", "On" } });
        s.push_back ({ "synthGlide", "Glide", 0, 127, 1, 0, tenth, {} });
        s.push_back ({ "synthUnison", "Unison", 0, 3, 1, 0, {}, { "Off", "1", "2", "3" } });
        return s;
    }();
    return specs;
}

juce::AudioProcessorValueTreeState::ParameterLayout KeysProcessor::createLayout()
{
    using namespace juce;
    auto pct = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100)) + " %"; });
    auto db = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return (v > 0 ? "+" : "") + String (v, 1) + " dB"; });
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id::volume, 1 }, "Volumen", NormalisableRange<float> (-40.f, 6.f, 0.1f), 0.f, db));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id::tune, 1 }, "Afinación"_u8, NormalisableRange<float> (-50.f, 50.f, 0.1f), 0.f,
                                                         AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return (v > 0 ? "+" : "") + String (v, 1) + " ct"; })));
    p.push_back (std::make_unique<AudioParameterInt> (ParameterID { id::transpose, 1 }, "Transponer", -12, 12, 0));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { id::touch, 1 }, "Toque", StringArray { "Ligero", "Normal", "Duro" }, 1));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id::dynamics, 1 }, "Dinámica"_u8, NormalisableRange<float> (0.f, 1.f), 1.f, pct));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id::release, 1 }, "Apagador", NormalisableRange<float> (0.25f, 4.f, 0.f, 0.5f), 1.f, pct));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { id::softRelease, 1 }, "Soft Release", false));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { id::stringRes, 1 }, "String Res",
        ! PianoLibrary::isManagedLibrary())); // apagado donde la memoria de un plugin es limitada
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id::releaseNoise, 2 }, "Soltar tecla", NormalisableRange<float> (-40.f, 6.f, 0.5f), 0.f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return v <= -39.9f ? String ("Off") : (v > 0 ? "+" : "") + String (v, 1) + " dB"; })));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id::bass, 1 }, "Graves", NormalisableRange<float> (-12.f, 12.f, 0.1f), 0.f, db));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id::treble, 1 }, "Agudos", NormalisableRange<float> (-12.f, 12.f, 0.1f), 0.f, db));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id::reverb, 1 }, "Reverb", NormalisableRange<float> (0.f, 1.f), 0.15f, pct));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id::room, 1 }, "Sala", NormalisableRange<float> (0.f, 1.f), 0.5f, pct));

    // Secciones
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { id::pianoOn, 1 }, "Piano", true));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id::pianoVolume, 1 }, "Nivel piano", NormalisableRange<float> (-40.f, 6.f, 0.1f), 0.f, db));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { id::organOn, 1 }, "Órgano"_u8, false));
    // Samples
    auto secs = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        { return v < 1.0f ? String (roundToInt (v * 1000)) + " ms" : String (v, 2) + " s"; });
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { id::samplesOn, 1 }, "Samples", false));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id::samplesVolume, 1 }, "Nivel samples", NormalisableRange<float> (-40.f, 6.f, 0.1f), 0.f, db));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id::samplesAttack, 1 }, "Ataque", NormalisableRange<float> (0.001f, 3.f, 0.f, 0.3f), 0.003f, secs));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id::samplesRelease, 1 }, "Liberación"_u8, NormalisableRange<float> (0.01f, 6.f, 0.f, 0.3f), 0.3f, secs));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id::samplesVelocity, 1 }, "Velocidad", NormalisableRange<float> (0.f, 1.f), 0.6f, pct));
    p.push_back (std::make_unique<AudioParameterInt> (ParameterID { id::samplesOctave, 1 }, "Octava samples", -2, 2, 0));
    // Synth: los mandos van en valores crudos del panel (0..127), con el texto en unidades reales.
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { id::synthOn, 1 }, "Synth", false));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id::synthVolume, 1 }, "Nivel synth", NormalisableRange<float> (-40.f, 6.f, 0.1f), 0.f, db));
    // FM (partes FM-X de MONTAGE/MODX): nivel y tres retoques sobre la performance.
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { id::fmOn, 1 }, "FM", false));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id::fmVolume, 1 }, "Nivel FM", NormalisableRange<float> (-40.f, 6.f, 0.1f), 0.f, db));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { id::fmNormalize, 1 }, "Igualar volumen FM", true));
    {
        auto offset = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            const int x = juce::roundToInt (v * 100);
            return x == 0 ? juce::String ("0") : (x > 0 ? "+" : "") + juce::String (x);
        });
        p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id::fmBright, 1 }, "Brillo FM", NormalisableRange<float> (-1.f, 1.f, 0.01f), 0.f, offset));
        p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id::fmAttack, 1 }, "Ataque FM", NormalisableRange<float> (-1.f, 1.f, 0.01f), 0.f, offset));
        p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id::fmRelease, 1 }, "Release FM", NormalisableRange<float> (-1.f, 1.f, 0.01f), 0.f, offset));
    }
    for (const auto& sp : synthParamSpecs())
    {
        if (sp.choices.isEmpty())
            p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { sp.id, 1 }, sp.name, NormalisableRange<float> (sp.min, sp.max, sp.step), sp.def,
                AudioParameterFloatAttributes().withStringFromValueFunction (sp.text)));
        else
            p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { sp.id, 1 }, sp.name, sp.choices, int (sp.def)));
    }
    // Órgano (los presets .ns4o los rellenan)
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id::organVolume, 1 }, "Volumen órgano"_u8, NormalisableRange<float> (-40.f, 6.f, 0.1f), 0.f, db));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { id::organModel, 1 }, "Modelo de órgano"_u8, kOrganModels, 0));
    const int defaults[9] = { 8, 8, 8, 0, 0, 0, 0, 0, 0 };
    static const char* footages[9] = { "16'", "5 1/3'", "8'", "4'", "2 2/3'", "2'", "1 3/5'", "1 1/3'", "1'" };
    for (int i = 0; i < 9; ++i)
        p.push_back (std::make_unique<AudioParameterInt> (ParameterID { id::drawbar (i), 1 }, String ("Drawbar ") + footages[i], 0, 8, defaults[i]));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { id::organVib, 1 }, "Vibrato/Chorus", false));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { id::organVibType, 1 }, "Tipo de vibrato", kVibTypes, 5));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { id::percOn, 1 }, "Percusión"_u8, false));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { id::perc3rd, 1 }, "Percusión 3ª"_u8, false));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { id::percFast, 1 }, "Percusión rápida"_u8, true));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { id::percSoft, 1 }, "Percusión suave"_u8, false));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id::organClick, 1 }, "Click", NormalisableRange<float> (0.f, 1.f), 0.5f, pct));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { id::organSustain, 1 }, "Sustain órgano"_u8, false));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { id::organPedal, 1 }, "Pedal en el órgano"_u8, false));
    p.push_back (std::make_unique<AudioParameterInt> (ParameterID { id::organOctave, 1 }, "Octava órgano"_u8, -2, 2, 0));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { id::rotaryOn, 1 }, "Rotary", true));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { id::rotaryFast, 1 }, "Rotary rápido"_u8, false));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id::rotaryDrive, 1 }, "Drive", NormalisableRange<float> (0.f, 1.f), 0.15f, pct));
    return { p.begin(), p.end() };
}

KeysProcessor::KeysProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      params (*this, nullptr, "BetracsecKeys", createLayout())
{
    startTimerHz (30);
}

KeysProcessor::~KeysProcessor()
{
    stopTimer();
    cancelLoad_ = true;
    if (loader_.joinable()) loader_.join();
    if (importer_.joinable()) importer_.join();
    if (sampleLoader_.joinable()) sampleLoader_.join();
    if (synthSampleLoader_.joinable()) synthSampleLoader_.join();
    const juce::SpinLock::ScopedLockType l (engineLock_);
    synth_.setSample (nullptr);
    engine_.setSound (nullptr);
    samples_.setSound (nullptr);
}

bool KeysProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void KeysProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sampleRate_ = sampleRate;
    {
        const juce::SpinLock::ScopedLockType l (engineLock_);
        engine_.prepare (sampleRate);
    }
    juce::dsp::ProcessSpec stereo { sampleRate, juce::uint32 (samplesPerBlock), 2 };
    juce::dsp::ProcessSpec mono { sampleRate, juce::uint32 (samplesPerBlock), 1 };
    for (auto* f : { &bassL_, &bassR_, &trebleL_, &trebleR_ }) { f->prepare (mono); f->reset(); }
    lastBass_ = lastTreble_ = 1000; // fuerza el cálculo de coeficientes
    updateFilters();
    reverb_.prepare (stereo);
    reverb_.reset();
    limiter_.prepare (stereo);
    limiter_.setThreshold (-1.0f);
    limiter_.setRelease (80.0f);
    volume_.reset (sampleRate, 0.05);
    volume_.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (params.getRawParameterValue (id::volume)->load()));
    scratch_.allocate (size_t (samplesPerBlock) * 2, true);
    scratchSize_ = samplesPerBlock;
    {
        const juce::SpinLock::ScopedLockType l (engineLock_);
        samples_.prepare (sampleRate);
    }
    sampleL_.allocate (size_t (samplesPerBlock), true);
    synthL_.allocate (size_t (samplesPerBlock), true);
    synthR_.allocate (size_t (samplesPerBlock), true);
    {
        const juce::SpinLock::ScopedLockType l (engineLock_);
        synth_.prepare (sampleRate);
    }
    fmL_.allocate (size_t (samplesPerBlock), true);
    fmR_.allocate (size_t (samplesPerBlock), true);
    bk::fmx::FmxEngine::buildTables();
    {
        const juce::SpinLock::ScopedLockType l (engineLock_);
        fm_.prepare (sampleRate);
        awm_.prepare (sampleRate);
    }
    sampleR_.allocate (size_t (samplesPerBlock), true);
    organ_.setParams (organParamsFromState());
    organ_.prepare (sampleRate);
    rotary_.prepare (sampleRate);
    organBlock_ = samplesPerBlock;
    organMono_.allocate (size_t (samplesPerBlock), true);
    organL_.allocate (size_t (samplesPerBlock), true);
    organR_.allocate (size_t (samplesPerBlock), true);
    organHeld_.fill (false);
    organSustained_.fill (false);
    organPedal_ = false;
}

bk::organ::OrganParams KeysProcessor::organParamsFromState() const
{
    auto get = [this] (const juce::String& pid) { return params.getRawParameterValue (pid)->load(); };
    bk::organ::OrganParams p;
    p.model = bk::organ::Model (juce::jlimit (0, 5, int (get (id::organModel))));
    for (int i = 0; i < 9; ++i) p.drawbars[size_t (i)] = get (id::drawbar (i));
    p.vibOn = get (id::organVib) > 0.5f;
    p.vibType = bk::organ::VibType (juce::jlimit (0, 5, int (get (id::organVibType))));
    p.percussion = { get (id::percOn) > 0.5f, get (id::perc3rd) > 0.5f, get (id::percFast) > 0.5f, get (id::percSoft) > 0.5f };
    p.keyClick = get (id::organClick);
    p.sustain = get (id::organSustain) > 0.5f;
    p.sustainPedal = get (id::organPedal) > 0.5f;
    p.octaveShift = int (get (id::organOctave));
    const float v = get (id::organVolume);
    p.volumeDb = v <= -39.9f ? -120.0f : v;
    return p;
}

void KeysProcessor::organNoteOn (int note)
{
    if (note < 0 || note > 127) return;
    organHeld_[size_t (note)] = true;
    organSustained_[size_t (note)] = false;
    organ_.noteOn (note);
}

void KeysProcessor::organNoteOff (int note)
{
    if (note < 0 || note > 127) return;
    organHeld_[size_t (note)] = false;
    if (organPedal_ && organ_.params().sustainPedal) organSustained_[size_t (note)] = true;
    else organ_.noteOff (note);
}

void KeysProcessor::organSustainPedal (bool down)
{
    organPedal_ = down;
    if (down) return;
    for (int n = 0; n < 128; ++n)
        if (organSustained_[size_t (n)] && ! organHeld_[size_t (n)]) { organSustained_[size_t (n)] = false; organ_.noteOff (n); }
}

void KeysProcessor::updateFilters()
{
    const float bass = params.getRawParameterValue (id::bass)->load();
    const float treble = params.getRawParameterValue (id::treble)->load();
    using C = juce::dsp::IIR::Coefficients<float>;
    if (! juce::exactlyEqual (bass, lastBass_))
    {
        lastBass_ = bass;
        auto c = C::makeLowShelf (sampleRate_, 150.0f, 0.7f, juce::Decibels::decibelsToGain (bass));
        *bassL_.coefficients = *c; *bassR_.coefficients = *c;
    }
    if (! juce::exactlyEqual (treble, lastTreble_))
    {
        lastTreble_ = treble;
        auto c = C::makeHighShelf (sampleRate_, 4000.0f, 0.7f, juce::Decibels::decibelsToGain (treble));
        *trebleL_.coefficients = *c; *trebleR_.coefficients = *c;
    }
}

void KeysProcessor::applyParams()
{
    bk::EngineParams p;
    p.tuneCents = params.getRawParameterValue (id::tune)->load();
    p.transpose = int (params.getRawParameterValue (id::transpose)->load());
    const int touch = int (params.getRawParameterValue (id::touch)->load());
    p.touchOffset = touch == 0 ? -15 : touch == 2 ? 15 : 0; // ±3 dB al elegir capa
    p.dynamics = params.getRawParameterValue (id::dynamics)->load();
    p.releaseScale = params.getRawParameterValue (id::release)->load();
    p.softRelease = params.getRawParameterValue (id::softRelease)->load() > 0.5f;
    p.stringRes = params.getRawParameterValue (id::stringRes)->load() > 0.5f;
    // dB sobre el nivel que trae el archivo (+0x33); el mínimo lo apaga.
    const float noiseDb = params.getRawParameterValue (id::releaseNoise)->load();
    p.releaseNoise = noiseDb <= -39.9f ? 0.0f : juce::Decibels::decibelsToGain (noiseDb);
    engine_.setParams (p);

    juce::dsp::Reverb::Parameters r;
    const float amount = params.getRawParameterValue (id::reverb)->load();
    const float room = params.getRawParameterValue (id::room)->load();
    r.roomSize = 0.3f + 0.65f * room;
    r.damping = 0.6f - 0.3f * room;
    r.width = 1.0f;
    r.wetLevel = amount * 0.5f;
    r.dryLevel = 1.0f - amount * 0.35f;
    reverb_.setParameters (r);
}

void KeysProcessor::renderEngine (float* l, float* r, int n)
{
    if (params.getRawParameterValue (id::pianoOn)->load() > 0.5f || engine_.activeVoices() > 0)
    {
        // El piano se suma con su nivel de sección (los búferes llegan a cero: escala solo lo suyo).
        engine_.render (l, r, n);
        const float v = params.getRawParameterValue (id::pianoVolume)->load();
        if (v < -0.05f || v > 0.05f)
        {
            const float g = v <= -39.9f ? 0.0f : juce::Decibels::decibelsToGain (v);
            juce::FloatVectorOperations::multiply (l, g, n);
            juce::FloatVectorOperations::multiply (r, g, n);
        }
    }
    // Samples, con su nivel de sección.
    const bool samplesOn = params.getRawParameterValue (id::samplesOn)->load() > 0.5f;
    if ((samplesOn || samples_.activeVoices() > 0) && n <= organBlock_)
    {
        juce::FloatVectorOperations::clear (sampleL_.get(), n);
        juce::FloatVectorOperations::clear (sampleR_.get(), n);
        samples_.render (sampleL_.get(), sampleR_.get(), n);
        const float v = params.getRawParameterValue (id::samplesVolume)->load();
        const float g = v <= -39.9f ? 0.0f : juce::Decibels::decibelsToGain (v);
        juce::FloatVectorOperations::addWithMultiply (l, sampleL_.get(), g, n);
        juce::FloatVectorOperations::addWithMultiply (r, sampleR_.get(), g, n);
    }
    // Synth, con su nivel y el limitador suave de su salida.
    const bool synthOn = params.getRawParameterValue (id::synthOn)->load() > 0.5f;
    if ((synthOn || synth_.activeVoices() > 0) && n <= organBlock_)
    {
        juce::FloatVectorOperations::clear (synthL_.get(), n);
        juce::FloatVectorOperations::clear (synthR_.get(), n);
        synth_.render (synthL_.get(), synthR_.get(), n);
        const float v = params.getRawParameterValue (id::synthVolume)->load();
        const float g = v <= -39.9f ? 0.0f : juce::Decibels::decibelsToGain (v);
        for (int i = 0; i < n; ++i)
        {
            l[i] += bk::synth::softLimit (synthL_[i]) * g;
            r[i] += bk::synth::softLimit (synthR_[i]) * g;
        }
    }
    // FM, con su nivel y el mismo limitador suave que el synth.
    const bool fmOn = params.getRawParameterValue (id::fmOn)->load() > 0.5f;
    if ((fmOn || fm_.activeVoices() > 0 || awm_.activeVoices() > 0) && n <= organBlock_)
    {
        juce::FloatVectorOperations::clear (fmL_.get(), n);
        juce::FloatVectorOperations::clear (fmR_.get(), n);
        fm_.render (fmL_.get(), fmR_.get(), n);
        awm_.render (fmL_.get(), fmR_.get(), n);
        const float v = params.getRawParameterValue (id::fmVolume)->load();
        const float g = v <= -39.9f ? 0.0f : juce::Decibels::decibelsToGain (v);
        for (int i = 0; i < n; ++i)
        {
            l[i] += bk::synth::softLimit (fmL_[i]) * g;
            r[i] += bk::synth::softLimit (fmR_[i]) * g;
        }
    }
    // Órgano → rotary, sumado al piano. Calla del todo cuando está apagado y sin cola.
    const bool organOn = params.getRawParameterValue (id::organOn)->load() > 0.5f;
    if ((organOn || ! organ_.isSilent()) && n <= organBlock_)
    {
        organ_.render (organMono_.get(), n);
        rotary_.process (organMono_.get(), organL_.get(), organR_.get(), n);
        juce::FloatVectorOperations::add (l, organL_.get(), n);
        juce::FloatVectorOperations::add (r, organR_.get(), n);
    }
}

void KeysProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    buffer.clear();
    keyboardState.processNextMidiBuffer (midi, 0, numSamples, true);

    const juce::SpinLock::ScopedTryLockType lock (engineLock_);
    if (! lock.isLocked()) return; // cambiando de piano: un bloque en silencio

    applyParams();
    updateFilters();
    organ_.setParams (organParamsFromState());
    rotary_.setEnabled (params.getRawParameterValue (id::rotaryOn)->load() > 0.5f);
    rotary_.setDrive (params.getRawParameterValue (id::rotaryDrive)->load());
    rotary_.setSpeed (params.getRawParameterValue (id::rotaryFast)->load() > 0.5f ? bk::organ::Rotary::Speed::Fast
                                                                                    : bk::organ::Rotary::Speed::Slow);
    const bool pianoOn = params.getRawParameterValue (id::pianoOn)->load() > 0.5f;
    const bool organOn = params.getRawParameterValue (id::organOn)->load() > 0.5f;
    const bool samplesOn = params.getRawParameterValue (id::samplesOn)->load() > 0.5f;
    const bool synthOn = params.getRawParameterValue (id::synthOn)->load() > 0.5f;
    synth_.setParams (synthParamsFromState());
    const bool fmOn = params.getRawParameterValue (id::fmOn)->load() > 0.5f;
    fm_.setTweaks ({ params.getRawParameterValue (id::fmBright)->load(), params.getRawParameterValue (id::fmAttack)->load(),
                     params.getRawParameterValue (id::fmRelease)->load() });
    fm_.setTuneCents (params.getRawParameterValue (id::tune)->load());
    fm_.setGain (params.getRawParameterValue (id::fmNormalize)->load() > 0.5f ? fmNormGain_.load() : 1.0f);
    awm_.setGain (params.getRawParameterValue (id::fmNormalize)->load() > 0.5f ? fmNormGain_.load() : 1.0f);
    awm_.setTuneCents (params.getRawParameterValue (id::tune)->load());
    {
        bk::nsmp::SampleParams sp;
        sp.attackSec = params.getRawParameterValue (id::samplesAttack)->load();
        sp.releaseSec = params.getRawParameterValue (id::samplesRelease)->load();
        sp.velocity = params.getRawParameterValue (id::samplesVelocity)->load();
        sp.octaveShift = int (params.getRawParameterValue (id::samplesOctave)->load());
        sp.tuneCents = params.getRawParameterValue (id::tune)->load();
        samples_.setParams (sp);
    }
    if (organWasOn_ && ! organOn) { organ_.allNotesOff(); organHeld_.fill (false); organSustained_.fill (false); }
    organWasOn_ = organOn;

    // Siempre renderiza en estéreo (en un búfer propio si la salida es mono).
    const bool stereoOut = buffer.getNumChannels() >= 2;
    if (! stereoOut && numSamples > scratchSize_) return;
    float* L = buffer.getWritePointer (0);
    float* R = stereoOut ? buffer.getWritePointer (1) : scratch_.get();
    if (! stereoOut) juce::FloatVectorOperations::clear (R, numSamples);

    int pos = 0;
    for (const auto meta : midi)
    {
        const int at = juce::jlimit (0, numSamples, meta.samplePosition);
        if (at > pos) { renderEngine (L + pos, R + pos, at - pos); pos = at; }
        const auto m = meta.getMessage();
        if (m.isNoteOn())
        {
            if (pianoOn) engine_.noteOn (m.getNoteNumber(), m.getVelocity());
            if (organOn) organNoteOn (m.getNoteNumber());
            if (samplesOn) samples_.noteOn (m.getNoteNumber(), m.getVelocity());
            if (synthOn) synth_.noteOn (m.getNoteNumber(), m.getVelocity());
            if (fmOn) { fm_.noteOn (m.getNoteNumber(), m.getVelocity()); awm_.noteOn (m.getNoteNumber(), m.getVelocity()); }
        }
        else if (m.isNoteOff())
        {
            engine_.noteOff (m.getNoteNumber());
            organNoteOff (m.getNoteNumber());
            samples_.noteOff (m.getNoteNumber());
            synth_.noteOff (m.getNoteNumber());
            fm_.noteOff (m.getNoteNumber());
            awm_.noteOff (m.getNoteNumber());
        }
        else if (m.isSustainPedalOn()) { engine_.setSustain (true); organSustainPedal (true); samples_.setSustain (true); synth_.setSustain (true); fm_.setSustain (true); awm_.setSustain (true); }
        else if (m.isSustainPedalOff()) { engine_.setSustain (false); organSustainPedal (false); samples_.setSustain (false); synth_.setSustain (false); fm_.setSustain (false); awm_.setSustain (false); }
        else if (m.isAftertouch() || m.isChannelPressure())
            synth_.setAftertouch (float (m.isAftertouch() ? m.getAfterTouchValue() : m.getChannelPressureValue()) / 127.0f);
        else if (m.isPitchWheel())
        {
            fm_.setPitchBend (float (m.getPitchWheelValue() - 8192) / 8192.0f);
            awm_.setPitchBend (float (m.getPitchWheelValue() - 8192) / 8192.0f);
        }
        else if (m.isController() && m.getControllerNumber() == 1 && ! organOn)
        {
            synth_.setModWheel (float (m.getControllerValue()) / 127.0f);
            fm_.setModWheel (float (m.getControllerValue()) / 127.0f);
        }
        else if (m.isController() && m.getControllerNumber() == 1 && organOn)
        {
            synth_.setModWheel (float (m.getControllerValue()) / 127.0f);
            fm_.setModWheel (float (m.getControllerValue()) / 127.0f);
            // Rueda de modulación: rotary rápido arriba de la mitad, lento abajo (como un pedal de medio recorrido).
            const bool fast = m.getControllerValue() >= 64;
            if (auto* rp = params.getParameter (id::rotaryFast); rp && (rp->getValue() > 0.5f) != fast)
                rp->setValueNotifyingHost (fast ? 1.0f : 0.0f);
        }
        else if (m.isAllNotesOff() || m.isAllSoundOff())
        {
            engine_.allNotesOff (m.isAllSoundOff());
            samples_.allNotesOff (m.isAllSoundOff());
            synth_.allNotesOff (m.isAllSoundOff());
            fm_.allNotesOff (m.isAllSoundOff());
            awm_.allNotesOff (m.isAllSoundOff());
            organ_.allNotesOff();
            organHeld_.fill (false);
            organSustained_.fill (false);
        }
    }
    if (pos < numSamples) renderEngine (L + pos, R + pos, numSamples - pos);
    voices_ = engine_.activeVoices() + samples_.activeVoices() + synth_.activeVoices() + fm_.activeVoices() + awm_.activeVoices() + (organ_.isSilent() ? 0 : organ_.activeGenerators() > 0 ? 1 : 0);

    // EQ → reverb → volumen → limitador
    for (int i = 0; i < numSamples; ++i)
    {
        L[i] = trebleL_.processSample (bassL_.processSample (L[i]));
        R[i] = trebleR_.processSample (bassR_.processSample (R[i]));
    }
    float* chans[2] = { L, R };
    juce::dsp::AudioBlock<float> block (chans, 2, size_t (numSamples));
    juce::dsp::ProcessContextReplacing<float> ctx (block);
    reverb_.process (ctx);
    volume_.setTargetValue (juce::Decibels::decibelsToGain (params.getRawParameterValue (id::volume)->load(), -40.0f));
    for (int i = 0; i < numSamples; ++i)
    {
        const float g = volume_.getNextValue();
        L[i] *= g; R[i] *= g;
    }
    limiter_.process (ctx);

    if (! stereoOut)
        for (int i = 0; i < numSamples; ++i) L[i] = 0.5f * (L[i] + R[i]);
}

// ---------------------------------------------------------------------------------------- piano

void KeysProcessor::loadPiano (const juce::File& file)
{
    // Elegir un piano suelto deja de mostrar el preset (loadPianoPreset lo vuelve a poner tras llamar aquí).
    pianoPreset_ = juce::File();
    pianoPresetName_ = {};
    cancelLoad_ = true;
    if (loader_.joinable()) loader_.join();
    cancelLoad_ = false;
    loading_ = true;
    progress_ = 0;
    {
        const juce::ScopedLock l (infoLock_);
        pendingFile_ = file;
        error_ = {};
    }
    pianoChanged.sendChangeMessage();
    loader_ = std::thread ([this, file]
    {
        std::string err;
        const bk::LoadOptions options { params.getRawParameterValue (id::stringRes)->load() > 0.5f, true };
        auto sound = bk::loadPiano (file.getFullPathName().toStdString(), err, &progress_, &cancelLoad_, options);
        const juce::ScopedLock l (infoLock_);
        if (sound) loaded_ = std::move (sound);
        else if (! cancelLoad_) error_ = juce::String (file.getFileName()) + ": " + fromUtf8 (err);
        loading_ = false;
    });
}


void KeysProcessor::timerCallback()
{
    // Sample del synth recién cargado.
    if (! synthSampleLoading_ && synthSampleLoaded_)
    {
        std::unique_ptr<bk::nsmp::SampleSound> old;
        {
            const juce::SpinLock::ScopedLockType l (engineLock_);
            synth_.setSample (synthSampleLoaded_.get());
            old = std::move (synthSample_);
            synthSample_ = std::move (synthSampleLoaded_);
        }
        synthSampleFile_ = synthSamplePending_;
        if (synthSampleLoader_.joinable()) synthSampleLoader_.join();
        pianoChanged.sendChangeMessage();
    }
    else if (! synthSampleLoading_ && synthSampleLoader_.joinable())
    {
        synthSampleLoader_.join();
        pianoChanged.sendChangeMessage();
    }

    // Sample recién cargado: entra en el motor (el anterior se libera aquí, fuera del hilo de audio).
    if (! sampleLoading_ && sampleLoaded_)
    {
        std::unique_ptr<bk::nsmp::SampleSound> old;
        {
            const juce::SpinLock::ScopedLockType l (engineLock_);
            samples_.setSound (sampleLoaded_.get());
            old = std::move (sample_);
            sample_ = std::move (sampleLoaded_);
        }
        sampleFile_ = samplePending_;
        pianoChanged.sendChangeMessage();
    }
    else if (! sampleLoading_ && sampleLoader_.joinable() && ! sampleLoaded_)
    {
        sampleLoader_.join();
        pianoChanged.sendChangeMessage();
    }
    // Importación terminada: abre el primer piano importado.
    juce::File toOpen;
    {
        const juce::ScopedLock l (infoLock_);
        std::swap (toOpen, imported_);
    }
    if (toOpen.existsAsFile()) loadAny (toOpen);
    else if (! importing_ && importer_.joinable()) { importer_.join(); pianoChanged.sendChangeMessage(); }

    // App Standalone sin piano en su estado: vuelve a abrir el último que se usó.
    if (! triedRecent_ && wrapperType == wrapperType_Standalone)
    {
        triedRecent_ = true;
        const auto recent = library.recent();
        // Solo un piano (.npno): los presets y samples se restauran desde el estado guardado.
        if (! sound_ && ! loading_ && currentPianoFile() == juce::File() && recent.existsAsFile() && recent.hasFileExtension ("npno"))
            loadPiano (recent);
    }

    std::unique_ptr<bk::PianoSound> incoming;
    bool failed = false;
    {
        const juce::ScopedLock l (infoLock_);
        incoming = std::move (loaded_);
        failed = ! loading_ && ! error_.isEmpty() && pendingFile_ != juce::File();
        if (failed) pendingFile_ = juce::File();
    }
    if (incoming)
    {
        std::unique_ptr<bk::PianoSound> old;
        {
            const juce::SpinLock::ScopedLockType l (engineLock_);
            engine_.setSound (incoming.get());
            old = std::move (sound_);
            sound_ = std::move (incoming);
        }
        {
            const juce::ScopedLock l (infoLock_);
            currentFile_ = pendingFile_;
            pendingFile_ = juce::File();
        }
        library.noteRecent (currentFile_);
        // La carpeta de cualquier piano abierto entra en la biblioteca (así las flechas encuentran a sus vecinos).
        library.ensureFolder (currentFile_.getParentDirectory());
        pianoChanged.sendChangeMessage();
        // `old` se libera aquí, en el hilo de mensajes, nunca en el de audio.
    }
    else if (failed)
        pianoChanged.sendChangeMessage();

    // String Res cambiado: recarga el piano con o sin las grabaciones con pedal (el actual sigue
    // sonando mientras tanto). Solo si el piano las trae.
    if (sound_ && ! loading_ && sound_->meta.listCounts()[1] > 0)
    {
        const bool wanted = params.getRawParameterValue (id::stringRes)->load() > 0.5f;
        if (wanted != sound_->options.pedalDown && currentPianoFile().existsAsFile()) loadPiano (currentPianoFile());
    }
}

juce::File KeysProcessor::currentPianoFile() const
{
    const juce::ScopedLock l (infoLock_);
    return currentFile_;
}

juce::String KeysProcessor::currentPianoName() const
{
    if (pianoPresetName_.isNotEmpty() && sound_) return pianoPresetName_;
    return sound_ ? fromUtf8 (sound_->displayName()) : juce::String();
}

juce::String KeysProcessor::currentPianoDetails() const
{
    if (! sound_) return {};
    const auto& m = sound_->meta;
    if (pianoPresetName_.isNotEmpty())
        return pianoPresetPiano_ + pianoPresetNote_ + " · " + formatMB (double (sound_->memoryBytes()));
    return juce::String (bk::npno::pianoTypeName (m.pianoType)) + " · v"_u8 + juce::String (m.version) + " · "_u8
         + juce::String (m.zones.size()) + " zonas · "_u8 + juce::String (m.strokes.size()) + " grabaciones · "_u8
         + (m.channels == 2 ? "estéreo · "_u8 : "mono · "_u8)
         + (sound_->hasPedalDown ? "String Res · "_u8 : juce::String())
         + formatMB (double (sound_->memoryBytes()));
}

juce::String KeysProcessor::lastError() const
{
    const juce::ScopedLock l (infoLock_);
    return error_;
}

// ---------------------------------------------------------------------------------------- estado

void KeysProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = params.copyState();
    state.setProperty ("piano", currentPianoFile().getFullPathName(), nullptr);
    state.setProperty ("organPreset", organPreset_.getFullPathName(), nullptr);
    state.setProperty ("pianoPreset", pianoPreset_.getFullPathName(), nullptr);
    state.setProperty ("sample", sampleFile_.getFullPathName(), nullptr);
    state.setProperty ("synthPreset", synthPreset_.getFullPathName(), nullptr);
    state.setProperty ("synthSample", synthSampleFile_.getFullPathName(), nullptr);
    state.setProperty ("synthLayer", synthLayer_, nullptr);
    state.setProperty ("synthLayersOn", synthLayersOn_, nullptr);
    state.setProperty ("fmFile", fmFile_.getFullPathName(), nullptr);
    state.setProperty ("fmIndex", fmIndex_, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void KeysProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
    {
        auto state = juce::ValueTree::fromXml (*xml);
        if (! state.hasType (params.state.getType())) return;
        const juce::File piano (state.getProperty ("piano").toString());
        state.removeProperty ("piano", nullptr);
        const juce::File organ (state.getProperty ("organPreset").toString());
        state.removeProperty ("organPreset", nullptr);
        const juce::File pianoPresetFile (state.getProperty ("pianoPreset").toString());
        state.removeProperty ("pianoPreset", nullptr);
        const juce::File sampleFile (state.getProperty ("sample").toString());
        state.removeProperty ("sample", nullptr);
        const juce::File synthPresetFile (state.getProperty ("synthPreset").toString());
        const juce::File synthSampleFile (state.getProperty ("synthSample").toString());
        state.removeProperty ("synthPreset", nullptr);
        state.removeProperty ("synthSample", nullptr);
        synthLayer_ = int (state.getProperty ("synthLayer", 0));
        synthLayersOn_ = int (state.getProperty ("synthLayersOn", 0));
        state.removeProperty ("synthLayer", nullptr);
        state.removeProperty ("synthLayersOn", nullptr);
        synthPreset_ = synthPresetFile;
        const juce::File fmFile (state.getProperty ("fmFile").toString());
        const int fmIndex = int (state.getProperty ("fmIndex", -1));
        state.removeProperty ("fmFile", nullptr);
        state.removeProperty ("fmIndex", nullptr);
        if (fmFile.existsAsFile() && (fmFile != fmFile_ || fmIndex != fmIndex_)) openFm (fmFile, fmIndex);
        if (synthSampleFile.existsAsFile() && synthSampleFile != synthSampleFile_) loadSynthSample (synthSampleFile);
        if (sampleFile.existsAsFile() && sampleFile != sampleFile_) loadSample (sampleFile);
        // Solo el nombre: los ajustes ya vienen en los parámetros guardados.
        organPreset_ = organ;
        organPresetName_ = organ.existsAsFile() ? organ.getFileNameWithoutExtension() : juce::String();
        params.replaceState (state);
        if (piano.existsAsFile() && piano != currentPianoFile()) loadPiano (piano);
        // El preset de piano solo da nombre: su piano y sus ajustes ya vienen en el estado guardado
        // (al restaurar, la biblioteca aún no ha terminado de buscar).
        if (pianoPresetFile.existsAsFile() && piano.existsAsFile()) describePianoPreset (pianoPresetFile);
        else if (piano != juce::File() && ! piano.existsAsFile())
        {
            // El proyecto apunta a un piano que no está: búscalo por nombre en la biblioteca.
            if (auto found = library.findByFileName (piano.getFileName()); found.existsAsFile()) loadPiano (found);
            else
            {
                const juce::ScopedLock l (infoLock_);
                error_ = "No se encuentra " + piano.getFileName() + ". Añade su carpeta en Biblioteca."_u8;
            }
        }
    }
}

juce::AudioProcessorEditor* KeysProcessor::createEditor() { return new KeysEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new KeysProcessor(); }

// ---------------------------------------------------------------------------------------- órgano

bool KeysProcessor::loadOrganPreset (const juce::File& file)
{
    juce::MemoryBlock data;
    if (! file.loadFileAsData (data)) return false;
    std::string err;
    const auto preset = bk::organ::parseNs4o (static_cast<const uint8_t*> (data.getData()), data.getSize(), err);
    if (! preset)
    {
        const juce::ScopedLock l (infoLock_);
        error_ = file.getFileName() + ": " + fromUtf8 (err);
        pianoChanged.sendChangeMessage();
        return false;
    }
    const auto& o = preset->layerOn[0] ? preset->layers[0] : preset->layers[1];
    auto set = [this] (const juce::String& pid, float value)
    {
        if (auto* prm = params.getParameter (pid)) prm->setValueNotifyingHost (prm->convertTo0to1 (value));
    };
    set (id::organModel, float (int (o.model)));
    for (int i = 0; i < 9; ++i) set (id::drawbar (i), std::round (std::min (8.0f, o.drawbars[size_t (i)])));
    set (id::organVib, o.vibOn ? 1.0f : 0.0f);
    set (id::percOn, o.percussion.on ? 1.0f : 0.0f);
    set (id::perc3rd, o.percussion.harm3rd ? 1.0f : 0.0f);
    set (id::percFast, o.percussion.decayFast ? 1.0f : 0.0f);
    set (id::percSoft, o.percussion.volSoft ? 1.0f : 0.0f);
    set (id::organPedal, o.sustainPedal ? 1.0f : 0.0f);
    set (id::organOctave, float (o.octaveShift));
    set (id::organVolume, juce::jlimit (-40.0f, 6.0f, o.volumeDb));
    set (id::organOn, 1.0f); // elegir un preset de órgano enciende la sección
    organPreset_ = file;
    organPresetName_ = file.getFileNameWithoutExtension();
    {
        const juce::ScopedLock l (infoLock_);
        error_ = {};
    }
    pianoChanged.sendChangeMessage();
    return true;
}

juce::String KeysProcessor::currentOrganName() const
{
    return organPresetName_.isNotEmpty() ? organPresetName_ : juce::String ("Órgano"_u8);
}

juce::String KeysProcessor::currentOrganDetails() const
{
    const auto p = organParamsFromState();
    juce::String bars;
    for (int i = 0; i < 9; ++i) bars << juce::String (int (p.drawbars[size_t (i)]));
    juce::String d = juce::String (bk::organ::modelName (p.model)) + " · " + bars;
    if (p.percussion.on) d << " · perc " << (p.percussion.harm3rd ? "3ª"_u8 : "2ª"_u8);
    if (p.vibOn) d << " · " << bk::organ::vibName (p.vibType);
    return d;
}

// ---------------------------------------------------------------------------------------- presets de piano

bool KeysProcessor::loadPianoPreset (const juce::File& file)
{
    juce::MemoryBlock data;
    std::string err;
    std::optional<bk::npno::PianoPreset> preset;
    if (file.loadFileAsData (data))
        preset = bk::npno::parseNs4n (static_cast<const uint8_t*> (data.getData()), data.getSize(), err);
    auto fail = [this] (const juce::String& message)
    {
        const juce::ScopedLock l (infoLock_);
        error_ = message;
        pianoChanged.sendChangeMessage();
        return false;
    };
    if (! preset) return fail (file.getFileName() + ": " + fromUtf8 (err));

    // La capa A (o la B si es la única); Keys toca un piano por sección.
    const int L = preset->layers[0].on ? 0 : 1;
    const auto& layer = preset->layers[size_t (L)];
    const juce::String pianoName = fromUtf8 (layer.modelName);
    if (pianoName.isEmpty()) return fail (file.getFileNameWithoutExtension() + ": piano desconocido (id " + juce::String (layer.modelId) + ")");

    // Buscar su piano por nombre entre los de la biblioteca (regla de OpenNord).
    std::vector<bk::npno::LocalPiano> locals;
    for (const auto& e : library.entries())
    {
        bk::npno::LocalPiano p;
        p.path = e.file.getFullPathName().toStdString();
        p.fileName = e.file.getFileNameWithoutExtension().toStdString();
        p.headerName = e.name.toStdString();
        p.headerVariant = e.variant.toStdString();
        const int dot = e.version.indexOfChar ('.');
        p.headerVersion = dot > 0 ? e.version.substring (0, dot).getIntValue() * 100 + e.version.substring (dot + 1).getIntValue() : 0;
        locals.push_back (p);
    }
    const int hit = bk::npno::findPianoByName (layer.modelName, locals);
    if (hit < 0)
        return fail (juce::String::fromUTF8 ("Falta el piano «") + pianoName
                     + juce::String::fromUTF8 ("» de este preset: añade la carpeta de la librería de pianos que lo contiene."));

    auto set = [this] (const juce::String& pid, float value)
    {
        if (auto* prm = params.getParameter (pid)) prm->setValueNotifyingHost (prm->convertTo0to1 (value));
    };
    set (id::touch, float (layer.touch));
    set (id::softRelease, layer.softRelease ? 1.0f : 0.0f);
    set (id::stringRes, layer.stringRes ? 1.0f : 0.0f);
    set (id::pianoVolume, juce::jlimit (-40.0f, 6.0f, layer.volumeDb));
    set (id::transpose, float (juce::jlimit (-12, 12, 12 * layer.octaveShift)));
    set (id::pianoOn, 1.0f);

    loadPiano (juce::File (juce::String (locals[size_t (hit)].path)));
    describePianoPreset (file);
    pianoChanged.sendChangeMessage();
    return true;
}

void KeysProcessor::describePianoPreset (const juce::File& file)
{
    juce::MemoryBlock data;
    std::string err;
    if (! file.loadFileAsData (data)) return;
    const auto preset = bk::npno::parseNs4n (static_cast<const uint8_t*> (data.getData()), data.getSize(), err);
    if (! preset) return;
    const int L = preset->layers[0].on ? 0 : 1;
    const auto& other = preset->layers[size_t (1 - L)];
    pianoPreset_ = file;
    pianoPresetName_ = file.getFileNameWithoutExtension();
    pianoPresetPiano_ = fromUtf8 (preset->layers[size_t (L)].modelName);
    pianoPresetNote_ = other.on && ! other.modelName.empty()
        ? juce::String::fromUTF8 (" · capa B: ") + fromUtf8 (other.modelName) + juce::String::fromUTF8 (" (aún no)")
        : juce::String();
}

// ---------------------------------------------------------------------------------------- samples

void KeysProcessor::loadSample (const juce::File& file)
{
    if (sampleLoader_.joinable()) sampleLoader_.join();
    sampleLoading_ = true;
    samplePending_ = file;
    {
        const juce::ScopedLock l (infoLock_);
        error_ = {};
    }
    pianoChanged.sendChangeMessage();
    sampleLoader_ = std::thread ([this, file]
    {
        std::string err;
        auto s = file.hasFileExtension ("bkm") ? bk::bkm::read (file, err) : bk::nsmp::loadSample (file.getFullPathName().toStdString(), err);
        {
            const juce::ScopedLock l (infoLock_);
            if (s) sampleLoaded_ = std::move (s);
            else error_ = file.getFileName() + ": " + fromUtf8 (err);
        }
        sampleLoading_ = false;
    });
    // Elegir un sample enciende la sección.
    if (auto* prm = params.getParameter (id::samplesOn)) prm->setValueNotifyingHost (1.0f);
}

juce::String KeysProcessor::currentSampleName() const
{
    if (sampleLoading_) return juce::String::fromUTF8 ("Cargando…");
    if (! sample_) return {};
    return fromUtf8 (sample_->name.empty() ? sampleFile_.getFileNameWithoutExtension().toStdString() : sample_->name);
}

juce::String KeysProcessor::currentSampleDetails() const
{
    if (! sample_) return {};
    return (sample_->codec == 0 ? juce::String::fromUTF8 ("Muestreado con Keys") : juce::String ("Sample (formato ") + juce::String (sample_->codec) + ")")
         + " · " + juce::String (sample_->zones.size()) + " zonas · "
         + (sample_->stereo() ? juce::String::fromUTF8 ("estéreo") : juce::String ("mono")) + " · "
         + formatMB (double (sample_->memoryBytes()));
}

// ---------------------------------------------------------------------------------------- synth

bk::synth::SynthParams KeysProcessor::synthParamsFromState() const
{
    auto get = [this] (const char* pid) { return params.getRawParameterValue (pid)->load(); };
    using namespace bk::synth;
    SynthParams p;
    p.source = Source (juce::jlimit (0, 4, int (get ("synthSource"))));
    p.category = int (get ("synthCategory"));
    p.wave = int (get ("synthWave"));
    p.oscCtrl = get ("synthOscCtrl");
    p.coarse = int (get ("synthCoarse"));
    p.fineCents = int (get ("synthFine"));
    p.octave = int (get ("synthOctave"));
    p.oscEnvA = get ("synthOscEnvA"); p.oscEnvD = get ("synthOscEnvD"); p.oscEnvR = get ("synthOscEnvR");
    p.oscEnvAmt = get ("synthOscEnvAmt");
    p.oscEnvToPitch = get ("synthOscEnvPitch") > 0.5f;
    p.oscEnvVelocity = get ("synthOscEnvVel") > 0.5f;
    p.filterOn = get ("synthFilterOn") > 0.5f;
    p.filterType = FilterType (juce::jlimit (0, 5, int (get ("synthFilterType"))));
    p.cutoff = get ("synthCutoff");
    p.reso = get ("synthReso");
    p.track = int (get ("synthTrack"));
    p.drive = int (get ("synthDrive"));
    p.filterEnvAmt = get ("synthFEnvAmt");
    p.fEnvA = get ("synthFEnvA"); p.fEnvD = get ("synthFEnvD"); p.fEnvR = get ("synthFEnvR");
    p.filterVelocity = get ("synthFilterVel") > 0.5f;
    p.ampA = get ("synthAmpA"); p.ampD = get ("synthAmpD"); p.ampR = get ("synthAmpR");
    p.ampVelocity = int (get ("synthAmpVel"));
    p.lfoTarget = int (get ("synthLfoTarget"));
    p.lfoShape = int (get ("synthLfoShape"));
    p.lfoRate = get ("synthLfoRate");
    p.lfoAmt = get ("synthLfoAmt");
    p.vibMode = int (get ("synthVibMode"));
    p.vibRate = get ("synthVibRate");
    p.vibAmt = get ("synthVibAmt");
    p.mono = get ("synthMono") > 0.5f;
    p.legato = get ("synthLegato") > 0.5f;
    p.glide = get ("synthGlide");
    p.unison = int (get ("synthUnison"));
    p.tuneCents = params.getRawParameterValue (id::tune)->load();
    return p;
}

bool KeysProcessor::loadSynthPreset (const juce::File& file, int wantedLayer)
{
    juce::MemoryBlock data;
    std::string err;
    std::optional<bk::synth::SynthPreset> preset;
    if (file.loadFileAsData (data)) preset = bk::synth::parseNs4y (static_cast<const uint8_t*> (data.getData()), data.getSize(), err);
    if (! preset)
    {
        const juce::ScopedLock l (infoLock_);
        error_ = file.getFileName() + ": " + fromUtf8 (err);
        pianoChanged.sendChangeMessage();
        return false;
    }
    // La capa pedida, o la primera encendida (Keys toca una por sección).
    synthLayersOn_ = 0;
    for (int i = 0; i < 3; ++i) if (preset->layers[size_t (i)].on()) synthLayersOn_ |= 1 << i;
    int L = 0;
    if (wantedLayer >= 0 && wantedLayer < 3 && (synthLayersOn_ >> wantedLayer & 1)) L = wantedLayer;
    else while (L < 2 && ! preset->layers[size_t (L)].on()) ++L;
    synthLayer_ = L;
    {
        const juce::ScopedLock lk (infoLock_);
        error_ = {};
    }
    const auto& layer = preset->layers[size_t (L)];
    const auto p = bk::synth::paramsFromLayer (layer);
    auto set = [this] (const juce::String& pid, float value)
    {
        if (auto* prm = params.getParameter (pid)) prm->setValueNotifyingHost (prm->convertTo0to1 (value));
    };
    set ("synthSource", float (int (p.source)));
    set ("synthCategory", float (p.category)); set ("synthWave", float (p.wave)); set ("synthOscCtrl", p.oscCtrl);
    set ("synthCoarse", float (p.coarse)); set ("synthFine", float (juce::jlimit (-50, 50, p.fineCents))); set ("synthOctave", float (p.octave));
    set ("synthOscEnvA", p.oscEnvA); set ("synthOscEnvD", p.oscEnvD); set ("synthOscEnvR", p.oscEnvR); set ("synthOscEnvAmt", p.oscEnvAmt);
    set ("synthOscEnvPitch", p.oscEnvToPitch ? 1.f : 0.f); set ("synthOscEnvVel", p.oscEnvVelocity ? 1.f : 0.f);
    set ("synthFilterOn", p.filterOn ? 1.f : 0.f); set ("synthFilterType", float (int (p.filterType)));
    set ("synthCutoff", p.cutoff); set ("synthReso", p.reso); set ("synthTrack", float (p.track)); set ("synthDrive", float (p.drive));
    set ("synthFEnvAmt", p.filterEnvAmt); set ("synthFEnvA", p.fEnvA); set ("synthFEnvD", p.fEnvD); set ("synthFEnvR", p.fEnvR);
    set ("synthFilterVel", p.filterVelocity ? 1.f : 0.f);
    set ("synthAmpA", p.ampA); set ("synthAmpD", p.ampD); set ("synthAmpR", p.ampR); set ("synthAmpVel", float (p.ampVelocity));
    set ("synthLfoTarget", float (p.lfoTarget)); set ("synthLfoShape", float (juce::jlimit (0, 4, p.lfoShape)));
    set ("synthLfoRate", p.lfoRate); set ("synthLfoAmt", p.lfoAmt);
    set ("synthVibMode", float (juce::jlimit (0, 4, p.vibMode))); set ("synthVibRate", p.vibRate); set ("synthVibAmt", p.vibAmt);
    set ("synthMono", p.mono ? 1.f : 0.f); set ("synthLegato", p.legato ? 1.f : 0.f); set ("synthGlide", p.glide);
    set ("synthUnison", float (p.unison));
    const auto vol = layer[bk::synth::F_volume];
    set (id::synthVolume, vol <= 0 ? -40.0f : juce::jlimit (-40.0f, 6.0f, float (40.0 * std::log10 (double (vol) / 127.0))));
    set (id::synthOn, 1.0f);
    synthPreset_ = file;

    // Si suena con un sample, buscarlo en la biblioteca por el hash de su nombre (el id del preset).
    if (p.source == bk::synth::Source::Sample)
    {
        juce::File found;
        for (const auto& e : library.sampleEntries())
            if (bk::synth::soundId (e.file.getFileNameWithoutExtension().toStdString()) == layer.sampleId()) { found = e.file; break; }
        if (found.existsAsFile()) { if (found != synthSampleFile_ || ! synthSample_) loadSynthSample (found); }
        else
        {
            const char* name = bk::synth::factorySampleName (layer.sampleId());
            const juce::ScopedLock l (infoLock_);
            error_ = juce::String::fromUTF8 ("Falta el sample «") + (name ? juce::String (name) : juce::String (layer.sampleId()))
                   + juce::String::fromUTF8 ("» de este preset: añade la carpeta de tu librería de samples.");
        }
    }
    pianoChanged.sendChangeMessage();
    return true;
}

void KeysProcessor::loadSynthSample (const juce::File& file)
{
    if (synthSampleLoader_.joinable()) synthSampleLoader_.join();
    synthSampleLoading_ = true;
    synthSamplePending_ = file;
    synthSampleLoader_ = std::thread ([this, file]
    {
        std::string err;
        auto s = file.hasFileExtension ("bkm") ? bk::bkm::read (file, err) : bk::nsmp::loadSample (file.getFullPathName().toStdString(), err);
        {
            const juce::ScopedLock l (infoLock_);
            if (s) synthSampleLoaded_ = std::move (s);
            else error_ = file.getFileName() + ": " + fromUtf8 (err);
        }
        synthSampleLoading_ = false;
    });
}

juce::String KeysProcessor::currentSynthName() const
{
    return synthPreset_ != juce::File() ? synthPreset_.getFileNameWithoutExtension() : juce::String ("Synth");
}

juce::String KeysProcessor::currentSynthDetails() const
{
    using namespace bk::synth;
    const auto p = synthParamsFromState();
    static const char* filters[] = { "LP12", "LP24", "LPM", "LP+HP", "BP", "HP" };
    juce::String osc;
    switch (p.source)
    {
        case Source::Analog: { const char* n = analogWaveName (p.category, p.wave); osc = n ? n : analogCategoryName (p.category); break; }
        case Source::FmH: osc = "FM-H " + juce::String (p.wave); break;
        case Source::FmI: osc = "FM-I " + juce::String (p.wave); break;
        case Source::Wave: osc = juce::String::fromUTF8 ("Wave ") + juce::String (p.wave + 1) + juce::String::fromUTF8 (" (aprox.)"); break;
        case Source::Sample:
            osc = synthSampleLoading_ ? juce::String::fromUTF8 ("Sample: cargando…")
                : synthSample_ ? "Sample: " + fromUtf8 (synthSample_->name) : juce::String ("Sample: ninguno");
            break;
    }
    const auto dot = juce::String::fromUTF8 (" · ");
    return osc + dot + (p.filterOn ? juce::String (filters[int (p.filterType)]) : juce::String ("sin filtro"))
         + (p.unison > 0 ? dot + "unison " + juce::String (p.unison) : juce::String()) + (p.mono ? dot + "mono" : juce::String())
         + (synthPreset_ != juce::File() ? dot + "capa " + juce::String::charToString (juce::juce_wchar ('A' + synthLayer_)) : juce::String());
}

int KeysProcessor::loadAny (const juce::File& f)
{
    int panel = 0;
    if (f.hasFileExtension ("ns4o")) { if (loadOrganPreset (f)) panel = 1; }
    else if (f.hasFileExtension ("ns4n")) loadPianoPreset (f);
    else if (f.hasFileExtension ("ns4y")) { if (loadSynthPreset (f)) panel = 3; }
    else if (f.hasFileExtension ("nsmp3;nsmp4;bkm")) { loadSample (f); panel = 2; }
    else if (f.hasFileExtension ("x7l;x7u;x8l;x8u")) { if (loadFmPerformance (f)) panel = 4; }
    else loadPiano (f);
    panelRequest_ = panel;
    return panel;
}

// ---------------------------------------------------------------------------------------------- FM

std::vector<int> KeysProcessor::playableFm (const bk::ysfc::Library& lib)
{
    // Solo las que suenan completas en Keys: sin baterías ni elementos con ondas de la memoria del
    // instrumento, y con alguna parte FM-X o algún elemento AWM2 con onda de la propia librería.
    std::vector<int> out;
    for (size_t i = 0; i < lib.performances.size(); ++i)
    {
        const auto& p = lib.performances[i];
        if (! p.complete()) continue;
        bool ok = std::any_of (p.fm.begin(), p.fm.end(), [] (const bk::ysfc::FmPart& f) { return f.partOn && f.volume > 0; });
        for (const auto& part : p.awm)
            for (const auto& e : part.elements)
                ok = ok || (e.on && lib.waveFor (e) != nullptr);
        if (ok) out.push_back (int (i));
    }
    return out;
}

bool KeysProcessor::openFm (const juce::File& file, int index)
{
    std::unique_ptr<bk::ysfc::Library> lib;
    if (file == fmFile_ && fmLib_ != nullptr) lib = std::move (fmLib_);
    else
    {
        std::string err;
        auto read = bk::ysfc::readLibrary (file.getFullPathName().toStdString(), err);
        if (! read)
        {
            const juce::ScopedLock l (infoLock_);
            error_ = file.getFileName() + ": " + juce::String::fromUTF8 (err.c_str());
            return false;
        }
        lib = std::make_unique<bk::ysfc::Library> (std::move (*read));
    }
    auto playable = playableFm (*lib);
    if (playable.empty())
    {
        fmLib_ = std::move (lib);
        const juce::ScopedLock l (infoLock_);
        error_ = file.getFileName() + juce::String::fromUTF8 (": no tiene performances que Keys pueda tocar (sus muestras son de la memoria del instrumento o están en formato WXC).");
        return false;
    }
    if (std::find (playable.begin(), playable.end(), index) == playable.end()) index = playable.front();
    const auto& perf = lib->performances[size_t (index)];
    bk::fmx::FmxEngine::prepareTables (perf);
    // Muestras de las partes AWM2 (solo las ondas que usa; unos ms, 24 MB como mucho en MONTAGE Expanded).
    auto samples = bk::awm::loadSamples (lib->path, perf, lib->waves);
    {
        // Igualar volumen: todas a −22 dBFS de pico de sonoridad (±12 dB como mucho).
        const float loud = bk::awm::measureLoudness (perf, samples.get());
        fmNormGain_ = juce::Decibels::decibelsToGain (juce::jlimit (-12.0f, 12.0f, -22.0f - loud));
    }
    {
        const juce::SpinLock::ScopedLockType l (engineLock_);
        fm_.setPerformance (&perf);
        awm_.setPerformance (&perf, samples.get());
        std::swap (awmSamples_, samples);   // las anteriores se liberan fuera del bloqueo
    }
    fmLib_ = std::move (lib);
    fmPlayable_ = std::move (playable);
    fmFile_ = file;
    fmIndex_ = index;
    {
        const juce::ScopedLock l (infoLock_);
        error_.clear();
    }
    pianoChanged.sendChangeMessage();
    return true;
}

bool KeysProcessor::loadFmPerformance (const juce::File& file, int index)
{
    if (! openFm (file, index)) { pianoChanged.sendChangeMessage(); return false; }
    if (auto* on = params.getParameter (id::fmOn)) on->setValueNotifyingHost (1.0f);
    return true;
}

juce::String KeysProcessor::currentFmName() const
{
    if (fmLib_ == nullptr || fmIndex_ < 0 || size_t (fmIndex_) >= fmLib_->performances.size()) return "FM";
    return juce::String::fromUTF8 (fmLib_->performances[size_t (fmIndex_)].name.c_str());
}

juce::String KeysProcessor::currentFmDetails() const
{
    if (fmLib_ == nullptr || fmIndex_ < 0 || size_t (fmIndex_) >= fmLib_->performances.size()) return {};
    const auto& p = fmLib_->performances[size_t (fmIndex_)];
    const auto dot = juce::String::fromUTF8 (" \xc2\xb7 ");
    juce::StringArray kinds;
    if (! p.fm.empty()) kinds.add (p.fm.size() == 1 ? juce::String ("1 parte FM") : juce::String (int (p.fm.size())) + " partes FM");
    if (! p.awm.empty()) kinds.add (p.awm.size() == 1 ? juce::String ("1 parte de muestras") : juce::String (int (p.awm.size())) + " partes de muestras");
    juce::String s = kinds.joinIntoString (dot);
    if (p.fm.size() == 1 && p.awm.empty()) s << dot << "algoritmo " << (p.fm.front().algorithm + 1);
    if (! p.complete()) s << dot << juce::String::fromUTF8 ("parcial: sin muestras de la memoria del instrumento ni baterías");
    s << dot << fmFile_.getFileNameWithoutExtension();
    return s;
}
