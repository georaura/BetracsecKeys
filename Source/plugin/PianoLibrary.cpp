#include "PianoLibrary.h"
#include "../core/Npno.h"
#include "../core/OrganPreset.h"
#include "../core/PianoPreset.h"
#include "../core/NordSample.h"
#include "../core/SynthEngine.h"
#include "PluginProcessor.h"
#include "KeysSampleFile.h"

static juce::PropertiesFile::Options settingsOptions()
{
    juce::PropertiesFile::Options o;
    o.applicationName = "Library";
    o.folderName = "Betracsec Keys"; // carpeta de datos: conserva el nombre de antes (BeTracks Keys se llamó así) para no perder lo guardado
    o.filenameSuffix = "settings";
    o.osxLibrarySubFolder = "Application Support";
    o.processLock = nullptr;
    o.millisecondsBeforeSaving = -1; // se guarda al momento en writeSetting
    return o;
}


PianoLibrary::PianoLibrary() : juce::Thread ("BeTracks Keys library")
{
    // Primera vez: las carpetas donde suele acabar la Nord Piano Library, si existen.
    if (folders().isEmpty() && ! isManagedLibrary())
    {
        const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
        for (auto rel : { "Documents/Nord Piano Library", "Music/Nord Piano Library", "Downloads/Nord Piano Library",
                          "Documents/Nord Sound Manager Files", "Music/Nord" })
            if (auto f = home.getChildFile (rel); f.isDirectory()) addFolder (f);
    }
    rescan();
}

PianoLibrary::~PianoLibrary() { stopThread (4000); }

static std::unique_ptr<juce::PropertiesFile> makeSettings()
{
    return std::make_unique<juce::PropertiesFile> (settingsOptions());
}

// Los ajustes se comparten entre la app y los plugins (otros procesos): cada lectura vuelve a leer
// el archivo, y cada escritura lee, cambia y guarda en un solo paso (si se recargara entre el cambio
// y el guardado, el cambio se perdería).
juce::String PianoLibrary::readSetting (const juce::String& key) const
{
    const juce::ScopedLock l (settingsLock_);
    if (! settings_) settings_ = makeSettings();
    settings_->reload();
    return settings_->getValue (key);
}

void PianoLibrary::writeSetting (const juce::String& key, const juce::String& value)
{
    const juce::ScopedLock l (settingsLock_);
    if (! settings_) settings_ = makeSettings();
    settings_->reload();
    settings_->setValue (key, value);
    settings_->save();
}

juce::StringArray PianoLibrary::folders() const
{
    juce::StringArray a;
    a.addLines (readSetting ("folders"));
    a.removeEmptyStrings();
    return a;
}

void PianoLibrary::addFolder (const juce::File& folder)
{
    auto a = folders();
    a.addIfNotAlreadyThere (folder.getFullPathName());
    writeSetting ("folders", a.joinIntoString ("\n"));
    rescan();
}

void PianoLibrary::removeFolder (const juce::String& path)
{
    auto a = folders();
    a.removeString (path);
    writeSetting ("folders", a.joinIntoString ("\n"));
    rescan();
}

void PianoLibrary::noteRecent (const juce::File& file)
{
    if (file != juce::File()) writeSetting ("recent", file.getFullPathName());
}

juce::File PianoLibrary::recent() const
{
    return juce::File (readSetting ("recent"));
}

void PianoLibrary::rescan()
{
    stopThread (4000);
    startThread (juce::Thread::Priority::low);
}

std::vector<PianoLibrary::Entry> PianoLibrary::entries() const
{
    const juce::ScopedLock l (lock_);
    return entries_;
}

std::vector<PianoLibrary::Entry> PianoLibrary::organEntries() const
{
    const juce::ScopedLock l (lock_);
    return organEntries_;
}

std::vector<PianoLibrary::Entry> PianoLibrary::pianoPresetEntries() const
{
    const juce::ScopedLock l (lock_);
    return pianoPresetEntries_;
}

std::vector<PianoLibrary::Entry> PianoLibrary::sampleEntries() const
{
    const juce::ScopedLock l (lock_);
    return sampleEntries_;
}

