import os
import sys
import json,math,statistics,pathlib
root=pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path(os.environ.get('LADDER_WORK', './ladder_work'));scores=json.loads((root/'g2_scores.json').read_text());index=json.loads((root/'g2_index.json').read_text());encoded=json.loads((root/'g2_encode.json').read_text());rows=[];byarm={x:[] for x in ('F_vs_A','fSF','fWIN','rSF','rWIN','A_vs_Apple')}
for item in index:
 k=item['id'];d=scores.get(k)
 if not d:continue
 slope=(d['base144']['mos']-d['base112']['mos'])/math.log2(d['base144']['bytes']/d['base112']['bytes'])
 def adj(x,y):return (d[x]['mos']-d[y]['mos'])-slope*math.log2(d[x]['bytes']/d[y]['bytes'])
 for arm,x,y in [('F_vs_A','F','A'),('fSF','fSF','A'),('fWIN','fWIN','A'),('rSF','rSF','F'),('rWIN','rWIN','F'),('A_vs_Apple','A','apple')]:
  z=adj(x,y);r={'id':k,'stem':item['stem'],'arm':arm,'delta':z,'mos':d[x]['mos'],'bytes':d[x]['bytes'],'base_mos':d[y]['mos'],'base_bytes':d[y]['bytes']};rows.append(r);byarm[arm].append(r)
  check=(r['mos']-r['base_mos'])-slope*math.log2(r['bytes']/r['base_bytes']);assert abs(check-z)<1e-10
for arm,v in byarm.items():
 if not v:continue
 ds=[x['delta'] for x in v];worst=sorted(v,key=lambda x:x['delta'])[:5]
 print(arm,'n',len(v),'mean',round(statistics.mean(ds),5),'median',round(statistics.median(ds),5),'wins',sum(x>0 for x in ds),'losses',sum(x<0 for x in ds),'ties',sum(x==0 for x in ds),'worst',[(x['stem'],round(x['delta'],5)) for x in worst])
if byarm['A_vs_Apple']:
 v=byarm['A_vs_Apple'];print('A-Apple abs<=0.02',sum(abs(x['delta'])<=0.02 for x in v),'of',len(v))
print('sign-check',len(rows),'rows')
(root/'g2_derived.json').write_text(json.dumps(rows,indent=2))
