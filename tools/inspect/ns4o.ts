// Lee presets de órgano del Stage 4 (.ns4o) con el decodificador de OpenNord y muestra lo que trae cada uno.
//   tools/inspect/run.sh ns4o <carpeta>
import { readFileSync, readdirSync, statSync } from 'node:fs';
import { join } from 'node:path';
import { parseNs4Preset } from '@/lib/ns4/preset';

const walk = (d: string): string[] => readdirSync(d).flatMap((n) => {
  const p = join(d, n);
  return statSync(p).isDirectory() ? walk(p) : p.endsWith('.ns4o') ? [p] : [];
});
const files = walk(process.argv[2]).sort();
let ok = 0;
const params = new Map<string, number>();
for (const f of files) {
  const preset = parseNs4Preset(new Uint8Array(readFileSync(f)));
  const name = f.split('/').slice(-2).join('/');
  if (!preset) { console.log(`✗ ${name}`); continue; }
  ok++;
  for (const l of preset.layers) {
    console.log(`${name.padEnd(46)} ${l.letter}: ${l.headline ?? '?'}`);
    for (const r of l.rows) params.set(r.name, (params.get(r.name) ?? 0) + 1);
  }
}
console.log(`\n${ok}/${files.length} presets leídos. Parámetros que aparecen (veces):`);
console.log([...params.entries()].map(([k, v]) => `${k} (${v})`).join(' · '));
const one = parseNs4Preset(new Uint8Array(readFileSync(files[0])));
console.log(`\nEjemplo completo — ${files[0].split('/').pop()}:`);
for (const l of one!.layers) for (const r of l.rows) console.log(`  ${l.letter} ${r.name}: ${r.display}`);
