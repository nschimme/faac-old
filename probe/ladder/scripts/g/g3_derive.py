import os
import sys
import json,math,statistics,pathlib
root=pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path(os.environ.get('LADDER_WORK', './ladder_work'));s=json.loads((root/'g1_scores.json').read_text());rows=[]
for name,d in s.items():
 slope=(d['faac144']['mos']-d['faac112']['mos'])/math.log2(d['faac144']['bytes']/d['faac112']['bytes'])
 def loss(x):return (d['A']['mos']-d[x]['mos'])-slope*math.log2(d['A']['bytes']/d[x]['bytes'])
 sf=loss('fSF')
 for arm in ('SFt','SFs'):
  l=loss(arm);v=-l;row={'clip':name,'arm':arm,'mos':d[arm]['mos'],'bytes':d[arm]['bytes'],'delta_vs_A':v,'share_SF_loss':100*l/sf if sf else None};rows.append(row);print(name,arm,f"{row['mos']:.4f}",row['bytes'],f'{v:+.5f}',f'{row["share_SF_loss"]:+.1f}%')
for label,group in [('Pooled',list(s)),('Clean',['Severance','21classic'])]:
 losses=[]
 for name in group:
  d=s[name];sl=(d['faac144']['mos']-d['faac112']['mos'])/math.log2(d['faac144']['bytes']/d['faac112']['bytes']);losses.append((d['A']['mos']-d['fSF']['mos'])-sl*math.log2(d['A']['bytes']/d['fSF']['bytes']))
 for arm in ('SFt','SFs'):
  r=[x for x in rows if x['clip'] in group and x['arm']==arm];v=statistics.mean(x['delta_vs_A'] for x in r);sh=-100*sum(x['delta_vs_A'] for x in r)/sum(losses);out={'clip':label,'arm':arm,'mos':statistics.mean(x['mos'] for x in r),'bytes':sum(x['bytes'] for x in r),'delta_vs_A':v,'share_SF_loss':sh};rows.append(out);print(label,arm,f"{out['mos']:.4f}",out['bytes'],f'{v:+.5f}',f'{sh:+.1f}%')
if (root/'g3_wide_scores.json').exists():
 d2=json.loads((root/'g2_scores.json').read_text());w=json.loads((root/'g3_wide_scores.json').read_text());full=[]
 for k,v in w.items():
  d=d2[k];sl=(d['base144']['mos']-d['base112']['mos'])/math.log2(d['base144']['bytes']/d['base112']['bytes']);lsf=(d['A']['mos']-d['fSF']['mos'])-sl*math.log2(d['A']['bytes']/d['fSF']['bytes'])
  for arm in ('SFt','SFs'):
   x=v[arm];dv=(x['mos']-d['A']['mos'])-sl*math.log2(x['bytes']/d['A']['bytes']);full.append({'id':k,'arm':arm,'delta_vs_A':dv,'sf_loss':lsf,'mos':x['mos'],'bytes':x['bytes']})
 for arm in ('SFt','SFs'):
  rr=[x for x in full if x['arm']==arm];print('49',arm,'n',len(rr),'mean_delta',statistics.mean(x['delta_vs_A'] for x in rr),'median_delta',statistics.median(x['delta_vs_A'] for x in rr),'share_SF_loss',-100*sum(x['delta_vs_A'] for x in rr)/sum(x['sf_loss'] for x in rr))
 (root/'g3_wide_derived.json').write_text(json.dumps(full,indent=2))
(root/'g3_derived.json').write_text(json.dumps(rows,indent=2))
