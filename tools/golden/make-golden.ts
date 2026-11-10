/**
 * Referencia para las pruebas del decodificador de BeTracks Keys.
 *
 * Construye pianos .npno sintéticos (senos, sin audio de Nord) con el escritor de OpenNord y
 * guarda, para cada uno, lo que el decodificador de OpenNord lee de él:
 *   <id>.npno         el piano
 *   <id>.json         identidad, zonas y directorio de golpes
 *   <id>.s<n>.pcm     PCM decodificado del golpe n (int32 LE, canales intercalados)
 *
 * Uso:  tools/golden/run.sh [carpeta]   (por defecto tests/golden)
 */
import { mkdirSync, writeFileSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { testPianoSpecs } from '@/lib/hwtest/test-piano';
import { buildNpno, NPNO_PIANO_TYPE, type NpnoInput } from '@/lib/ns4/nsp-write';
import { decodeNspStroke, readNspDirectory, strokeFor } from '@/lib/ns4/nsp-audio';
import { readNspIdentity, readNspZones } from '@/lib/ns4/nsp';

const out = resolve(process.argv[2] ?? 'tests/golden');
mkdirSync(out, { recursive: true });

const SR = 44100;
/** Un tono con armónicos, ataque corto, caída y (opcional) bucle al final. */
function tone(hz: number, seconds: number, harmonics: number[], loopFrames = 0, phase = 0): { pcm: Int16Array; loop?: { start: number; length: number } } {
  const n = Math.round(seconds * SR);
  const pcm = new Int16Array(n);
  // Con bucle, su longitud se ajusta a un número entero de ciclos (error < 0,5 cents).
  if (loopFrames) {
    const cycles = Math.max(1, Math.round((hz * loopFrames) / SR));
    loopFrames = Math.round((cycles * SR) / hz);
  }
  const f = loopFrames ? (Math.round((hz * loopFrames) / SR) * SR) / loopFrames : hz;
  const loopStart = loopFrames ? n - loopFrames : n;
  for (let i = 0; i < n; i++) {
    const t = i / SR;
    const env = Math.min(1, i / (0.004 * SR)) * (i < loopStart ? Math.exp(-t * 1.5) : Math.exp(-(loopStart / SR) * 1.5));
    let v = 0;
    harmonics.forEach((a, h) => { v += a * Math.sin(2 * Math.PI * f * (h + 1) * t + phase * (h + 1)); });
    pcm[i] = Math.round(v * env * 6000);
  }
  return { pcm, loop: loopFrames ? { start: loopStart, length: loopFrames } : undefined };
}

/** Un piano de 4 zonas (C2–C7) con 3 capas cada una, estéreo y con bucles: más parecido a uno de fábrica. */
function multiZone(): NpnoInput {
  const tops = [47, 59, 71, 96]; // última tecla de cada zona
  let low = 36;
  const zones = tops.map((high) => {
    const rootNote = high + 3; // NPNO_RECORDING_ABOVE_TOP_KEY
    const hz = 440 * 2 ** ((rootNote - 69) / 12);
    const layers = [
      { floorDb: 0, h: [1, 0.6, 0.4, 0.3, 0.2, 0.1] },
      { floorDb: 12, h: [1, 0.3, 0.1] },
      { floorDb: 26, h: [1, 0.05] },
    ].map(({ floorDb, h }, k) => {
      const l = tone(hz, 1.2, h, 8820, 0);
      const r = tone(hz, 1.2, h, 8820, 0.3 + k);
      return { pcm: [l.pcm, r.pcm], loop: l.loop, floorDb, decayDbPerSecond: 3 };
    });
    const z = { rootNote, lowNote: low, highNote: high, layers };
    low = high + 1;
    return z;
  });
  return { name: 'Keys Test Multi', variant: 'synthetic', pianoType: NPNO_PIANO_TYPE.grand, channels: 2, zones };
}

/** Un piano mono de 2 zonas, sin bucle: cubre el caso de un canal. */
function mono(): NpnoInput {
  const z = (rootNote: number, lowNote: number, highNote: number) => ({
    rootNote, lowNote, highNote,
    layers: [{ pcm: [tone(440 * 2 ** ((rootNote - 69) / 12), 0.8, [1, 0.5, 0.25]).pcm], floorDb: 0 }],
  });
  return { name: 'Keys Test Mono', pianoType: NPNO_PIANO_TYPE.grand, channels: 1, zones: [z(63, 48, 60), z(75, 61, 72)] };
}

const specs: { id: string; input: NpnoInput }[] = [
  ...testPianoSpecs().map((s) => ({ id: s.id, input: s.input })),
  { id: 'M4', input: multiZone() },
  { id: 'MONO', input: mono() },
];

for (const { id, input } of specs) {
  const { bytes } = buildNpno(input);
  writeFileSync(join(out, `${id}.npno`), bytes);
  const dir = readNspDirectory(bytes);
  if (!dir) throw new Error(`${id}: sin directorio`);
  const zones = readNspZones(bytes);
  // Golpe elegido por zona y velocidad, para comprobar la selección de capa.
  const picks = zones.map((_, z) => [1, 20, 40, 64, 90, 110, 127].map((v) => [v, strokeFor(dir, z, v)?.index ?? -1]));
  dir.strokes.forEach((s) => {
    const pcm = decodeNspStroke(bytes, dir, s);
    const inter = new Int32Array(s.frames * dir.channels);
    for (let t = 0; t < s.frames; t++) for (let c = 0; c < dir.channels; c++) inter[t * dir.channels + c] = pcm[c][t];
    writeFileSync(join(out, `${id}.s${s.index}.pcm`), Buffer.from(inter.buffer));
  });
  writeFileSync(join(out, `${id}.json`), JSON.stringify({
    identity: readNspIdentity(bytes), zones,
    directory: { version: dir.version, channels: dir.channels, blockWords: dir.blockWords, strokes: dir.strokes },
    picks,
  }, null, 1));
  // Lo mismo en texto plano, para la prueba en C++.
  const id_ = readNspIdentity(bytes)!;
  const lines = [
    `name ${id_.name ?? ''}`, `variant ${id_.variant ?? ''}`, `version ${id_.version}`,
    `dir ${dir.version} ${dir.channels} ${dir.blockWords} ${dir.strokes.length}`,
    ...zones.map((z) => `zone ${z.rootNote} ${z.lowNote} ${z.highNote}`),
    ...dir.strokes.map((s) => `stroke ${s.index} ${s.zone} ${s.velocityFrom} ${s.flags} ${s.frames} ${s.loop?.start ?? 0} ${s.loop?.length ?? 0}`),
    ...picks.flatMap((row, z) => row.map(([v, i]) => `pick ${z} ${v} ${i}`)),
  ];
  writeFileSync(join(out, `${id}.txt`), lines.join('\n') + '\n');
  console.log(`${id}:${zones.length} zonas, ${dir.strokes.length} golpes, ${dir.channels} canal(es), ${bytes.length} bytes`);
}
