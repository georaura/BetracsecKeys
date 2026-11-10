// BeTracks Keys — «Acerca de»: versión, cómo se desarrolla, créditos y licencias del código de terceros, aviso de
// marca y el texto completo de la AGPL-3.0. La AGPL pide que una app con interfaz muestre sus avisos legales.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace bk::about
{
/** Abre la ventana (una sola a la vez). */
void show();
/** El texto de créditos y avisos (sin la licencia completa). */
juce::String creditsText();
} // namespace bk::about
