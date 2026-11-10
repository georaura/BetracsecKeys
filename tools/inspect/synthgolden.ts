// Presets de synth del Stage 4 (.ns4y): por capa encendida, los valores crudos de los campos que usa
// BeTracks Keys y lo que OpenNord muestra (nombre del sample, onda…). Referencia para el lector en C++.
//   tools/inspect/run.sh synthgolden <carpeta> > build/inspect/synth-golden.tsv
import { readFileSync, readdirSync, statSync } from 'node:fs';
import { join } from 'node:path';
import { buildPortParamMap } from '@/lib/ns4/maps';
import { decodeAllParams } from '@/lib/ns4/coverage';
import { identifyNordFile } from '@/lib/clavia/nord-file';

const walk = (d: string): string[] => readdirSync(d).flatMap((n) => {
  const p = join(d, n);
  return statSync(p).isDirectory() ? walk(p) : p.endsWith('.ns4y') ? [p] : [];
});
const SHIFT = 327 * 8;
const FIELDS = [
  'layer on/off', 'volume', 'octave shift', 'samples/analog', 'sample ID/name', 'analog type (knob 1)', 'analog cat (knob 2)',
  'analog wave/partial (knob 3)', 'osc ctrl', 'pitch fine', 'pitch coarse', 'osc env attack', 'osc env decay', 'osc env release',
  'osc env amt', 'osc env-to-pitch on/off', 'osc env velocity on/off', 'LFO target', 'LFO shape', 'LFO rate/time', 'LFO mod amt',
  'amp env attack', 'amp env decay', 'amp env release', 'amp env velocity', 'filter on/off', 'filter type', 'filter freq',
  'filter resonance / freq HP', 'filter track', 'filter drive', 'filter env amt', 'filter env attack', 'filter env decay',
  'filter env release', 'filter velocity on/off', 'mono on/off', 'legato on/off', 'glide', 'unison level', 'vibrato mode',
  'vibrato rate', 'vibrato amount', 'sustain pedal', 'susped on/off', 'pan',
];
const map = buildPortParamMap().filter((p) => p.group === 'y' && FIELDS.includes(p.name) && !p.morphOf);
for (const f of walk(process.argv[2]).sort()) {
  const bytes = new Uint8Array(readFileSync(f));
  if (identifyNordFile(bytes).tag !== 'ns4y') continue;
  const relocated = map.map((p) => ({ ...p, layers: p.layers.map((l) => ({ ...l, begBit: l.begBit - SHIFT, endBit: l.endBit - SHIFT })) }));
  const decoded = decodeAllParams(bytes, relocated);
  for (let L = 0; L < 3; L++) {
    const rows = decoded.filter((d) => d.layer === L);
    const on = rows.find((r) => r.baseName === 'layer on/off');
    if (!on || on.value !== 1) continue;
    const raw: Record<string, number> = {}, show: Record<string, string> = {};
    for (const r of rows) { raw[r.baseName] = r.value; show[r.baseName] = r.display; }
    console.log([f, 'ABC'[L], JSON.stringify(raw), JSON.stringify(show)].join('\t'));
  }
}
