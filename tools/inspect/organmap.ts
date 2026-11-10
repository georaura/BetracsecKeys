// Vuelca, para los presets de órgano (.ns4o), la posición en bits de cada parámetro (capas A y B,
// ya reubicada como hace parseNs4Preset) y su tabla raw → texto. Sirve para escribir el lector en C++.
import { buildPortParamMap } from '@/lib/ns4/maps';
import { decodeAllParams } from '@/lib/ns4/coverage';

const SHIFT = 45 * 8;
const organ = buildPortParamMap().filter((p) => p.group === 'o');
const out: unknown[] = [];
for (const p of organ) {
  if (p.morphOf) continue;
  const layers = p.layers.map((l) => ({ beg: l.begBit - SHIFT, end: l.endBit - SHIFT }));
  if (layers[0].beg < 0) continue;
  const width = layers[0].end - layers[0].beg + 1;
  const table: Record<number, string> = {};
  if (width <= 8) {
    // Valor de cada raw: un archivo sintético con ese raw en la capa A.
    for (let raw = 0; raw < 1 << width; raw++) {
      const bytes = new Uint8Array(200);
      for (let b = 0; b < width; b++) {
        const bit = layers[0].beg + b;
        if ((raw >> (width - 1 - b)) & 1) bytes[bit >> 3] |= 0x80 >> (bit & 7);
      }
      const d = decodeAllParams(bytes, [{ ...p, layers: [{ ...p.layers[0], begBit: layers[0].beg, endBit: layers[0].end }] }]);
      table[raw] = d[0]?.display ?? '';
    }
  }
  out.push({ name: p.name, id: p.id, layers, width, table });
}
console.log(JSON.stringify(out, null, 1));
