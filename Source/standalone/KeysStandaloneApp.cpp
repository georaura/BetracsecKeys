// BeTracks Keys — la app Standalone. Igual que la de JUCE, pero abre todas las entradas MIDI al
// arrancar (quien conecta un teclado espera que suene sin tocar ajustes) y deja sitio para el
// comprobador de actualizaciones.
#include <juce_core/system/juce_TargetPlatform.h>
#include <juce_audio_plugin_client/detail/juce_CheckSettingMacros.h>
#include <juce_audio_plugin_client/detail/juce_IncludeSystemHeaders.h>
#include <juce_audio_plugin_client/detail/juce_IncludeModuleHeaders.h>
#include <juce_audio_plugin_client/detail/juce_PluginUtilities.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

#include "../plugin/Text.h"
#include "../plugin/PluginProcessor.h"
#include "Updater.h"

namespace
{
class KeysStandaloneApp final : public juce::JUCEApplication
{
public:
    KeysStandaloneApp()
    {
        juce::PropertiesFile::Options options;
        options.applicationName = juce::CharPointer_UTF8 (JucePlugin_Name);
        options.filenameSuffix = ".settings";
        options.osxLibrarySubFolder = "Application Support";
        options.folderName = "";
        // La app se llamó «Betracsec Keys»: sus ajustes (interfaz de audio, entradas MIDI, estado) estaban en
        // «Betracsec Keys.settings». Si aún no hay ajustes con el nombre nuevo, se copian; si no, la app arranca
        // con unos nuevos y puede quedarse sin audio (pasó el 2026-11-09: sin hilo de audio, ni sonido ni MIDI).
        {
            const auto now = options.getDefaultFile();
            const auto before = now.getSiblingFile ("Betracsec Keys" + options.filenameSuffix);
            if (! now.existsAsFile() && before.existsAsFile()) before.copyFileTo (now);
        }
        appProperties_.setStorageParameters (options);
    }

    const juce::String getApplicationName() override { return juce::CharPointer_UTF8 (JucePlugin_Name); }
    const juce::String getApplicationVersion() override { return JucePlugin_VersionString; }
    bool moreThanOneInstanceAllowed() override { return false; }
    void anotherInstanceStarted (const juce::String&) override {}

    void initialise (const juce::String&) override
    {
        auto holder = std::make_unique<juce::StandalonePluginHolder> (appProperties_.getUserSettings(), false, juce::String {},
                                                                      nullptr, juce::Array<juce::StandalonePluginHolder::PluginInOuts> {},
                                                                      /* autoOpenMidiDevices */ true);
        window_ = std::make_unique<juce::StandaloneFilterWindow> (getApplicationName(), juce::Colour (0xff0b0b0e), std::move (holder));
        window_->setVisible (true);
        // Si la interfaz guardada no se pudo abrir, la salida por defecto: sin dispositivo de audio no corre el
        // procesado, y entonces tampoco llega el MIDI. Mejor sonar por la salida del sistema que quedarse mudo.
        if (auto& dm = window_->pluginHolder->deviceManager; dm.getCurrentAudioDevice() == nullptr)
        {
            juce::AudioDeviceManager::AudioDeviceSetup setup;
            dm.getAudioDeviceSetup (setup);
            setup.outputDeviceName = {};
            setup.inputDeviceName = {};
            setup.bufferSize = 0;
            setup.useDefaultOutputChannels = setup.useDefaultInputChannels = true;
            if (dm.setAudioDeviceSetup (setup, true).isNotEmpty())
                dm.initialiseWithDefaultDevices (0, 2);
        }
        bk::startUpdater();
    }

    // Al volver a la app puede haber pianos nuevos en las carpetas de la biblioteca.
    void resumed() override
    {
        if (window_ != nullptr)
            if (auto* keys = dynamic_cast<KeysProcessor*> (window_->pluginHolder->processor.get()))
                keys->library.rescan();
    }

    void shutdown() override
    {
        window_ = nullptr;
        appProperties_.saveIfNeeded();
    }

    void systemRequestedQuit() override
    {
        if (window_ != nullptr) window_->pluginHolder->savePluginState();
        if (juce::ModalComponentManager::getInstance()->cancelAllModalComponents())
            juce::Timer::callAfterDelay (100, [] { if (auto app = JUCEApplicationBase::getInstance()) app->systemRequestedQuit(); });
        else
            quit();
    }

private:
    juce::ApplicationProperties appProperties_;
    std::unique_ptr<juce::StandaloneFilterWindow> window_;
};
} // namespace

juce::JUCEApplicationBase* juce_CreateApplication();
juce::JUCEApplicationBase* juce_CreateApplication() { return new KeysStandaloneApp(); }

