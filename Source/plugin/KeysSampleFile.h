// BeTracks Keys — librerías de muestras propias (.bkm), las que crea el muestreador a partir de un instrumento
// del usuario (p. ej. un Yamaha Motif Rack XS).
//
// Formato: "BKMUESTR", versión (u32), longitud (u32) + índice JSON (nombre, origen, zonas, bucles) y, por cada
// golpe, longitud (u32) + FLAC de 16 bits. Todo en little-endian. Se decodifica entero a memoria al abrirlo:
// sin lectura de disco mientras se toca. El sonido grabado es del instrumento del usuario: solo para su uso.
#pragma once

#include "../core/SampleEngine.h"

#include <juce_audio_formats/juce_audio_formats.h>

namespace bk::bkm
{
/** Un golpe grabado: audio (1 o 2 canales) y la zona que cubre. */
struct Stroke
{
    juce::AudioBuffer<float> audio;
    int root = 60, low = 0, high = 127, velLow = 1, velHigh = 127;
    int velocity = 100;               ///< velocidad con la que se grabó
    bool loops = false;
    int loopStart = 0, loopEnd = 0;   ///< en muestras; el bucle es [loopStart, loopEnd) y el audio sigue unas muestras más
};

/** Escribe la librería. Normaliza todos los golpes juntos (respeta las diferencias entre capas de velocidad). */
bool write (const juce::File& file, const juce::String& name, const juce::String& source, double sampleRate,
            const std::vector<Stroke>& strokes, juce::String& error);

bool looksLikeBkm (const juce::File& file);
/** Lee y decodifica una librería para la sección SAMPLES. */
std::unique_ptr<bk::nsmp::SampleSound> read (const juce::File& file, std::string& error);
} // namespace bk::bkm
