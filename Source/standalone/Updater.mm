// Sparkle en la app Standalone de BeTracks Keys. El feed y la clave pública van en el Info.plist
// (SUFeedURL, SUPublicEDKey), que pone build_public.sh. Los plugins AU/VST3 no se actualizan
// solos: el instalador .pkg de cada versión los reemplaza.
#include "Updater.h"

#if BK_SPARKLE
 #import <Sparkle/Sparkle.h>

namespace bk
{
static SPUStandardUpdaterController* controller = nil;

void startUpdater()
{
    // Solo dentro de la app (el código compartido también va en el AU y el VST3).
    if (! [[[NSBundle mainBundle] bundleIdentifier] isEqualToString:@"com.betracsec.keys"]) return;
    // Sin clave pública (build de prueba sin firmar) Sparkle mostraría un error al arrancar.
    if ([[NSBundle mainBundle] objectForInfoDictionaryKey:@"SUPublicEDKey"] == nil) return;
    if (controller == nil)
        controller = [[SPUStandardUpdaterController alloc] initWithStartingUpdater:YES updaterDelegate:nil userDriverDelegate:nil];
}
} // namespace bk

#else

namespace bk
{
void startUpdater() {}
}

#endif
