# Genera Source/core/SynthFields.h: posiciones en bits (MSB primero) de los campos del synth en un .ns4y
# (capas A, B, C), a partir del volcado del mapa de OpenNord (tools/inspect/presetmap.ts y 327).
#   tools/inspect/run.sh presetmap y 327 > build/inspect/synthmap.json
#   python3 tools/inspect/gen-synth-fields.py build/inspect/synthmap.json > Source/core/SynthFields.h
import json, sys
FIELDS = [
  ('on', 'layer on/off'), ('volume', 'volume'), ('octave', 'octave shift'), ('source', 'samples/analog'),
  ('sampleId', 'sample ID/name'), ('oscType', 'analog type (knob 1)'), ('oscCat', 'analog cat (knob 2)'),
  ('oscWave', 'analog wave/partial (knob 3)'), ('oscCtrl', 'osc ctrl'), ('fine', 'pitch fine'), ('coarse', 'pitch coarse'),
  ('oscEnvA', 'osc env attack'), ('oscEnvD', 'osc env decay'), ('oscEnvR', 'osc env release'), ('oscEnvAmt', 'osc env amt'),
  ('oscEnvPitch', 'osc env-to-pitch on/off'), ('oscEnvVel', 'osc env velocity on/off'), ('lfoTarget', 'LFO target'),
  ('lfoShape', 'LFO shape'), ('lfoRate', 'LFO rate/time'), ('lfoAmt', 'LFO mod amt'), ('ampA', 'amp env attack'),
  ('ampD', 'amp env decay'), ('ampR', 'amp env release'), ('ampVel', 'amp env velocity'), ('filterOn', 'filter on/off'),
  ('filterType', 'filter type'), ('cutoff', 'filter freq'), ('reso', 'filter resonance / freq HP'), ('track', 'filter track'),
  ('drive', 'filter drive'), ('fEnvAmt', 'filter env amt'), ('fEnvA', 'filter env attack'), ('fEnvD', 'filter env decay'),
  ('fEnvR', 'filter env release'), ('fVel', 'filter velocity on/off'), ('mono', 'mono on/off'), ('legato', 'legato on/off'),
  ('glide', 'glide'), ('unison', 'unison level'), ('vibMode', 'vibrato mode'), ('vibRate', 'vibrato rate'),
  ('vibAmt', 'vibrato amount'), ('susPed', 'susped on/off'), ('pan', 'pan'),
]
by = {p['name']: p for p in json.load(open(sys.argv[1]))}
print('// Generado por tools/inspect/gen-synth-fields.py desde el mapa de OpenNord (AGPL-3.0). No editar a mano.')
print('// Posiciones (bit inicial, bit final; MSB primero) de cada campo del synth en un .ns4y, capas A, B y C.')
print('#pragma once\n\nnamespace bk::synth\n{')
print('enum Field\n{')
for k, _ in FIELDS: print(f'    F_{k},')
print('    kFieldCount\n};\n')
print('struct FieldPos { const char* name; int beg[3], end[3]; };')
print('inline constexpr FieldPos kFields[kFieldCount] = {')
for k, n in FIELDS:
    L = by[n]['layers']
    while len(L) < 3: L = L + [{'beg': -1, 'end': -1}]
    print(f'    {{ "{k}", {{ {L[0]["beg"]}, {L[1]["beg"]}, {L[2]["beg"]} }}, {{ {L[0]["end"]}, {L[1]["end"]}, {L[2]["end"]} }} }},')
print('};\n} // namespace bk::synth')
