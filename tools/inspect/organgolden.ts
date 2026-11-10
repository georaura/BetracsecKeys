// Referencia para el lector de presets de órgano (.ns4o) de BeTracks Keys: para cada preset de una
// carpeta, los parámetros de motor que obtiene OpenNord (parseNs4Preset + organParamsFromLayer).
// Una línea por capa encendida: <archivo>\t<capa>\t<json>. Los presets de fábrica son de Clavia: esto
// se ejecuta contra la carpeta del usuario y no se guarda en el proyecto.
//   tools/inspect/run.sh organgolden <carpeta> > build/inspect/organ-golden.tsv
import { readFileSync, readdirSync, statSync } from 'node:fs';
import { join } from 'node:path';
import { parseNs4Preset } from '@/lib/ns4/preset';
import { organParamsFromLayer } from '@/lib/engine/organ/params';

const walk = (d: string): string[] => readdirSync(d).flatMap((n) => {
  const p = join(d, n);
  return statSync(p).isDirectory() ? walk(p) : p.endsWith('.ns4o') ? [p] : [];
});
for (const f of walk(process.argv[2]).sort()) {
  const preset = parseNs4Preset(new Uint8Array(readFileSync(f)));
  if (!preset) { console.log(`${f}\t-\tnull`); continue; }
  for (const l of preset.layers) {
    if (!l.model) continue;
    const p = organParamsFromLayer(l.model);
    console.log(`${f}\t${l.letter}\t${JSON.stringify({
      model: p.model, drawbars: p.drawbars, perc: p.percussion, vib: p.vibrato.on,
      volumeDb: Math.round(p.volumeDb * 10) / 10, octave: p.octaveShift, sustain: p.sustain,
    })}`);
  }
}
