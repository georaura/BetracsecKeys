cmake_minimum_required(VERSION 3.22)

# BeTracks Keys — instrumento que toca pianos .npno.
#   Mac:     AU, VST3 y app Standalone     (build_app.sh, build_public.sh)
#   Windows: VST3 y app Standalone          (.github/workflows/windows.yml)
# Licencia AGPL-3.0 (incluye código portado de OpenNord y usa JUCE bajo AGPLv3).

file(STRINGS VERSION BK_VERSION LIMIT_COUNT 1)
if(APPLE)
    project(BetracsecKeys VERSION ${BK_VERSION} LANGUAGES C CXX OBJC OBJCXX)
else()
    # Windows (y Linux): sin Objective-C. VST3 y la app Standalone.
    project(BetracsecKeys VERSION ${BK_VERSION} LANGUAGES C CXX)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(BK_IOS FALSE)
if(NOT BK_IOS)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "macOS mínimo")
endif()

add_subdirectory(vendor/JUCE)

set(BK_ICON "")
if(EXISTS "${CMAKE_SOURCE_DIR}/assets/icon-1024.png")
    set(BK_ICON ICON_BIG "${CMAKE_SOURCE_DIR}/assets/icon-1024.png")
endif()

if(BK_IOS)
elseif(NOT APPLE)
    set(BK_PLATFORM
        FORMATS VST3 Standalone
        VST3_CATEGORIES Instrument Piano)
else()
    set(BK_PLATFORM
        FORMATS AU VST3 Standalone
        AU_MAIN_TYPE kAudioUnitType_MusicDevice
        VST3_CATEGORIES Instrument Piano
        HARDENED_RUNTIME_ENABLED TRUE
        HARDENED_RUNTIME_OPTIONS com.apple.security.device.audio-input
        # El muestreador de la app graba la salida de un instrumento externo por la interfaz de audio.
        MICROPHONE_PERMISSION_ENABLED TRUE
        MICROPHONE_PERMISSION_TEXT "Para muestrear tus instrumentos (p. ej. un Motif) por las entradas de tu interfaz de audio.")
endif()

juce_add_plugin(BetracsecKeys
    VERSION ${BK_VERSION}
    PRODUCT_NAME "BeTracks Keys"
    COMPANY_NAME "BeTracks"
    COMPANY_WEBSITE "https://betracksec.com/keys"
    COMPANY_COPYRIGHT "© BeTracks. AGPL-3.0"
    BUNDLE_ID com.betracsec.keys
    ${BK_ICON}
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    # Códigos fijos: cambiarlos rompe los proyectos de DAW que usen el plugin.
    PLUGIN_MANUFACTURER_CODE Btrc
    PLUGIN_CODE Bkey
    ${BK_PLATFORM}
    COPY_PLUGIN_AFTER_BUILD FALSE
    NEEDS_CURL FALSE
    NEEDS_WEB_BROWSER FALSE)

target_sources(BetracsecKeys PRIVATE
    Source/core/Npno.cpp
    Source/core/PianoSound.cpp
    Source/core/PianoEngine.cpp
    Source/core/OrganPreset.cpp
    Source/core/OrganEngine.cpp
    Source/core/PianoPreset.cpp
    Source/core/SampleEngine.cpp
    Source/core/NordSample.cpp
    Source/core/SynthPreset.cpp
    Source/core/SynthEngine.cpp
    Source/core/Ysfc.cpp
    Source/core/FmxEngine.cpp
    Source/core/AwmEngine.cpp
    Source/core/SfgrWriter.cpp
    Source/core/PianoExport.cpp
    Source/plugin/PianoLibrary.cpp
    Source/plugin/PluginProcessor.cpp
    Source/plugin/PluginEditor.cpp
    Source/plugin/OrganPanel.cpp
    Source/plugin/SynthPanel.cpp
    Source/plugin/FmPanel.cpp
    Source/plugin/KeysSampleFile.cpp
    Source/plugin/Autosampler.cpp
    Source/plugin/SfgrExport.cpp
    Source/plugin/SelfSampleExport.cpp
    Source/plugin/SelfSampler.cpp
    Source/plugin/AboutWindow.cpp)

target_compile_definitions(BetracsecKeys PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1)

# Logo de la cabecera (assets/brand/header-logo.png, sacado del logo de la marca).
juce_add_binary_data(BetracsecKeysAssets SOURCES assets/brand/header-logo.png LICENSE)
set_target_properties(BetracsecKeysAssets PROPERTIES POSITION_INDEPENDENT_CODE TRUE)

target_link_libraries(BetracsecKeys
    PRIVATE
        BetracsecKeysAssets
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# App Standalone propia (abre las entradas MIDI y, en Mac, lleva el actualizador). Solo en la app:
# los plugins no incluyen Sparkle.
target_compile_definitions(BetracsecKeys PUBLIC JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1)
if(APPLE)
    target_sources(BetracsecKeys_Standalone PRIVATE
        Source/standalone/KeysStandaloneApp.cpp
        Source/standalone/Updater.mm)
else()
    target_sources(BetracsecKeys_Standalone PRIVATE
        Source/standalone/KeysStandaloneApp.cpp
        Source/standalone/UpdaterStub.cpp)
endif()
if(MSVC)
    # Fuentes en UTF-8 (los textos llevan tildes y «») y M_PI de <cmath>.
    target_compile_options(BetracsecKeys PRIVATE /utf-8 /bigobj)
    target_compile_definitions(BetracsecKeys PRIVATE _USE_MATH_DEFINES NOMINMAX)
endif()

option(BK_SPARKLE "Incluir Sparkle (versión pública de Mac)" OFF)
if(BK_SPARKLE AND NOT BK_IOS)
    target_compile_definitions(BetracsecKeys_Standalone PRIVATE BK_SPARKLE=1)
    target_compile_options(BetracsecKeys_Standalone PRIVATE -F${CMAKE_SOURCE_DIR}/vendor/Sparkle)
    target_link_options(BetracsecKeys_Standalone PRIVATE -F${CMAKE_SOURCE_DIR}/vendor/Sparkle -framework Sparkle
                        -Wl,-rpath,@executable_path/../Frameworks)
endif()
