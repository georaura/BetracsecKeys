// Referencia para los presets de piano (.ns4n): para cada preset de <carpeta de presets>, por capa encendida,
// el nombre del piano, el .npno que elige OpenNord entre los de <carpeta de pianos> y los ajustes.
//   tools/inspect/run.sh pianogolden <presets> <pianos> > build/inspect/piano-golden.tsv
import { readFileSync, readdirSync, statSync, openSync, readSync, closeSync } from 'node:fs';
import { join, basename } from 'node:path';
import { parseNs4Preset } from '@/lib/ns4/preset';
import { readNspIdentity } from '@/lib/ns4/nsp';
import { findProgramPiano, type LocalPiano } from '@/lib/library/piano-match';

const walk = (d: string, ext: string): string[] => readdirSync(d).flatMap((n) => {
  const p = join(d, n);
  return statSync(p).isDirectory() ? walk(p, ext) : p.endsWith(ext) ? [p] : [];
});
const head = (f: string) => { const b = Buffer.alloc(0x200); const fd = openSync(f, 'r'); readSync(fd, b, 0, 0x200, 0); closeSync(fd); return new Uint8Array(b); };
const locals: LocalPiano[] = walk(process.argv[3], '.npno').map((f) => ({
  id: f, name: basename(f).replace(/\.npno$/i, ''), source: 'local', bytes: head(f), header: readNspIdentity(head(f)),
}));
for (const f of walk(process.argv[2], '.ns4n').sort()) {
  const p = parseNs4Preset(new Uint8Array(readFileSync(f)));
  if (!p) { console.log(`${f}\t-\tnull`); continue; }
  for (const l of p.layers) {
    const rows = Object.fromEntries(l.rows.map((r) => [r.name, r.display]));
    const model = (rows['piano model ID/name'] ?? '').match(/\(([^)]+)\)/)?.[1] ?? '';
    const match = model ? findProgramPiano(model, locals) : null;
    console.log([f, l.letter, JSON.stringify({
      piano: model, file: match ? basename(match.piano.id) : '',
      touch: rows['touch'], softRel: rows['soft rel on/off'], stringRes: rows['string res on/off'],
      volume: rows['volume'], octave: rows['octave shift'], timbre: rows['timbre'], dynComp: rows['dyn comp'],
    })].join('\t'));
  }
}
