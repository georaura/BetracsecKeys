// Genera Source/core/PianoNames.h: id de modelo de piano (32 bits, en los programas y presets del
// Stage 4) → nombre del panel. Tabla de OpenNord (ns4/names.generated.ts, AGPL-3.0): metadatos de
// catálogo (nombres), no audio.
//   tools/inspect/run.sh gen-piano-names > Source/core/PianoNames.h
import { PIANO_NAMES } from '@/lib/ns4/names.generated';
const rows = Object.entries(PIANO_NAMES).map(([id, n]) => [Number(id), n] as const).sort((a, b) => a[0] - b[0]);
console.log(`// Generado por tools/inspect/gen-piano-names.ts desde OpenNord (AGPL-3.0). No editar a mano.
// Id de modelo de piano del Stage 4 → nombre del panel (${rows.length} entradas, ordenadas por id).
#pragma once
#include <cstdint>

namespace bk::npno
{
struct PianoName { uint32_t id; const char* name; };
inline constexpr PianoName kPianoNames[] = {`);
for (const [id, n] of rows) console.log(`    { ${id}u, ${JSON.stringify(n)} },`);
console.log(`};
} // namespace bk::npno`);
