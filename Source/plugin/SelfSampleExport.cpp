#include "SelfSampleExport.h"
#include "SelfSampler.h"
#include "KeysSampleFile.h"
#include "../core/SfgrWriter.h"

#include <map>
#include <thread>

using namespace juce;

namespace bk::selfsample
{
namespace
{
// ── Interfaz ─────────────────────────────────────────────────────────────────────────────────────
String mb (uint64_t bytes) { return String (double (bytes) / 1.0e6, bytes < 100'000'000 ? 1 : 0) + " MB"; }

struct State
{
    Source source = Source::Organ;
    String name;
    bk::organ::OrganParams organ;
    float rotaryDrive = 0.15f;
    bk::synth::SynthParams synth;
    std::unique_ptr<bk::nsmp::SampleSound> synthSample;
    File synthSampleFile;
    Plan plan;
    bool rotary = false;
    double trimDb = -70;
    float releaseSec = 0.05f;
    File target;
    std::unique_ptr<FileChooser> chooser;
    std::unique_ptr<bk::nsmp::SampleSound> sound;
    bk::sfgr::ExportResult result;
    std::string error;
    // FM: la librería se vuelve a leer aquí (la de Keys puede cambiar mientras se exporta).
    File fmFile;
    int fmIndex = -1;
    bk::fmx::Tweaks fmTweaks;
    bool fmNormalize = true;
    std::unique_ptr<bk::ysfc::Library> fmLib;
    std::unique_ptr<bk::awm::SampleSet> fmSamples;
    Probe probe;
};

/** FM: lee la librería, prepara la performance, la mide y decide bucle, duración y release. */
bool prepareFm (State& st, ThreadWithProgressWindow& w, FmSound& out)
{
    w.setStatusMessage (String::fromUTF8 ("Leyendo la librería…"));
    std::string err;
    auto read = bk::ysfc::readLibrary (st.fmFile.getFullPathName().toStdString(), err);
    if (! read) { st.error = err; return false; }
    st.fmLib = std::make_unique<bk::ysfc::Library> (std::move (*read));
    if (st.fmIndex < 0 || size_t (st.fmIndex) >= st.fmLib->performances.size()) { st.error = "La performance ya no está en la librería."; return false; }
    const auto& perf = st.fmLib->performances[size_t (st.fmIndex)];
    bk::fmx::FmxEngine::prepareTables (perf);
    st.fmSamples = bk::awm::loadSamples (st.fmLib->path, perf, st.fmLib->waves);
    out.performance = &perf;
    out.samples = st.fmSamples.get();
    out.tweaks = st.fmTweaks;
    // Igualar volumen, como en Keys: −22 dBFS de pico de sonoridad (±12 dB).
    out.gain = st.fmNormalize ? Decibels::decibelsToGain (jlimit (-12.0f, 12.0f, -22.0f - bk::awm::measureLoudness (perf, st.fmSamples.get())))
                              : 1.0f;
    w.setStatusMessage (String::fromUTF8 ("Escuchando cómo suena…"));
    st.probe = probeFm (out);
    if (st.probe.silent) { st.error = "La performance no suena con una nota C4."; return false; }
    // Si se sostiene: hasta que se asienta y algo más, con el bucle en la parte asentada. Si se apaga en menos
    // de 8 s: la caída entera, sin bucle. Si se apaga despacio: bucle con la caída quitada, que BeTracks Pro pone.
    const auto& pr = st.probe;
    st.plan.loops = pr.sustains || pr.slowDecay;
    st.plan.decayLoops = pr.slowDecay;
    st.plan.loopFromSec = pr.sustains ? pr.settleSec : pr.slowDecay ? std::max (1.0, pr.peakSec + 0.5) : 0.0;
    st.plan.holdSec = pr.sustains ? std::clamp (pr.settleSec + 2.0, 2.0, 8.0)
                    : pr.slowDecay ? 5.0
                                   : std::clamp (pr.heldTo60Sec + 0.2, 1.0, 8.0);
    st.plan.tailSec = std::clamp (pr.releaseSec * 0.75, 0.3, 6.0);
    st.releaseSec = float (pr.releaseSec);
    return true;
}

class Job : public ThreadWithProgressWindow
{
public:
    explicit Job (std::shared_ptr<State> s) : ThreadWithProgressWindow (String::fromUTF8 ("Muestreando y exportando a BeTracks Pro…"), true, true), st_ (std::move (s)) {}
    void run() override
    {
        std::atomic<double> p { 0 };
        auto& st = *st_;
        if (st.source == Source::Synth && st.synthSampleFile.existsAsFile() && st.synth.source == bk::synth::Source::Sample)
        {
            setStatusMessage (String::fromUTF8 ("Leyendo el sample del synth…"));
            std::string e;
            st.synthSample = st.synthSampleFile.hasFileExtension ("bkm") ? bk::bkm::read (st.synthSampleFile, e)
                                                                         : bk::nsmp::loadSample (st.synthSampleFile.getFullPathName().toStdString(), e);
        }
        FmSound fm;
        if (st.source == Source::Fm && ! prepareFm (st, *this, fm)) return;
        setStatusMessage (String::fromUTF8 ("Tocando cada nota…"));
        std::atomic<bool> finished { false };
        std::thread worker ([&]
        {
            st.sound = st.source == Source::Organ ? renderOrgan (st.organ, st.rotary, st.rotaryDrive, st.plan, &p, st.error)
                     : st.source == Source::Fm    ? renderFm (fm, st.plan, &p, st.error)
                                                  : renderSynth (st.synth, st.synthSample.get(), st.plan, &p, st.error);
            finished = true;
        });
        while (! finished.load()) { setProgress (p.load() * 0.85); wait (100); }
        worker.join();
        if (threadShouldExit() || st.sound == nullptr) return;
        st.sound->name = st.name.toStdString();
        setStatusMessage (st.target.getFileName());
        setProgress (0.9);
        bk::sfgr::ExportOptions o;
        o.trimDb = st.trimDb;
        o.releaseSec = st.releaseSec;
        st.result = bk::sfgr::write (st.target.getFullPathName().toStdString(), st.sound->name, *st.sound, o);
        setProgress (1.0);
    }
    void threadComplete (bool cancelled) override
    {
        auto& st = *st_;
        if (cancelled) { delete this; return; }
        if (st.sound == nullptr || ! st.result.ok)
            AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "BeTracks Pro",
                                              String::fromUTF8 ((st.sound == nullptr ? st.error : st.result.error).c_str()));
        else
        {
            int loops = 0;
            for (const auto& s : st.sound->strokes) loops += s.loops;
            AlertWindow::showMessageBoxAsync (MessageBoxIconType::InfoIcon, String::fromUTF8 ("Exportado para BeTracks Pro"),
                st.target.getFileName() + "\n\n" + String (st.result.samples) + " muestras (" + String (loops) + " con bucle) · "
                + String (st.result.zones) + " zonas\nEn RAM en BeTracks Pro: " + mb (st.result.ramBytes) + "\n\n"
                + (st.source == Source::Fm
                       ? String::fromUTF8 (st.probe.sustains ? "Se sostiene: cada muestra lleva bucle. "
                                           : st.probe.slowDecay ? "Se apaga despacio: cada muestra lleva bucle y BeTracks Pro lo va apagando (necesita BeTracks Pro con caída por zona). "
                                                                : "Se apaga sola: cada muestra lleva su caída entera, sin bucle. ")
                             + String::fromUTF8 ("Release medido: ") + String (st.releaseSec, 2) + " s.\n\n"
                       : String())
                + String::fromUTF8 ("Cópialo a la carpeta SFGRInstruments de BeTracks Pro y asígnalo a un preset. Los controles en vivo "
                                    "(drawbars, rueda, rotary rápido, pitch bend) quedan fijos en la muestra."));
            st.target.getParentDirectory().revealToUser();
        }
        delete this;
    }

private:
    std::shared_ptr<State> st_;
};

void chooseTarget (std::shared_ptr<State> st)
{
    const auto folder = File::getSpecialLocation (File::userMusicDirectory).getChildFile ("Betracsec Keys").getChildFile ("BeTracks Pro");
    folder.createDirectory();
    st->chooser = std::make_unique<FileChooser> (String::fromUTF8 ("Guardar para BeTracks Pro"),
                                                 folder.getChildFile (File::createLegalFileName (st->name) + ".sfgr"), "*.sfgr");
    st->chooser->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::warnAboutOverwriting, [st] (const FileChooser& fc)
    {
        auto f = fc.getResult();
        if (f == File()) return;
        st->target = f.hasFileExtension ("sfgr") ? f : f.withFileExtension ("sfgr");
        (new Job (st))->launchThread();
    });
}
} // namespace

