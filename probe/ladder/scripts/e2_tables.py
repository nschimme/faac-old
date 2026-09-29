import os
import sys
import pathlib,re
root=pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path(os.environ.get('LADDER_WORK', './ladder_work'))
for n in ('Severance','21classic','velvet','Greensleeves','German'):
 s=(root/(n+'_aligned.lines')).read_text().splitlines(); rows=[]
 for x in s:
  m=re.match(r'^(0-2k|2-6k|6-12k|>12k)\s+(0|1|2-4|>4)\s+(\d+)\s+([\d.]+)%\s+([\d.]+)%\s+([\d.]+)%',x)
  if m: rows.append((m[1],m[2],int(m[3]),float(m[4]),float(m[5]),float(m[6])))
 totals=[sum(r[2]*r[i]/100 for r in rows) for i in (3,4,5)]; tot=sum(r[2] for r in rows)
 print(n,'total',tot,'exact',round(100*totals[0]/tot,1),'off1',round(100*totals[1]/tot,1),'big',round(100*totals[2]/tot,1))
 if n=='Severance':
  for r in rows:print(' ',r)
  for x in s:
   if 'median=' in x or ' IQR=' in x:print(' ',x)
