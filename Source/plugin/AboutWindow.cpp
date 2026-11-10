#include "AboutWindow.h"
#include "BinaryData.h"

using namespace juce;

namespace bk::about
{
String creditsText()
{
    return String::fromUTF8 (
        "BETRACKS KEYS " JucePlugin_VersionString "\n"
        "Herramienta de sampling automático e instrumento virtual.\n"
        "© 2026 BeTracks · betracksec.com/keys\n"
        "\n"
        "Este programa es software libre: puedes redistribuirlo y modificarlo según la GNU Affero General Public\n"
        "License, versión 3 (AGPL-3.0), publicada por la Free Software Foundation. Se distribuye SIN NINGUNA\n"
        "GARANTÍA, ni siquiera la implícita de comerciabilidad o idoneidad para un fin concreto. El texto completo\n"
        "de la licencia está más abajo. Código fuente: github.com/georaura/BetracsecKeys\n"
        "\n"
        "AVISO\n"
        "Software en desarrollo: úsalo bajo tu responsabilidad. El muestreador envía MIDI y SysEx a tu\n"
        "instrumento y un SysEx puede cambiar sus ajustes: haz una copia de seguridad antes de muestrear. Lo que\n"
        "muestreas o exportas de sonidos ajenos es para tu uso personal: no lo redistribuyas. BeTracks Keys no\n"
        "envía nada: sin cuentas ni telemetría. Aviso legal completo: docs/LEGAL.md en el código fuente.\n"
        "\n"
        "CÓMO SE DESARROLLA\n"
        "Con vibe coding supervisado: el código lo generan herramientas de IA bajo la dirección\n"
        "del autor, que decide qué se hace y valida los resultados con sus propios\n"
        "instrumentos. Cada cambio pasa además las pruebas automáticas del proyecto.\n"
        "\n"
        "MARCAS\n"
        "BeTracks™ y su logotipo son marca de su titular. BeTracks Keys pertenece a la marca BeTracks. La AGPL\n"
        "cubre el código, no la marca: una versión modificada que se distribuya debe llevar otro nombre y otro\n"
        "logotipo.\n"
        "BeTracks Keys no está afiliada a ningún fabricante de instrumentos y no incluye sonidos de terceros.\n"
        "VST es una marca registrada de Steinberg Media Technologies GmbH.\n"
        "\n"
        "CRÉDITOS Y LICENCIAS DE TERCEROS\n"
        "· OpenNord (github.com/simonflore/opennord) — AGPL-3.0. Lectura de los formatos de piano, órgano,\n"
        "  synth y samples, portada a C++.\n"
        "· JUCE 9.0.5 (juce.com) — AGPLv3. Interfaz, audio, MIDI y plugins. Incluye:\n"
        "    FLAC (Xiph.Org Foundation) — BSD-3-Clause; Ogg Vorbis (Xiph.Org Foundation) — BSD-3-Clause;\n"
        "    zlib (Jean-loup Gailly y Mark Adler) — licencia zlib; VST3 SDK (Steinberg) — su licencia.\n"
        "· FSVR (github.com/musicastudio/FSVR) — GPL-3.0. Tabla de conexiones de operadores FM y formas\n"
        "  espectrales.\n"
        "· msfa (Music Synthesizer for Android, Google) — Apache-2.0. Modelo de envolvente, escalado por tecla y\n"
        "  velocidad de la síntesis FM, tal como lo documenta también Dexed (GPL-3.0).\n"
        "· ConvertWithMoss (Jürgen Moßgraber) — LGPL-3.0. Referencia del formato de los elementos de muestras.\n"
        "· Sparkle (sparkle-project.org) — MIT. Actualizaciones de la app de Mac.\n"
        "\n"
        "Las licencias de terceros piden conservar sus avisos de copyright; los textos completos están en el\n"
        "código fuente de cada proyecto, enlazado arriba.\n");
}

namespace
{
class Content : public Component
{
public:
    Content()
    {
        text_.setMultiLine (true, true);
        text_.setReadOnly (true);
        text_.setCaretVisible (false);
        text_.setScrollbarsShown (true);
        text_.setFont (FontOptions (Font::getDefaultMonospacedFontName(), 12.5f, Font::plain));
        text_.setColour (TextEditor::backgroundColourId, Colour (0xff111216));
        text_.setColour (TextEditor::textColourId, Colour (0xffd8d8dc));
        text_.setColour (TextEditor::outlineColourId, Colours::transparentBlack);
        const String licence (BinaryData::LICENSE, (size_t) BinaryData::LICENSESize);
        text_.setText (creditsText() + "\n\n──────── GNU AFFERO GENERAL PUBLIC LICENSE (texto completo) ────────\n\n" + licence, false);
        text_.moveCaretToTop (false);
        addAndMakeVisible (text_);
        setSize (720, 620);
    }
    void resized() override { text_.setBounds (getLocalBounds().reduced (8)); }

private:
    TextEditor text_;
};

Component::SafePointer<DialogWindow> open;
} // namespace

void show()
{
    if (open != nullptr) { open->toFront (true); return; }
    DialogWindow::LaunchOptions o;
    o.content.setOwned (new Content());
    o.dialogTitle = String::fromUTF8 ("Acerca de BeTracks Keys");
    o.dialogBackgroundColour = Colour (0xff0b0b0e);
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = true;
    open = o.launchAsync();
}
} // namespace bk::about
