# Aviso legal y criterios de BeTracks Keys

BeTracks Keys es una herramienta de interoperabilidad y de sampling para músicos. Quedarse claramente del lado
correcto de la línea es parte de lo que es el proyecto. Este documento explica esa línea; no es asesoría legal
(ver el final).

## Software en desarrollo, sin garantía

BeTracks Keys está en desarrollo activo y se ofrece **tal cual, sin ninguna garantía** (AGPL-3.0, secciones 15
y 16). Cambiará, y algunas cosas fallarán.

- **El muestreador habla con tu instrumento.** Envía notas, cambios de programa y de banco por MIDI, y los
  mensajes que tú escribas en «Antes de grabar» (también SysEx). Un SysEx puede cambiar ajustes de tu
  instrumento o de una voz. **Haz una copia de seguridad de tu instrumento antes de muestrear**, y no lo uses con
  datos que no puedas perder. Eres responsable de lo que envías a tu instrumento.
- **Cuida el volumen.** El muestreador toca notas a todas las fuerzas, también las más fuertes: baja la
  escucha antes de empezar.
- **Exportar sobrescribe** el archivo que elijas como destino.

## No es un producto de ninguna marca de instrumentos

BeTracks Keys **no es un plugin de ninguna marca de instrumentos** ni está afiliada, respaldada, patrocinada o
relacionada con ningún fabricante. Es una **herramienta de sampling automático** que, además, sabe **leer y
muestrear archivos de teclados icónicos** que el usuario ya tiene. Por eso hablamos de «teclados icónicos» y no
mencionamos marcas: las marcas de terceros pertenecen a sus dueños, Keys no usa sus nombres ni sus logotipos, y
nombra los formatos que abre solo por su extensión (`.npno`, `.ns4o`, `.X7L`…), para indicar compatibilidad.

## La línea: lo que es tuyo y lo que no

La línea es la **autoría**, no el tipo de archivo:

- **Lo que muestreas tú** con BeTracks Keys (librerías `.bkm`) son grabaciones tuyas. Pero el sonido de un
  instrumento —sobre todo sus sonidos de fábrica— puede seguir protegido por los derechos de su fabricante:
  muestrear tu propio instrumento para tocarlo tú está bien; **redistribuir** muestras de sonidos de fábrica,
  no.
- **Las librerías de terceros** que abres (pianos, samples, presets, librerías FM) siguen siendo de sus dueños.
  Keys las lee en tu equipo, para tu uso; no las sube, no las comparte ni las copia a ningún sitio.
- **Lo que exportas a BeTracks™ Pro** (`.sfgr`) a partir de una librería de terceros es para tu uso personal,
  en tus propios equipos. No lo compartas ni lo publiques.
- **BeTracks Keys no incluye ni distribuye sonidos** de nadie: ni en el código, ni en los instaladores, ni en
  la web.

## Ingeniería inversa e interoperabilidad

Leer un formato de archivo **para interoperar** con los datos del propio usuario está ampliamente aceptado (y
protegido expresamente en varias jurisdicciones, por ejemplo en las disposiciones de interoperabilidad de la
Directiva europea sobre programas de ordenador). Keys solo lo hace para leer los archivos que ya tienes.

La línea está entre **conocimiento** y **copias**:

**Permitido — conocimiento, escrito con código propio:**
- formatos, estructuras, significado de los campos, flujo de señal y algoritmos;
- constantes pequeñas que hacen falta para ser compatible o sonar bien: frecuencias de muestreo, tamaños de
  bloque, rangos y pasos de parámetros, nombres de los valores;
- código escrito de nuevo a partir de ese conocimiento.

**No permitido en el repositorio, en un instalador ni en nada que se publique:**
- firmware o software de terceros, entero o en parte, ni su audio;
- datos copiados en bloque del software de terceros: tablas de ondas, muestras, respuestas al impulso, tablas
  de coeficientes o de curvas. Si un motor necesita esos datos, se leen **de los archivos del usuario, en su
  equipo, al tocar**; nunca se guardan en el código ni se reparten.

**Excepciones registradas.** Tablas pequeñas que hacen falta para leer los archivos del usuario como lo hace el
instrumento, y que ninguna fórmula reproduce:

- `Source/core/FmxAlgorithms.h` — las 88 conexiones de operadores de la síntesis FM (88 × 8 máscaras). Viene
  del proyecto libre FSVR (GPL-3.0). Es una topología (qué operador modula a cuál), no audio.
- `Source/core/PianoNames.h`, `Source/core/SampleNames.h` — identificador → nombre visible (295 y 90
  entradas), generadas desde OpenNord (AGPL-3.0). Son metadatos para mostrar nombres legibles, no contenido.
- `Source/core/SynthFields.h` — posiciones de los campos de un preset de synth, generadas desde el mapa de
  OpenNord. Es la descripción del formato.

## Privacidad

Todo se lee y se procesa en tu equipo. **BeTracks Keys no envía nada**: sin cuentas, sin telemetría, sin
analítica. La única conexión es la de la **app de Mac publicada**, que consulta en betracksec.com si hay una
versión nueva (Sparkle) sin enviar datos de tu equipo ni de tus archivos. Los plugins y la versión de Windows
no se conectan a nada.

## Cómo se desarrolla

Con **vibe coding supervisado**: el código lo generan herramientas de IA bajo la dirección del autor, que decide
qué se hace y valida los resultados con sus propios instrumentos. Cada cambio pasa además las pruebas automáticas
del proyecto.

## Instaladores

- **Mac:** se publicará firmado y notarizado por Apple, en el sitio de BeTracks Keys.
- **Windows:** lo compila GitHub Actions a partir de este repositorio. **No está firmado**: Windows mostrará
  «Windows protegió su PC»; «Más información» → «Ejecutar de todas formas». Si prefieres no fiarte de un binario,
  compílalo tú (ver el README).

## Marca y licencia

BeTracks™ y su logotipo son marca de su titular. BeTracks Keys pertenece a la marca BeTracks. El código es
software libre bajo la **AGPL-3.0** (`LICENSE`); la licencia cubre el código, **no la marca**: una versión
modificada que se distribuya debe llevar otro nombre y otro logotipo. Los créditos de terceros están en el
README y en la app (Biblioteca › Acerca de).

## Esto no es asesoría legal

Quien use, modifique o distribuya BeTracks Keys debe comprobar las leyes de su país antes de basarse en lo
anterior. Si tienes dudas sobre un uso concreto, abre un *issue* y lo hablamos antes.
