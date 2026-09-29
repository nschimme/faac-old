import os
import sys
import pathlib,re
p=pathlib.Path('probe/ladder/LADDER_RESULT.md');s=p.read_text();root=pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path(os.environ.get('LADDER_WORK', './ladder_work'));clips=['Severance','21classic','velvet','Greensleeves','German']
region=['0-2k','2-6k','6-12k','>12k'];buckets=['small(1-2)','mid(3-8)','large(>8)']
rows=['The same `prequant_check.py` runs give the signed prequant/reference ratio median [IQR] by region and `|q|` below. The full residual-dB and line-count outputs are `*.{prequant,lines}`.','', '| clip | Apple grid | 0–2 kHz | 2–6 kHz | 6–12 kHz | >12 kHz |','|---|---|---|---|---|---|']
for c in clips:
 for arm in ('aligned','unshifted'):
  t=(root/(c+'_'+arm+'.prequant')).read_text();found=dict((m[1],f'{m[2]} [{m[3]}, {m[4]}]') for m in re.finditer(r'^(0-2k|2-6k|6-12k|>12k)\s+\d+\s+-?[\d.]+\s+(-?[\d.]+)\s+\[\s*(-?[\d.]+),\s*(-?[\d.]+)\]',t,re.M))
  rows.append(f'| {c} | {"+64" if arm=="aligned" else "0"} | '+' | '.join(found[k] for k in region)+' |')
rows += ['','| clip | Apple grid | small `|q|` 1–2 | mid 3–8 | large >8 |','|---|---|---|---|---|']
for c in clips:
 for arm in ('aligned','unshifted'):
  t=(root/(c+'_'+arm+'.prequant')).read_text();found=dict((m[1],f'{m[3]} [{m[4]}, {m[5]}]') for m in re.finditer(r'^\s+(small\(1-2\)|mid\(3-8\)|large\(>8\))\s+n=(\d+)\s+median=(-?[\d.]+)\s+IQR=\[(-?[\d.]+), (-?[\d.]+)\]',t,re.M))
  rows.append(f'| {c} | {"+64" if arm=="aligned" else "0"} | '+' | '.join(found[k] for k in buckets)+' |')
rows.append('')
needle='`python3 probe/ladder/line_level.py <step1 dump> <Apple dump> 1` produced the match and ratio tables below.'
s=s.replace(needle,'\n'.join(rows)+'\n'+needle)
needle2='The signed dequantized step1/reference ratio for Severance is 1.000 [1.000, 1.000] in each of the four frequency regions (`line_level.py`); per-clip region ratios remain in `*_aligned.lines`.'
s=s.replace(needle2,'The signed dequantized step1/reference ratio from `line_level.py` is 1.000 [1.000, 1.000] in **each region of all five clips**; the per-region nonzero-line counts are in `*_aligned.lines`.')
p.write_text(s)
