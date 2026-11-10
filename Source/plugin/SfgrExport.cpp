#include "SfgrExport.h"
#include "KeysSampleFile.h"
#include "../core/SfgrWriter.h"
#include "../core/PianoExport.h"

#include <thread>

using namespace juce;

namespace bk::sfgrui
{
namespace
{
String mb (uint64_t bytes) { return String (double (bytes) / 1.0e6, bytes < 100'000'000 ? 1 : 0) + " MB"; }

struct State
{
    File source;
    bool piano = false;                       // un .npno: zonas por tecla con caída (PianoExport)
    std::vector<bk::sfgr::ZoneExtra> extras;
    float attack = 0.002f, release = 0.3f;
    std::unique_ptr<bk::nsmp::SampleSound> sound;
    String error;
    bk::sfgr::ExportResult estimates[3];      // sin recorte, −70 dB, −60 dB
    bk::sfgr::ExportResult result;
    File target;
    double trimDb = -70;
    std::unique_ptr<FileChooser> chooser;
};

constexpr double kTrim[3] = { 0, -70, -60 };

void showResult (std::shared_ptr<State> st);

class ExportJob : public ThreadWithProgressWindow
{
public:
    explicit ExportJob (std::shared_ptr<State> s)
        : ThreadWithProgressWindow (String::fromUTF8 ("Exportando a BeTracks Pro…"), true, false), st_ (std::move (s)) {}
    void run() override
    {
        setStatusMessage (st_->target.getFileName());
        setProgress (-1.0);
        bk::sfgr::ExportOptions o;
        o.trimDb = st_->trimDb;
        o.attackSec = st_->attack;
        o.releaseSec = st_->release;
        o.zoneExtras = st_->extras;
        st_->result = bk::sfgr::write (st_->target.getFullPathName().toStdString(), st_->sound->name.empty()
                                           ? st_->source.getFileNameWithoutExtension().toStdString() : st_->sound->name,
                                       *st_->sound, o);
    }
    void threadComplete (bool) override
    {
        showResult (st_);
        delete this;
    }

private:
    std::shared_ptr<State> st_;
};

void showResult (std::shared_ptr<State> st)
{
    const auto& r = st->result;
    if (! r.ok)
    {
        AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "BeTracks Pro", String::fromUTF8 (r.error.c_str()));
        return;
    }
    AlertWindow::showMessageBoxAsync (MessageBoxIconType::InfoIcon, String::fromUTF8 ("Exportado para BeTracks Pro"),
        st->target.getFileName() + "\n\n" + String (r.zones) + " zonas · " + String (r.samples) + " muestras a 48 kHz\n"
        + "En RAM en BeTracks Pro: " + mb (r.ramBytes)
        + (r.ramBytesBeforeTrim > r.ramBytes ? String::fromUTF8 (" (sin recortar serían ") + mb (r.ramBytesBeforeTrim) + ")" : String()) + "\n\n"
        + (st->piano ? String::fromUTF8 ("Necesita un BeTracks Pro con caída por zona: una versión anterior lo carga, pero las notas "
                                         "no se apagan solas mientras se sostienen.\n\n") : String())
        + String::fromUTF8 ("Cópialo a la carpeta SFGRInstruments de BeTracks Pro (en el iPad: Archivos › En mi iPad › BeTracks Pro › "
                            "SFGRInstruments) y asígnalo a un preset. Es el sonido de tu instrumento o de tu librería: "
                            "úsalo tú, no lo compartas."));
    st->target.getParentDirectory().revealToUser();
}

void chooseTarget (std::shared_ptr<State> st)
{
    const auto folder = File::getSpecialLocation (File::userMusicDirectory).getChildFile ("Betracsec Keys").getChildFile ("BeTracks Pro");
    folder.createDirectory();
    const auto name = File::createLegalFileName (st->sound->name.empty() ? st->source.getFileNameWithoutExtension()
                                                                         : String::fromUTF8 (st->sound->name.c_str()));
    st->chooser = std::make_unique<FileChooser> (String::fromUTF8 ("Guardar para BeTracks Pro"), folder.getChildFile (name + ".sfgr"), "*.sfgr");
    st->chooser->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::warnAboutOverwriting, [st] (const FileChooser& fc)
    {
        auto f = fc.getResult();
        if (f == File()) return;
        if (! f.hasFileExtension ("sfgr")) f = f.withFileExtension ("sfgr");
        st->target = f;
        (new ExportJob (st))->launchThread();
    });
}