void exportToBeTracks (KeysProcessor& proc, Source source)
{
    auto st = std::make_shared<State>();
    st->source = source;
    const bool organ = source == Source::Organ;
    if (organ)
    {
        st->organ = proc.organParamsFromState();
        st->rotaryDrive = proc.params.getRawParameterValue ("rotaryDrive")->load();
        st->name = proc.currentOrganName();
        st->releaseSec = st->organ.sustain ? 1.0f : 0.05f;
        st->plan.velocities = { 100 };   // el órgano no responde a la fuerza
        st->plan.holdSec = 3.0;
        st->plan.tailSec = st->organ.sustain ? 2.0 : 0.5;
    }
    else if (source == Source::Fm)
    {
        st->fmFile = proc.currentFmFile();
        st->fmIndex = proc.currentFmIndex();
        st->name = proc.currentFmName();
        st->fmTweaks = { proc.params.getRawParameterValue ("fmBright")->load(), proc.params.getRawParameterValue ("fmAttack")->load(),
                         proc.params.getRawParameterValue ("fmRelease")->load() };
        st->fmNormalize = proc.params.getRawParameterValue ("fmNormalize")->load() > 0.5f;
        // Casi todo lo FM cambia de timbre con la fuerza: 4 capas por defecto. Bucle, duración y release se
        // deciden al medirla (prepareFm).
        st->plan.velocities = { 32, 64, 96, 127 };
    }
    else
    {
        st->synth = proc.synthParamsFromState();
        st->synthSampleFile = proc.currentSynthSample();
        st->name = proc.currentSynthName();
        const auto& p = st->synth;
        st->releaseSec = float (std::clamp (bk::synth::decaySeconds (p.ampR, false) * 2.0, 0.005, 60.0));
        const bool velocity = p.ampVelocity > 0 || p.filterVelocity || p.oscEnvVelocity;
        st->plan.velocities = velocity ? std::vector<int> { 32, 64, 96, 127 } : std::vector<int> { 100 };
        // Si la envolvente se sostiene (decay al máximo), basta el ataque y algo de la parte estable para el bucle.
        // Si cae hasta apagarse, se graba la caída entera SIN bucle: un bucle a mitad de la caída la dejaría
        // sonando para siempre (el buscador compara la forma de la onda, no el nivel).
        const double attack = bk::synth::attackSeconds (p.ampA);
        const bool sustains = p.ampD >= 126.5f;
        st->plan.loops = sustains;
        st->plan.holdSec = sustains ? std::clamp (attack + 1.5, 2.0, 8.0)
                                    : std::clamp (attack + bk::synth::decaySeconds (p.ampD, true) * 1.6, 1.0, 8.0);
        st->plan.tailSec = std::clamp (bk::synth::decaySeconds (p.ampR, false) * 1.5, 0.3, 6.0);
    }

    const bool fm = source == Source::Fm;
    if (fm && ! st->fmFile.existsAsFile())
    {
        AlertWindow::showMessageBoxAsync (MessageBoxIconType::InfoIcon, "BeTracks Pro", String::fromUTF8 ("Elige primero una performance FM."));
        return;
    }
    auto* w = new AlertWindow (String::fromUTF8 ("Exportar a BeTracks Pro (.sfgr)"),
        String::fromUTF8 ("«") + st->name + String::fromUTF8 ("»: Keys toca ")
            + (organ ? String::fromUTF8 ("el órgano") : fm ? String::fromUTF8 ("la performance FM") : String ("el synth"))
            + String::fromUTF8 (" nota a nota con los ajustes de ahora y guarda las muestras para BeTracks Pro. Lo que se mueve en vivo "
                                "(drawbars, rueda, rotary rápido) queda fijo."),
        MessageBoxIconType::NoIcon);
    w->addComboBox ("range", { String::fromUTF8 ("88 teclas (A0–C8)"), String::fromUTF8 ("61 teclas (C2–C7)") }, "Teclas");
    w->addComboBox ("step", { "Cada 3 semitonos", "Cada 2 semitonos", "Cada 4 semitonos" }, "Muestras");
    if (organ)
        w->addComboBox ("rotary", { String::fromUTF8 ("Sin rotary (bucles limpios)"), String::fromUTF8 ("Con el rotary en lento grabado") }, "Rotary");
    else
        w->addComboBox ("layers", { String::fromUTF8 (st->plan.velocities.size() > 1 ? (fm ? "4 capas de velocidad (recomendado en FM)" : "4 capas de velocidad")
                                                                                      : "1 capa (no responde a la fuerza)"),
                                    "1 capa", "4 capas de velocidad" }, "Velocidad");
    w->addComboBox ("trim", { String::fromUTF8 ("Recortar colas bajo −70 dB (recomendado)"), String::fromUTF8 ("Bajo −60 dB"), "Sin recorte" }, "Colas");
    w->addButton (String::fromUTF8 ("Continuar…"), 1, KeyPress (KeyPress::returnKey));
    w->addButton ("Cancelar", 0, KeyPress (KeyPress::escapeKey));
    w->enterModalState (true, ModalCallbackFunction::create ([st, w, organ] (int r)
    {
        if (r != 1) return;
        const int range = w->getComboBoxComponent ("range")->getSelectedItemIndex();
        st->plan.lowNote = range == 0 ? 21 : 36;
        st->plan.highNote = range == 0 ? 108 : 96;
        const int step = w->getComboBoxComponent ("step")->getSelectedItemIndex();
        st->plan.step = step == 0 ? 3 : step == 1 ? 2 : 4;
        if (organ) st->rotary = w->getComboBoxComponent ("rotary")->getSelectedItemIndex() == 1;
        else
        {
            const int layers = w->getComboBoxComponent ("layers")->getSelectedItemIndex();
            if (layers == 1) st->plan.velocities = { 100 };
            if (layers == 2) st->plan.velocities = { 32, 64, 96, 127 };
        }
        const int trim = w->getComboBoxComponent ("trim")->getSelectedItemIndex();
        st->trimDb = trim == 0 ? -70 : trim == 1 ? -60 : 0;
        chooseTarget (st);
    }), true);
}
} // namespace bk::selfsample