std::vector<PianoLibrary::Entry> PianoLibrary::synthEntries() const
{
    const juce::ScopedLock l (lock_);
    return synthEntries_;
}

juce::File PianoLibrary::findByFileName (const juce::String& fileName) const
{
    const juce::ScopedLock l (lock_);
    for (const auto& e : entries_)
        if (e.file.getFileName().equalsIgnoreCase (fileName)) return e.file;
    return {};
}

std::vector<PianoLibrary::Entry> PianoLibrary::fmEntries() const
{
    const juce::ScopedLock l (lock_);
    return fmEntries_;
}

std::optional<PianoLibrary::Entry> PianoLibrary::identify (const juce::File& file)
{
    if (file.hasFileExtension ("bkm"))
    {
        if (! bk::bkm::looksLikeBkm (file)) return std::nullopt;
        Entry e;
        e.file = file;
        e.sample = true;
        e.name = file.getFileNameWithoutExtension();
        e.variant = "Keys";
        e.group = file.getParentDirectory().getFileName();
        e.bytes = file.getSize();
        return e;
    }
    if (file.hasFileExtension ("x7l;x7u;x8l;x8u"))
    {
        // Solo las performances (unos cientos de KB), no las muestras.
        std::string err;
        const auto lib = bk::ysfc::readLibrary (file.getFullPathName().toStdString(), err);
        if (! lib) return std::nullopt;
        Entry e;
        e.file = file;
        e.fm = true;
        e.name = file.getFileNameWithoutExtension();
        e.group = file.getFileExtension().substring (1).toUpperCase();
        e.bytes = file.getSize();
        for (int i : KeysProcessor::playableFm (*lib))
            e.performances.push_back ({ i, juce::String::fromUTF8 (lib->performances[size_t (i)].name.c_str()) });
        if (e.performances.empty()) return std::nullopt;
        return e;
    }
    if (file.hasFileExtension ("nsmp3;nsmp4"))
    {
        // Solo la cabecera: listar no decodifica (los hay de 15 MB). Nombre del archivo sin versión.
        juce::FileInputStream in (file);
        uint8_t head[0x40] = {};
        if (! in.openedOk() || in.read (head, sizeof head) < 0x30 || ! bk::nsmp::looksLikeNsmp (head, sizeof head)) return std::nullopt;
        Entry e;
        e.file = file;
        e.sample = true;
        auto name = file.getFileNameWithoutExtension();
        const int sp = name.lastIndexOfChar (' ');
        if (sp > 0 && name.substring (sp + 1).containsOnly ("0123456789.")) name = name.substring (0, sp);
        e.name = name.trim();
        e.group = file.getParentDirectory().getFileName().replaceCharacter ('_', ' ');
        e.bytes = file.getSize();
        return e;
    }
    if (file.hasFileExtension ("ns4y"))
    {
        juce::MemoryBlock data;
        if (file.getSize() > 8192 || ! file.loadFileAsData (data)) return std::nullopt;
        std::string err;
        const auto p = bk::synth::parseNs4y (static_cast<const uint8_t*> (data.getData()), data.getSize(), err);
        if (! p) return std::nullopt;
        int L = 0;
        while (L < 2 && ! p->layers[size_t (L)].on()) ++L;
        static const char* sources[] = { "Analog", "FM-H", "FM-I", "Wave", "Sample" };
        const auto sp = bk::synth::paramsFromLayer (p->layers[size_t (L)]);
        Entry e;
        e.file = file;
        e.synth = true;
        e.name = file.getFileNameWithoutExtension();
        e.variant = sources[int (sp.source)];
        e.group = file.getParentDirectory().getFileName();
        e.bytes = file.getSize();
        return e;
    }
    if (file.hasFileExtension ("ns4n"))
    {
        juce::MemoryBlock data;
        if (file.getSize() > 8192 || ! file.loadFileAsData (data)) return std::nullopt;
        std::string err;
        const auto p = bk::npno::parseNs4n (static_cast<const uint8_t*> (data.getData()), data.getSize(), err);
        if (! p) return std::nullopt;
        const auto& l = p->layers[0].on ? p->layers[0] : p->layers[1];
        Entry e;
        e.file = file;
        e.pianoPreset = true;
        e.name = file.getFileNameWithoutExtension();
        e.variant = juce::String::fromUTF8 (l.modelName.c_str());
        e.group = bk::npno::pianoLayerTypeName (l.pianoType);
        e.bytes = file.getSize();
        return e;
    }
    if (file.hasFileExtension ("ns4o"))
    {
        // Un preset de órgano pesa unos 180 bytes: se lee entero.
        juce::MemoryBlock data;
        if (file.getSize() > 4096 || ! file.loadFileAsData (data)) return std::nullopt;
        std::string err;
        const auto p = bk::organ::parseNs4o (static_cast<const uint8_t*> (data.getData()), data.getSize(), err);
        if (! p) return std::nullopt;
        const auto& o = p->layerOn[0] ? p->layers[0] : p->layers[1];
        Entry e;
        e.file = file;
        e.organ = true;
        e.name = file.getFileNameWithoutExtension();
        e.group = bk::organ::modelName (o.model);
        if (o.model == bk::organ::Model::Pipe1 || o.model == bk::organ::Model::Pipe2) e.group = "Pipe";
        e.bytes = file.getSize();
        return e;
    }
    juce::FileInputStream in (file);
    if (! in.openedOk()) return std::nullopt;
    juce::HeapBlock<uint8_t> head (0x100, true);
    const auto got = in.read (head.get(), 0x100);
    if (got <= 0) return std::nullopt;
    const auto id = bk::npno::readIdentity (head.get(), size_t (got));
    if (! id) return std::nullopt;
    Entry e;
    e.file = file;
    e.name = juce::String (id->name);
    if (e.name.isEmpty()) e.name = file.getFileNameWithoutExtension();
    e.variant = juce::String (id->variant);
    e.version = juce::String (id->version);
    e.group = bk::npno::pianoTypeName (id->pianoType);
    e.bytes = file.getSize();
    return e;
}

