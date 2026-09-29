import os
import sys
import json,math,pathlib
root=pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path(os.environ.get('LADDER_WORK', './ladder_work'));s=json.loads((root/'f2_scores.json').read_text());u=json.loads((root/'f2_units.json').read_text());names=list(s);arms=('A','WIN','BW','CLS','SF','MS','TNS','ALLF');rows=[]
for name in names:
 d=s[name];slope=(d['faac144']['mos']-d['faac112']['mos'])/math.log2(d['faac144']['bytes']/d['faac112']['bytes'])
 def adj(arm):return (d[arm]['mos']-d['A']['mos'])-slope*math.log2(d[arm]['bytes']/d['A']['bytes'])
 gap=-adj('ALLF')
 print(name,'slope',round(slope,6),'gap',round(gap,6))
 for arm in arms:
  x=d[arm];delta=adj(arm);share=-delta/gap*100 if gap else float('nan');c=u[name][arm];row={'clip':name,'arm':arm,'mos':x['mos'],'bytes':x['bytes'],'adj_delta':delta,'share':share,'ics':c['ics'],'bands':c['bands']};rows.append(row)
  print('|',name,'|',arm,'|',f"{x['mos']:.4f}",'|',f"{x['bytes']:,}",'|',f'{delta:+.4f}','|',f'{share:+.1f}%','|',f"{c['ics']} ICS / {c['bands']:,} bands",'|')
for group,members in [('Pooled',names),('Clean',['Severance','21classic'])]:
 for arm in arms:
  rs=[r for r in rows if r['clip'] in members and r['arm']==arm];gap=sum(-r['adj_delta'] for r in rows if r['clip'] in members and r['arm']=='ALLF');delta=sum(r['adj_delta'] for r in rs)/len(rs);share=-sum(r['adj_delta'] for r in rs)/gap*100 if gap else float('nan');out={'clip':group,'arm':arm,'mos':sum(r['mos'] for r in rs)/len(rs),'bytes':sum(r['bytes'] for r in rs),'adj_delta':delta,'share':share,'ics':sum(r['ics'] for r in rs),'bands':sum(r['bands'] for r in rs)};rows.append(out)
  print('|',group,'|',arm,'|',f"{out['mos']:.4f}",'|',f"{out['bytes']:,}",'|',f'{delta:+.4f}','|',f'{share:+.1f}%','|',f"{out['ics']} ICS / {out['bands']:,} bands",'|')
(root/'f2_derived.json').write_text(json.dumps(rows,indent=2))
