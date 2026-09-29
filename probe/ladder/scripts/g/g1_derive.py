import os
import sys
import json,math,statistics,pathlib
root=pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path(os.environ.get('LADDER_WORK', './ladder_work'));scores=json.loads((root/'g1_scores.json').read_text());names=list(scores);decisions=('WIN','BW','CLS','SF','MS','TNS');arms=('F','rWIN','rBW','rCLS','rSF','rMS','rTNS','A');pairs=('rSF+rWIN','rSF+rBW','rSF+rCLS','rSF+rMS','rSF+rTNS','rWIN+rBW','rWIN+rCLS','rWIN+rMS','rWIN+rTNS','rMS+rCLS');records=[];inter=[]
for name in names:
 d=scores[name];slope=(d['faac144']['mos']-d['faac112']['mos'])/math.log2(d['faac144']['bytes']/d['faac112']['bytes'])
 def delta(x,y):return (d[x]['mos']-d[y]['mos'])-slope*math.log2(d[x]['bytes']/d[y]['bytes'])
 gap=delta('A','F');u=json.loads((root/(name+'_G_units.json')).read_text())
 for arm in arms+pairs+('SFt','SFs'):
  x=d[arm];v=delta(arm,'F');row={'clip':name,'arm':arm,'mos':x['mos'],'bytes':x['bytes'],'delta':v,'share':100*v/gap if gap else None,'ics':u[arm]['ics'],'bands':u[arm]['bands']};records.append(row)
  if arm in arms:print('G1',name,arm,f"{x['mos']:.4f}",x['bytes'],f'{v:+.5f}',f'{row["share"]:+.1f}%',u[arm])
 for x in decisions:
  loss=delta('A','f'+x);gain=delta('r'+x,'F')
  if gain<0:verdict='compensated'
  elif loss>0 and gain>=0.7*loss and gain<=1.3*loss:verdict='separable'
  elif loss>0 and gain<0.7*loss:verdict='needs partner'
  else:verdict='interaction'
  row={'clip':name,'decision':x,'forward_loss':loss,'reverse_gain':gain,'sum_share':100*(loss+gain)/gap if gap else None,'verdict':verdict};inter.append(row)
  print('INT',name,x,f'{loss:+.5f}',f'{gain:+.5f}',f'{row["sum_share"]:+.1f}%',verdict)
for label,group in [('Pooled',names),('Clean',['Severance','21classic'])]:
 for arm in arms+pairs+('SFt','SFs'):
  r=[x for x in records if x['clip'] in group and x['arm']==arm];g=sum(x['delta'] for x in records if x['clip'] in group and x['arm']=='A')
  row={'clip':label,'arm':arm,'mos':statistics.mean(x['mos'] for x in r),'bytes':sum(x['bytes'] for x in r),'delta':statistics.mean(x['delta'] for x in r),'share':100*sum(x['delta'] for x in r)/g if g else None,'ics':sum(x['ics'] for x in r),'bands':sum(x['bands'] for x in r)};records.append(row)
  if arm in arms:print('G1',label,arm,f"{row['mos']:.4f}",row['bytes'],f'{row["delta"]:+.5f}',f'{row["share"]:+.1f}%',{'ics':row['ics'],'bands':row['bands']})
 for x in decisions:
  r=[z for z in inter if z['clip'] in group and z['decision']==x];loss=statistics.mean(z['forward_loss'] for z in r);gain=statistics.mean(z['reverse_gain'] for z in r);gap=statistics.mean(z['forward_loss']+z['reverse_gain'] for z in r)
  totalgap=sum(z['delta'] for z in records if z['clip'] in group and z['arm']=='A')/len(group)
  if gain<0:verdict='compensated'
  elif loss>0 and gain>=0.7*loss and gain<=1.3*loss:verdict='separable'
  elif loss>0 and gain<0.7*loss:verdict='needs partner'
  else:verdict='interaction'
  row={'clip':label,'decision':x,'forward_loss':loss,'reverse_gain':gain,'sum_share':100*(loss+gain)/totalgap if totalgap else None,'verdict':verdict};inter.append(row)
  print('INT',label,x,f'{loss:+.5f}',f'{gain:+.5f}',f'{row["sum_share"]:+.1f}%',verdict)
# Direct sign verification from raw MOS and bytes.
for r in records:
 if r['clip'] not in scores:continue
 d=scores[r['clip']];slope=(d['faac144']['mos']-d['faac112']['mos'])/math.log2(d['faac144']['bytes']/d['faac112']['bytes']);v=(d[r['arm']]['mos']-d['F']['mos'])-slope*math.log2(d[r['arm']]['bytes']/d['F']['bytes']);assert abs(v-r['delta'])<1e-10
print('sign-check passed',len([r for r in records if r['clip'] in scores]),'arms')
(root/'g1_derived.json').write_text(json.dumps({'arms':records,'interaction':inter},indent=2))
