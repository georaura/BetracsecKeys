// Referencia del motor de órgano: OpenNord renderiza la misma interpretación con cada modelo
// (tests/organ_engine_test.cpp la repite en C++ y compara muestra a muestra).
//   tools/inspect/run.sh organrender <carpeta de salida>
import { writeFileSync, mkdirSync } from 'node:fs';
import { join } from 'node:path';
import { renderOffline } from '@/lib/engine/organ/core';
import type { OrganParams, OrganModel } from '@/lib/engine/organ/types';

const out = process.argv[2];
mkdirSync(out, { recursive: true });
const cases: { name: string; model: OrganModel; drawbars: number[]; perc: boolean; vib: boolean }[] = [
  { name: 'b3', model: 'B3', drawbars: [8, 8, 8, 0, 0, 0, 0, 0, 0], perc: true, vib: true },
  { name: 'b3full', model: 'B3', drawbars: [8, 8, 8, 8, 8, 8, 8, 8, 8], perc: false, vib: false },
  { name: 'b3bass', model: 'B3 BASS', drawbars: [8, 6, 8, 0, 0, 0, 0, 0, 0], perc: false, vib: false },
  { name: 'vox', model: 'VOX', drawbars: [0, 8, 8, 8, 8, 4, 0, 0, 2], perc: false, vib: true },
  { name: 'farf', model: 'FARF', drawbars: [8, 0, 8, 8, 0, 0, 8, 0, 8], perc: false, vib: true },
  { name: 'pipe1', model: 'PIPE1', drawbars: [8, 0, 8, 6, 0, 4, 0, 0, 2], perc: false, vib: false },
  { name: 'pipe2', model: 'PIPE2', drawbars: [8, 8, 6, 4, 0, 0, 0, 0, 0], perc: false, vib: true },
];
const events: [number, 'on' | 'off', number][] = [
  [0.0, 'on', 60], [0.0, 'on', 64], [0.0, 'on', 67], [0.5, 'off', 60], [0.5, 'off', 64], [0.5, 'off', 67],
  [0.6, 'on', 72], [0.62, 'on', 76], [1.0, 'off', 72], [1.05, 'off', 76],
];
for (const c of cases) {
  const p: OrganParams = {
    model: c.model, drawbars: c.drawbars,
    percussion: { on: c.perc, harm3rd: true, decayFast: true, volSoft: false },
    vibrato: { on: c.vib, type: 'C3' }, volumeDb: 0, octaveShift: 0, sustain: false, keyClick: 0.5,
  };
  const s = renderOffline(p, events, 1.3, 48000, 1);
  writeFileSync(join(out, `${c.name}.f32`), Buffer.from(s.buffer));
  console.log(c.name, s.length);
}
