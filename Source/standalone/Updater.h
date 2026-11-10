// Actualizaciones automáticas de la app Standalone (Sparkle). Solo hace algo en la versión
// pública (BK_SPARKLE=1, ver build_public.sh); en desarrollo es una función vacía.
#pragma once

namespace bk
{
void startUpdater();
}
