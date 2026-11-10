// Presets de piano del Stage 4 (.ns4n) con el decodificador de OpenNord: parámetros de cada uno.
import { readFileSync, readdirSync, statSync } from 'node:fs';
import { join } from 'node:path';
import { parseNs4Preset } from '@/lib/ns4/preset';
const walk = (d: string): string[] => readdirSync(d).flatMap((n) => {
  const p = join(d, n);
  return statSync(p).isDirectory() ? walk(p) : p.endsWith('.ns4n') ? [p] : [];
});
const files = walk(process.argv[2]).sort();
let ok = 0;
const counts = new Map<string, number>();
for (const f of files) {
  const p = parseNs4Preset(new Uint8Array(readFileSync(f)));
  if (!p) { console.log('✗', f); continue; }
  ok++;
  for (const l of p.layers) {
    console.log(`${f.split('/').slice(-2).join('/').padEnd(44)} ${l.letter}: ${l.headline}`);
    for (const r of l.rows) counts.set(r.name, (counts.get(r.name) ?? 0) + 1);
  }
}
console.log(`\n${ok}/${files.length} leídos.`);
console.log([...counts.keys()].join(' · '));
const one = parseNs4Preset(new Uint8Array(readFileSync(files[0])))!;
console.log(`\nEjemplo — ${files[0].split('/').pop()}`);
for (const l of one.layers) for (const r of l.rows) console.log(`  ${l.letter} ${r.name}: ${r.display}`);
