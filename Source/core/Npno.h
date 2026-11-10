// BeTracks Keys — lectura de pianos Nord (.npno / CNSP).
//
// Port a C++ de OpenNord (github.com/simonflore/opennord, AGPL-3.0): src/lib/ns4/nsp.ts y
// nsp-audio.ts, commit fijado en vendor/opennord.COMMIT. Sin dependencias de JUCE, para que las
// pruebas (tests/decoder_test.cpp) lo compilen solo.
//
// Los pianos de fábrica son audio de Clavia: se decodifican solo en el Mac del usuario, nunca se
// incluyen ni se redistribuyen.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace bk::npno
{
/** Frecuencia de las grabaciones. */
constexpr int kSampleRate = 44100;
/** Actualizaciones de envolvente por segundo en el instrumento (decaimientos por paso). */
constexpr int kEnvelopeRate = 22050;
/** La grabación suena 4 semitonos por encima de la raíz guardada en el mapa de teclas. */
constexpr int kRootOffset = 4;
/** Las últimas 64 tramas de un bloque repiten las primeras del siguiente. */
constexpr int kBlockOverlap = 64;

struct Zone
{
    int root = 0;  ///< raíz guardada (la grabación suena en root + kRootOffset)
    int low = 0;   ///< tecla MIDI más grave de la zona
    int high = 0;  ///< tecla MIDI más aguda
};

struct Stroke
{
    int index = 0;
    int zone = 0;
    int floor = 0;          ///< suelo de atenuación en pasos de 0,2 dB (0 = el golpe más fuerte)
    int flags = 0;          ///< 0 normal, 1 lista alternativa, 2 sonido de soltar tecla
    uint32_t frames = 0;
    size_t offset = 0;      ///< byte del primer bloque en el archivo
    int blocks = 0;
    std::array<std::array<int32_t, 4>, 2> history {}; ///< por canal, el más reciente primero
    bool hasLoop = false;
    uint32_t loopStart = 0, loopLength = 0;
    double loopDecay = 1.0;  ///< factor por actualización (22.050/s) dentro del bucle
    std::array<double, 14> preLoop {}; ///< factor por actualización antes del bucle, por semitono bajo raíz+1
    int damper = 0;          ///< índice de apagador (+0x72): T60 ≈ 1,5 ms · 10^(i/20)
    int damperSoft = 0;      ///< con Soft Release (+0x73)
    int partner = 0;         ///< +0x33: suelo de la capa pareja en la otra lista (pedal arriba/abajo); en los de soltar tecla, su atenuación al sonar
    int level = 0;           ///< +0x34 (con signo)
};

struct Piano
{
    std::string name, variant, version;
    int versionRaw = 0;
    int pianoType = 0;
    int channels = 0;
    int blockWords = 0;
    int dirVersion = 0;
    int undampedFrom = 109;               ///< primera tecla sin apagador (+0x40D)
    std::array<uint8_t, 128> damperByKey {}; ///< índice de apagador por tecla (+0x20C)
    std::vector<Zone> zones;              ///< de grave a agudo
    std::vector<Stroke> strokes;

    /** Zona que toca la tecla, o -1. */
    int zoneForKey (int midi) const;
    /** Golpe (capa) que suena en la zona con esa velocidad, como lo elige el instrumento; -1 si no hay.
     *  `list`: 0 normales, 1 con el pedal pisado, 2 sonidos de soltar tecla. */
    int strokeFor (int zone, int velocity, int selectOffset = 0, int list = 0) const;
    /** Igual, a partir de una atenuación en pasos de 0,2 dB. */
    int strokeForAttenuation (int zone, double attenuation, int list = 0) const;
    /** Cuántos golpes hay de cada lista (0, 1, 2). */
    std::array<int, 3> listCounts() const;
};

/** Velocidad (1–127) → atenuación en pasos de 0,2 dB (ley 40·log10(v/127), como OpenNord). */
double touchAttenuation (int velocity);
/** Ganancia lineal de una atenuación en pasos de 0,2 dB. */
double attenuationGain (double attenuation);
/** Segundos de T60 de un índice de apagador. */
double damperT60 (int index);

/** true si los bytes empiezan como un .npno (CBIN + etiqueta npno). */
bool looksLikeNpno (const uint8_t* data, size_t size);

/** Lo que dice la cabecera, sin leer el audio: basta con los primeros 0x100 bytes. */
struct Identity
{
    std::string name, variant, version;
    int pianoType = 0;
};
std::optional<Identity> readIdentity (const uint8_t* data, size_t size);

/** Nombre del grupo de un tipo de piano ("Grand", "Electric"…), en inglés como en Nord. */
const char* pianoTypeName (int type);

/** Lee cabecera, mapa de teclas y directorio. std::nullopt con `error` si el archivo no sirve. */
std::optional<Piano> parse (const uint8_t* data, size_t size, std::string& error);

/** Tramas que guarda un bloque con ese ancho de residuo (sin el solape). */
int framesPerBlock (int blockWords, int width, int channels);

/**
 * Decodifica un golpe a PCM entero, canales intercalados (`out` con frames * channels).
 * false con `error` si el golpe está dañado.
 */
bool decodeStroke (const uint8_t* data, size_t size, const Piano& piano, const Stroke& stroke,
                   int32_t* out, std::string& error);
} // namespace bk::npno
