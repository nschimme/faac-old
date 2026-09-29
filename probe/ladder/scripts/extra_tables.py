import os
import sys
import pathlib,re
root=pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path(os.environ.get('LADDER_WORK', './ladder_work')); clips=['Severance','21classic','velvet','Greensleeves','German']
print('PREQUANT REGION RATIOS')
for c in clips:
 for arm in ('aligned','unshifted'):
  s=(root/(c+'_'+arm+'.prequant')).read_text()
  rows=re.findall(r'^(0-2k|2-6k|6-12k|>12k)\s+\d+\s+-?[\d.]+\s+(-?[\d.]+)\s+\[\s*(-?[\d.]+),\s*(-?[\d.]+)\]',s,re.M)
  print(c,arm,rows)
print('PREQUANT Q')
for c in clips:
 for arm in ('aligned','unshifted'):
  s=(root/(c+'_'+arm+'.prequant')).read_text()
  rows=re.findall(r'^\s+(small\(1-2\)|mid\(3-8\)|large\(>8\))\s+n=(\d+)\s+median=(-?[\d.]+)\s+IQR=\[(-?[\d.]+), (-?[\d.]+)\]',s,re.M)
  print(c,arm,rows)
print('LINE RATIO')
for c in clips:
 s=(root/(c+'_aligned.lines')).read_text()
 print(c,re.findall(r'^(0-2k|2-6k|6-12k|>12k)\s+n=(\d+)\s+median=(-?[\d.]+)\s+IQR=\[(-?[\d.]+), (-?[\d.]+)\]',s,re.M))
