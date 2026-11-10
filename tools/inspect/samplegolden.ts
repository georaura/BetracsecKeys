// Referencia para el lector de Nord Samples (.nsmp3/.nsmp4): por archivo, zonas y golpes según OpenNord
// (decodeNsmp + readNsmpZones), con un hash del PCM de cada golpe. Uno de cada <paso> archivos.
//   tools/inspect/run.sh samplegolden <carpeta> [paso] > build/inspect/sample-golden.jsonl
import { readFileSync, readdirSync, statSync } from 'node:fs';
import { join } from 'node:path';
import { readNsmp, readNsmpZones, decodeNsmp } from '@/lib/ns4/nsmp';

const walk = (d: string): string[] => readdirSync(d).flatMap((n) => {
  const p = join(d, n);
  return statSync(p).isDirectory() ? walk(p) : /\.nsmp[34]$/i.test(p) ? [p] : [];
});
const step = Number(process.argv[3] ?? 1);
/** FNV-1a de 32 bits sobre los enteros de todos los canales, canal a canal. */
const hash = (chs: Int32Array[]) => {
  let h = 0x811c9dc5;
  for (const ch of chs) for (const v of ch) { h ^= v & 0xffffffff; h = Math.imul(h, 0x01000193) >>> 0; }
  return h >>> 0;
};
const files = walk(process.argv[2]).sort().filter((_, i) => i % step === 0);
for (const f of files) {
  const bytes = new Uint8Array(readFileSync(f));
  const info = readNsmp(bytes);
  const zones = readNsmpZones(bytes).map((z) => ({ root: z.rootKey, lo: z.keyLow, hi: z.keyHigh, vlo: z.velLow, vhi: z.velTop, id: z.globalID }));
  const strokes = decodeNsmp(bytes).map((s) => ({
    id: s.globalID, ch: s.channelCount, frames: s.channels[0].length, rate: s.sampleRate,
    loop: s.loop ? { start: s.loop.loopStart, end: s.loop.loopEnd, loops: s.loop.loops, decay: Math.round(s.loop.decayDbPerSecond * 100) / 100 } : null,
    hash: hash(s.channels),
  }));
  console.log(JSON.stringify({ file: f, name: info.name, codec: info.codec, zones, strokes }));
  process.stderr.write('.');
}
process.stderr.write('\n');
