// BeTracks Keys — librerías YSFC de Yamaha MONTAGE / MODX (.X7L, .X7U, .X8L, .X8U).
//
// Contenedor: cabecera "YAMAHA-YSFC" + versión, un índice de bloques (EPFM/DPFM = performances,
// EWFM/DWFM = ondas, EWIM/DWIM = sus muestras…) y, en DPFM, una performance por elemento "Data".
// Estructura de la performance y de las partes AWM2: ConvertWithMoss (Jürgen Moßgraber, LGPL-3.0;
// YamahaYsfcPerformance/PerformancePart/PartElement) y YSFC Forge (MIT/LGPL-3.0).
//
// Los bloques FM-X (un bloque común de 67 bytes y ocho de operador de 51) no están documentados en
// ninguno de los dos. El mapa de aquí es de BeTracks Keys, comprobado contra la "DX7 Selection" de la
// librería MONTAGE Expanded (voces del DX7 importadas): los operadores van en orden inverso (el bloque 0
// es el OP8, como en el volcado del DX7, que también empieza por el OP6), y los campos casan con los del
// DX7 (detune centrado en 15, sensibilidad a la velocidad centrada en 7, punto de ruptura y curvas del
// DX7). El número de algoritmo es el del FS1R (las 88 conexiones de FM-X son las del FS1R).
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace bk::ysfc
{
/** Un operador FM-X, en valores crudos del archivo. `slot` 0..7 es el orden del archivo (= OP8..OP1). */
struct FmOp
{
    bool keyOnReset = true;
    bool fixed = false;
    int coarse = 1, fine = 0;          ///< razón: coarse 0 = ×0,5; fijo: coarse 0..21, fine 0..127
    int detune = 15;                   ///< 0..30, 15 = 0
    int pitchKeyFollow = 0;            ///< solo en frecuencia fija, 0..99
    int pitchVelocity = 7;             ///< 0..14, 7 = 0
    int form = 0, skirt = 0, resonance = 0; ///< forma espectral 0 seno, 1 All1, 2 All2, 3 Odd1, 4 Odd2, 5 Res1, 6 Res2
    int pegInit = 50, pegAttack = 50, pegAttackTime = 0, pegDecayTime = 0; ///< EG de tono, 50 = 0
    int hold = 0;                      ///< tiempo de hold del EG de amplitud
    std::array<int, 4> times {};       ///< ataque, decay 1, decay 2, release (0..99)
    std::array<int, 4> levels { 99, 99, 99, 0 };
    int timeKeyFollow = 0;             ///< 0..7 (rate scaling)
    int level = 0;                     ///< nivel de salida 0..99
    int breakPoint = 39, depthLo = 0, depthHi = 0, curveLo = 0, curveHi = 0; ///< escalado de nivel (DX7)
    int levelVelocity = 7;             ///< 0..14, 7 = 0
    int pitchModSens = 0, ampModSens = 0; ///< sensibilidad al LFO 0..7
};

/** Cabecera de una parte, común a todos los motores. */
struct PartInfo
{
    std::string name;
    bool partOn = true, keyboard = true;
    int velLo = 1, velHi = 127, noteLo = 0, noteHi = 127;
    int bendUp = 2, bendDown = -2;
    int volume = 100, pan = 64, noteShift = 0;
};

/** Una parte FM-X: cabecera + bloque FM. */
struct FmPart : PartInfo
{
    int algorithm = 0;                 ///< 0..87 (FS1R)
    int feedback = 0;                  ///< 0..7
    int lfoWave = 4, lfoSpeed = 35, lfoDelay = 0, lfoSync = 0, lfoPmd = 0, lfoAmd = 0;
    std::array<FmOp, 8> ops {};
};

/**
 * Un elemento AWM2 (146 bytes; orden de campos de ConvertWithMoss YamahaYsfcPartElement). Valores crudos;
 * los "c64" van centrados en 64. Tiempos 0..127; niveles de envolvente de amplitud 0..127, de filtro 0..255
 * (centro 128).
 */
struct AwmElement
{
    bool on = false;
    int bank = 0;                      ///< 0 ROM del instrumento (no disponible), 1 usuario (la librería), 2.. librerías
    int wave = 0;                      ///< número de onda (1 = la primera de la librería)
    int pan = 64, noteLo = 0, noteHi = 127, velLo = 1, velHi = 127;
    int level = 127, levelVelSens = 64, levelVelOffset = 0;
    std::array<int, 4> aegTime {};     ///< ataque, decay 1, decay 2, release
    std::array<int, 4> aegLevel { 0, 127, 127, 127 }; ///< inicial, ataque, decay 1, decay 2 (sostenido)
    int aegTimeKeyFollow = 64, aegKeyCentre = 60;
    int coarse = 64, fine = 64, pitchKeyFollow = 96, pitchKeyCentre = 60;
    int filterType = 17, cutoff = 255, cutoffVelSens = 64, resonance = 0, hpfCutoff = 0, distance = 128;
    std::array<int, 5> fegTime {};     ///< hold, ataque, decay 1, decay 2, release
    std::array<int, 5> fegLevel { 128, 128, 128, 128, 128 }; ///< hold, ataque, decay 1, decay 2, release
    int fegDepth = 64, fegLevelVelSens = 64, cutoffKeyFollow = 64;
    int lfoWave = 1, lfoSync = 1, lfoDelay = 0, lfoSpeed = 38, lfoAmp = 0, lfoPitch = 0, lfoFilter = 0, lfoFadeIn = 0;
};

/** Una parte AWM2 normal (8 elementos). Las partes de batería todavía no se leen. */
struct AwmPart : PartInfo
{
    std::vector<AwmElement> elements;
};

/** Un keybank de una onda: un trozo de muestra mono (los estéreo son dos keybanks, izquierda y derecha). */
struct Keybank
{
    int keyLo = 0, keyHi = 127, velLo = 1, velHi = 127;
    int level = 255, pan = 64, root = 60, coarse = 64, fine = 64;
    int format = 5;                    ///< 5 o 0: PCM de 16 bits; 4: WXC (no se puede leer)
    int loopMode = 1;                  ///< 0 bucle hacia delante, 1 una vez, 2 hacia atrás
    uint32_t rate = 44100, playStart = 0, loopStart = 0, loopEnd = 0, length = 0;
    uint64_t offset = 0;               ///< posición de las muestras (int16 little-endian) en el archivo
};

struct Waveform
{
    std::string name;
    std::vector<Keybank> keybanks;
    bool readable() const;
};

struct Performance
{
    std::string name;
    std::vector<FmPart> fm;
    std::vector<AwmPart> awm;
    int drumParts = 0;                 ///< partes de batería (todavía sin motor en Keys)
    int romElements = 0;               ///< elementos AWM2 que usan ondas del instrumento (no suenan)
    int awmParts() const { return int (awm.size()); }
    bool hasFm() const { return ! fm.empty(); }
    bool onlyFm() const { return awm.empty() && drumParts == 0 && ! fm.empty(); }
    /** Todo lo que tiene suena en Keys (FM-X y elementos con ondas de la librería). */
    bool complete() const { return drumParts == 0 && romElements == 0; }
};

struct Library
{
    std::string version;               ///< "4.0.2"…
    std::string path;
    std::vector<Performance> performances;
    std::vector<Waveform> waves;       ///< ondas de usuario de la librería (número de onda − 1)
    /** La onda de un elemento, o nullptr si es de la ROM o no se puede leer. */
    const Waveform* waveFor (const AwmElement& e) const;
};

bool looksLikeYsfc (const uint8_t* data, size_t size);
/** Lee la cabecera y las performances (no las muestras). */
std::optional<Library> readLibrary (const std::string& path, std::string& error);
} // namespace bk::ysfc
