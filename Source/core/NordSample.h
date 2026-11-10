// BeTracks Keys — Nord Samples (.nsmp3 / .nsmp4).
//
// Port a C++ del lector de OpenNord (src/lib/ns4/nsmp.ts y nsmp-codec.ts, AGPL-3.0; commit en
// vendor/opennord.COMMIT): árbol de secciones (NSMP → hdr → cat → map → N×stk …), tabla de zonas del
// `map` y el códec NW1 (predicción lineal fija por bloques). Solo las generaciones 3 y 4; las
// antiguas (.nsmp v8, Library 2.0) no se leen todavía.
//
// Cada golpe (`stk`) guarda su propia frecuencia de reproducción (≈ 35.002 Hz, con la afinación fina
// de la zona). Los samples de fábrica son contenido de Clavia: solo se decodifican en el equipo del usuario.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace bk::nsmp
{
struct Zone
{
    int root = 60, low = 0, high = 127;   ///< tecla raíz y rango de teclas
    int velLow = 0, velHigh = 127;        ///< rango de velocidad (capa)
    uint32_t strokeId = 0;                ///< el golpe que suena (id global del `stk`)
    bool oneShot = false;
};

struct Stroke
{
    uint32_t id = 0;
    int channels = 1;
    double sampleRate = 35002;            ///< frecuencia a la que suena en su tecla raíz
    std::vector<int32_t> pcm;             ///< entero, canales intercalados
    uint32_t frames = 0;
    bool loops = false;
    uint32_t loopStart = 0, loopEnd = 0;  ///< tramas
    double loopDecayDbPerSecond = 0;      ///< 0 = el bucle mantiene el nivel
};

struct Sample
{
    std::string name;
    int codec = 0;                        ///< 3 o 4
    std::vector<Zone> zones;
    std::vector<Stroke> strokes;
};

/** true si parece un Nord Sample de generación 3 o 4. */
bool looksLikeNsmp (const uint8_t* data, size_t size);
/** Solo nombre y generación (barato: no decodifica audio). */
std::optional<Sample> readInfo (const uint8_t* data, size_t size, std::string& error);
/** Todo: zonas y golpes decodificados. */
std::optional<Sample> decode (const uint8_t* data, size_t size, std::string& error);
} // namespace bk::nsmp
