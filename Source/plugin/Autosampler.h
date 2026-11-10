// BeTracks Keys — muestreador automático (solo la app de Mac): toca un instrumento externo por MIDI (p. ej.
// un Yamaha Motif Rack XS por USB), graba su salida por una interfaz de audio (p. ej. PreSonus Quantum HD 8),
// recorta cada toma, busca bucles en los sonidos sostenidos y guarda una librería propia (.bkm) que se toca en
// la sección SAMPLES, cargada entera en memoria. El sonido grabado es del instrumento del usuario: solo para su
// propio uso, no para compartirlo.
#pragma once

#include "PluginProcessor.h"

#include <juce_audio_utils/juce_audio_utils.h>

class AutosamplerWindow : public juce::DocumentWindow
{
public:
    /** `onSaved` recibe la librería guardada (en el hilo de mensajes). */
    AutosamplerWindow (std::function<void (const juce::File&)> onSaved);
    ~AutosamplerWindow() override;
    void closeButtonPressed() override;

    static bool isAvailable();
};
