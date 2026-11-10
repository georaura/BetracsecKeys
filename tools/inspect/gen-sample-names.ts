// Genera Source/core/SampleNames.h: id de sample de fábrica (en los presets/programas del Stage 4) →
// nombre de archivo sin extensión. Tabla de OpenNord (ns4/names.generated.ts, AGPL-3.0): metadatos.
//   tools/inspect/run.sh gen-sample-names > Source/core/SampleNames.h
import { SAMPLE_NAMES } from '@/lib/ns4/names.generated';
const rows = Object.entries(SAMPLE_NAMES).map(([id, n]) => [Number(id), n] as const).sort((a, b) => a[0] - b[0]);
console.log(`// Generado por tools/inspect/gen-sample-names.ts desde OpenNord (AGPL-3.0). No editar a mano.
// Id de sample del Stage 4 → nombre del archivo de la Nord Sample Library (${rows.length} entradas).
#pragma once
#include <cstdint>

namespace bk::nsmp
{
struct SampleName { uint32_t id; const char* name; };
inline constexpr SampleName kSampleNames[] = {`);
for (const [id, n] of rows) console.log(`    { ${id}u, ${JSON.stringify(n)} },`);
console.log(`};
} // namespace bk::nsmp`);