void PianoLibrary::run()
{

    std::vector<Entry> found, organs, pianoPresets, samples, synths, fms;
    for (const auto& path : folders())
    {
        const juce::File dir (path);
        if (! dir.isDirectory()) continue;
        for (const auto& f : juce::RangedDirectoryIterator (dir, true, kPatterns, juce::File::findFiles))
        {
            if (threadShouldExit()) return;
            if (auto e = identify (f.getFile())) (e->organ ? organs : e->pianoPreset ? pianoPresets : e->sample ? samples : e->synth ? synths : e->fm ? fms : found).push_back (*e);
        }
    }
    auto byGroup = [] (const Entry& a, const Entry& b)
    {
        return a.group != b.group ? a.group < b.group : a.label().compareNatural (b.label()) < 0;
    };
    std::sort (found.begin(), found.end(), byGroup);
    std::sort (organs.begin(), organs.end(), byGroup);
    std::sort (pianoPresets.begin(), pianoPresets.end(), byGroup);
    std::sort (samples.begin(), samples.end(), byGroup);
    std::sort (synths.begin(), synths.end(), [] (const Entry& a, const Entry& b)
    {
        return a.group != b.group ? a.group.compareNatural (b.group) < 0 : a.name.compareNatural (b.name) < 0;
    });
    {
        const juce::ScopedLock l (lock_);
        entries_ = std::move (found);
        organEntries_ = std::move (organs);
        pianoPresetEntries_ = std::move (pianoPresets);
        sampleEntries_ = std::move (samples);
        synthEntries_ = std::move (synths);
        fmEntries_ = std::move (fms);
    }
    sendChangeMessage();
}

void PianoLibrary::ensureFolder (const juce::File& folder)
{
    if (! folder.isDirectory()) return;
    for (const auto& path : folders())
    {
        const juce::File f (path);
        if (folder == f || folder.isAChildOf (f)) return;
    }
    addFolder (folder);
}