void askOptions (std::shared_ptr<State> st)
{
    auto* w = new AlertWindow (String::fromUTF8 ("Exportar a BeTracks Pro (.sfgr)"),
        String::fromUTF8 ("«") + String::fromUTF8 (st->sound->name.c_str()) + String::fromUTF8 ("» · ")
            + String (int (st->sound->zones.size())) + " zonas\n\n"
            + String::fromUTF8 ("BeTracks Pro carga el instrumento entero en RAM. Recortar la cola de cada muestra por debajo del "
                                "golpe más fuerte ahorra memoria y hace la carga más rápida; sobre las pistas de la banda no se oye."),
        MessageBoxIconType::NoIcon);
    StringArray options;
    options.add (String::fromUTF8 ("Recortar colas bajo −70 dB (recomendado) · ") + mb (st->estimates[1].ramBytes));
    options.add (String::fromUTF8 ("Recortar colas bajo −60 dB (más ligero) · ") + mb (st->estimates[2].ramBytes));
    options.add (String::fromUTF8 ("Sin recorte · ") + mb (st->estimates[0].ramBytes));
    w->addComboBox ("trim", options, String::fromUTF8 ("RAM en BeTracks Pro"));
    w->addTextBlock (st->piano
        ? String::fromUTF8 ("Las grabaciones se pasan a 48 kHz. Cada tecla lleva la caída de su nota, el apagador de su tecla y el "
                            "ruido de soltarla; la fuerza suena como en Keys con TOUCH en 2. No van las grabaciones con el pedal "
                            "pisado (String Res) ni el Soft Release.")
        : String::fromUTF8 ("Las muestras se pasan a 48 kHz y cada capa de velocidad se ajusta para que en BeTracks Pro "
                            "suene a su nivel natural (TOUCH en su valor por defecto). Ataque y liberación: los de SAMPLES."));
    w->addButton (String::fromUTF8 ("Continuar…"), 1, KeyPress (KeyPress::returnKey));
    w->addButton ("Cancelar", 0, KeyPress (KeyPress::escapeKey));
    w->enterModalState (true, ModalCallbackFunction::create ([st, w] (int r)
    {
        if (r != 1) return;
        const int idx = w->getComboBoxComponent ("trim")->getSelectedItemIndex();
        st->trimDb = idx == 0 ? -70 : idx == 1 ? -60 : 0;
        chooseTarget (st);
    }), true);
}

class PrepareJob : public ThreadWithProgressWindow
{
public:
    explicit PrepareJob (std::shared_ptr<State> s)
        : ThreadWithProgressWindow (String::fromUTF8 ("Preparando la exportación…"), true, false), st_ (std::move (s)) {}
    void run() override
    {
        setProgress (-1.0);
        setStatusMessage (String::fromUTF8 ("Leyendo ") + st_->source.getFileName());
        std::string err;
        if (st_->piano)
        {
            // Sin las grabaciones con el pedal pisado: BeTracks Pro no elige capa por el pedal (y doblarían la RAM).
            std::atomic<float> progress { 0 };
            std::atomic<bool> cancel { false };
            bk::LoadOptions lo;
            lo.pedalDown = false;
            lo.releases = true;
            std::unique_ptr<bk::PianoSound> piano;
            std::atomic<bool> done { false };
            std::thread t ([&] { piano = bk::loadPiano (st_->source.getFullPathName().toStdString(), err, &progress, &cancel, lo); done = true; });
            while (! done.load())
            {
                setProgress (progress.load());
                if (threadShouldExit()) cancel = true;
                wait (100);
            }
            t.join();
            if (! piano) { st_->error = String::fromUTF8 (err.c_str()); return; }
            setProgress (-1.0);
            setStatusMessage (String::fromUTF8 ("Preparando las zonas…"));
            bk::pianoexport::Converted c;
            if (! bk::pianoexport::convert (*piano, c, err)) { st_->error = String::fromUTF8 (err.c_str()); return; }
            st_->sound = std::make_unique<bk::nsmp::SampleSound> (std::move (c.sound));
            st_->extras = std::move (c.extras);
        }
        else
            st_->sound = st_->source.hasFileExtension ("bkm") ? bk::bkm::read (st_->source, err)
                                                              : bk::nsmp::loadSample (st_->source.getFullPathName().toStdString(), err);
        if (! st_->sound) { st_->error = String::fromUTF8 (err.c_str()); return; }
        for (int i = 0; i < 3 && ! threadShouldExit(); ++i)
        {
            setStatusMessage (String::fromUTF8 ("Calculando la RAM…"));
            bk::sfgr::ExportOptions o;
            o.trimDb = kTrim[i];
            st_->estimates[i] = bk::sfgr::estimate (*st_->sound, o);
        }
    }
    void threadComplete (bool cancelled) override
    {
        if (! cancelled)
        {
            if (st_->sound == nullptr) AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "BeTracks Pro", st_->error);
            else askOptions (st_);
        }
        delete this;
    }

private:
    std::shared_ptr<State> st_;
};
} // namespace

void exportToBeTracks (const File& source, float attackSec, float releaseSec)
{
    auto st = std::make_shared<State>();
    st->source = source;
    st->attack = attackSec;
    st->release = releaseSec;
    (new PrepareJob (st))->launchThread();
}

void exportPianoToBeTracks (const File& npno)
{
    auto st = std::make_shared<State>();
    st->source = npno;
    st->piano = true;
    (new PrepareJob (st))->launchThread();
}
} // namespace bk::sfgrui
