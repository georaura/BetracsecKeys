# BeTracks Keys

**Herramienta de sampling automático e instrumento virtual** para Mac (app, AU y VST3) y Windows (app y VST3).

- **Muestrea tus propios instrumentos.** Conecta un teclado o un módulo por MIDI y su salida a tu interfaz de
  audio: Keys toca cada nota a varias fuerzas, graba, recorta, busca el bucle de cada muestra y guarda una
  librería propia (`.bkm`) que se toca al momento.
- **Toca librerías de teclados icónicos que ya tengas.** Pianos, órganos, synths, samples y librerías FM en
  formatos compatibles (`.npno`, `.ns4n`, `.ns4o`, `.ns4y`, `.nsmp3`, `.nsmp4`, `.X7L`/`.X7U`/`.X8L`/`.X8U`).
- **Exporta a BeTracks™ Pro** (`.sfgr`), el reproductor de pistas para directo: lo que suena en Keys, también lo
  que Keys sintetiza (órgano, synth, FM), se convierte nota a nota en un instrumento listo para el escenario.

**No incluye sonidos.** Keys abre los archivos que ya tienes o los que tú muestreas. Es software libre
(AGPL-3.0).

> [!WARNING]
> **Software en desarrollo: úsalo bajo tu responsabilidad.** Se ofrece **tal cual, sin garantía** (AGPL-3.0,
> secciones 15 y 16), y cambiará. Sobre todo: **el muestreador envía MIDI y SysEx a tu instrumento**, y un
> SysEx puede cambiar sus ajustes. **Haz una copia de seguridad de tu instrumento antes de muestrear.** Eres
> responsable de lo que envías a tu instrumento. Lo que muestreas o exportas de sonidos ajenos es para tu uso
> personal: no lo redistribuyas. Detalles en [`docs/LEGAL.md`](docs/LEGAL.md).

## Descargas

- **Windows:** el instalador `.exe` lo compila GitHub Actions en cada cambio (pestaña **Actions** → la última
  ejecución → **Artifacts**). No está firmado: Windows avisará («Más información» → «Ejecutar de todas formas»).
- **Mac:** se publicará firmado y notarizado en el sitio de BeTracks Keys.
- O compílalo tú (abajo).

## Compilar

| Plataforma | Cómo |
|---|---|
| Mac | `./build_app.sh` (app, AU y VST3; instala los plugins en `~/Library/Audio/Plug-Ins`) |
| Windows | GitHub Actions (`.github/workflows/windows.yml`): VST3, app e instalador `.exe`. En local: Visual Studio 2022 o posterior y `cmake -S . -B build/win -A x64 && cmake --build build/win --config Release` |

JUCE va como submódulo: `git clone --recursive`, o `git submodule update --init` tras clonar.

Pruebas del núcleo, sin archivos de terceros: `tests/run_tests.sh`, `tests/run_sfgr_tests.sh` y
`tests/run_autosample_test.sh` (en Windows, `tests/windows` con `ctest`).

## Secciones

| Sección | Qué toca |
|---|---|
| **PIANO** | Pianos multimuestreados con capas por fuerza, apagador por tecla, cuerdas sin apagador y pedal |
| **ÓRGANO** | Órgano de ruedas fónicas con drawbars, percusión, vibrato/chorus y rotary |
| **SAMPLES** | Samples compatibles y las librerías que muestreas con Keys |
| **SYNTH** | Synth virtual-analógico y de muestras, con sus presets compatibles |
| **FM** | Síntesis FM de 8 operadores y partes de muestras de librerías compatibles |

## Exportar a BeTracks Pro

Desde el menú del nombre de cada sección: **Exportar … a BeTracks Pro (.sfgr)**. PCM de 16 bits a 48 kHz, capas
de velocidad, bucles, caída de cada nota, apagador por tecla y ruidos de soltar tecla. Lo que se mueve en vivo
(drawbars, rueda, rotary rápido) queda fijo en la muestra.

## Cómo se desarrolla

BeTracks Keys se escribe con **vibe coding supervisado**: el código lo generan herramientas de IA bajo la
dirección del autor, que decide qué se hace y valida los resultados con
sus propios instrumentos. Cada cambio pasa además las pruebas automáticas del proyecto antes de publicarse.

## Créditos y licencia

Software libre bajo **AGPL-3.0** (ver `LICENSE`). La app muestra estos créditos y la licencia completa en
**Biblioteca › Acerca de BeTracks Keys**.

| Proyecto | Licencia | Para qué |
|---|---|---|
| [OpenNord](https://github.com/simonflore/opennord) (commit en `vendor/opennord.COMMIT`) | AGPL-3.0 | Lectura de los formatos de piano, órgano, synth y samples, portada a C++ |
| [JUCE](https://github.com/juce-framework/JUCE) 9.0.5 | AGPLv3 | Interfaz, audio, MIDI y plugins; trae FLAC y Ogg Vorbis (BSD-3-Clause), zlib (zlib) y el VST3 SDK |
| [FSVR](https://github.com/musicastudio/FSVR) | GPL-3.0 | Tabla de conexiones de operadores FM y formas espectrales |
| msfa (Music Synthesizer for Android) | Apache-2.0 | Modelo de envolvente, escalado por tecla y velocidad de la síntesis FM |
| [ConvertWithMoss](https://github.com/git-moss/ConvertWithMoss) | LGPL-3.0 | Referencia del formato de los elementos de muestras |
| [Sparkle](https://sparkle-project.org) | MIT | Actualizaciones de la app de Mac |

VST es una marca registrada de Steinberg Media Technologies GmbH.

BeTracks Keys no está afiliada a ningún fabricante de instrumentos. Los formatos se nombran solo por su
extensión, para indicar compatibilidad.

## Aviso legal

Qué es tuyo y qué no, ingeniería inversa e interoperabilidad, privacidad e instaladores: [`docs/LEGAL.md`](docs/LEGAL.md).

## Marca

**BeTracks™** y su logotipo son marca de su titular. BeTracks Keys pertenece a la marca BeTracks. La AGPL-3.0
cubre el código, no la marca: el nombre y los logotipos (`assets/brand/`) no se ceden con la licencia, y una
versión modificada que se distribuya tiene que llevar otro nombre y otro logotipo.
