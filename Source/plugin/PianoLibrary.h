// BeTracks Keys — biblioteca de pianos.
//  Escritorio: carpetas del usuario y los archivos que contienen. Las carpetas se guardan en la carpeta de
//  datos de la aplicación (BeTracks Keys) y las comparten la app y los plugins.
#pragma once


#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_data_structures/juce_data_structures.h>

class PianoLibrary : public juce::ChangeBroadcaster, private juce::Thread
{
public:
    struct Entry
    {
        juce::File file;
        juce::String name, variant, version, group;
        juce::int64 bytes = 0;
        bool organ = false;      ///< preset de órgano (.ns4o) en vez de piano
        bool pianoPreset = false; ///< preset de piano (.ns4n)
        bool sample = false;      ///< Nord Sample (.nsmp3/.nsmp4) o librería muestreada con Keys (.bkm)
        bool synth = false;       ///< preset de synth (.ns4y)
        bool fm = false;          ///< librería de MONTAGE/MODX (.X7L/.X8L): sus performances con FM-X
        std::vector<std::pair<int, juce::String>> performances; ///< índice en la librería y nombre
        juce::String label() const { return variant.isEmpty() ? name : name + " (" + variant + ")"; }
    };

    PianoLibrary();
    ~PianoLibrary() override;

    juce::StringArray folders() const;
    void addFolder (const juce::File& folder);
    void removeFolder (const juce::String& path);
    /** Añade la carpeta si ni ella ni ninguna que la contenga está ya en la biblioteca. */
    void ensureFolder (const juce::File& folder);
    void rescan();

    /** En escritorio la biblioteca son las carpetas que elige el usuario. */
    static constexpr bool isManagedLibrary() { return false; }

    bool isScanning() const { return isThreadRunning(); }
    /** Pianos (.npno). */
    std::vector<Entry> entries() const;
    /** Presets de órgano del Stage 4 (.ns4o). */
    std::vector<Entry> organEntries() const;
    /** Presets de piano del Stage 4 (.ns4n). */
    std::vector<Entry> pianoPresetEntries() const;
    /** Nord Samples (.nsmp3/.nsmp4). */
    std::vector<Entry> sampleEntries() const;
    /** Presets de synth del Stage 4 (.ns4y). */
    std::vector<Entry> synthEntries() const;
    /** Librerías de MONTAGE/MODX con performances FM-X. */
    std::vector<Entry> fmEntries() const;
    /** Extensiones que entiende la biblioteca, para los diálogos y el escaneo. */
    static constexpr const char* kPatterns = "*.npno;*.ns4o;*.ns4n;*.ns4y;*.nsmp3;*.nsmp4;*.bkm;*.x7l;*.x7u;*.x8l;*.x8u";
    juce::File findByFileName (const juce::String& fileName) const;
    /** Recuerda el último piano abierto (para el Standalone). */
    void noteRecent (const juce::File& file);
    juce::File recent() const;

    /** Una pasada sobre un archivo: identidad o nada si no es un piano. */
    static std::optional<Entry> identify (const juce::File& file);

private:
    void run() override;
    juce::String readSetting (const juce::String& key) const;
    void writeSetting (const juce::String& key, const juce::String& value);

    mutable juce::CriticalSection settingsLock_;
    mutable std::unique_ptr<juce::PropertiesFile> settings_;
    mutable juce::CriticalSection lock_;
    std::vector<Entry> entries_, organEntries_, pianoPresetEntries_, sampleEntries_, synthEntries_, fmEntries_;
};
