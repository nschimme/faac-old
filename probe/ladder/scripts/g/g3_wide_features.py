import os
import sys,pathlib,collections,math,json,numpy as np
from scipy.stats import spearmanr
sys.path.insert(0,'probe/ladder');from parse_dump import parse
from line_level import long_off,short_off,region
root=pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path(os.environ.get('LADDER_WORK', './ladder_work'));old=pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path(os.environ.get('LADDER_WORK', './ladder_work'));clips={item['id']:item['stem'] for item in json.loads((root/'g2_index.json').read_text())}
rows=[]
for name,stem in clips.items():
 a=parse(str(root/(name+'_apple.dump')));f=parse(str(root/(name+'_normal.dump')))
 spec={}
 for line in open(root/(name+'_G_spec.txt')):
  tok=line.split();spec[(int(tok[1]),int(tok[2]))]=np.asarray(tok[3:],dtype=np.float32)
 mask={}
 for line in open(root/(name+'_G_mask.txt')):
  t=line.split();mask[tuple(map(int,t[1:5]))]=tuple(map(float,t[5:8]))
 count=0
 for fr,chs in f.items():
  for ch,y in chs.items():
   x=a.get(fr+1,{}).get(ch);sp=spec.get((fr-1,ch))
   if x is None or sp is None or x.win_seq!=y.win_seq or x.group_len!=y.group_len:continue
   off=short_off if x.win_seq==2 else long_off;nwin=128 if x.win_seq==2 else 1024;absw=0
   for g in range(x.num_groups):
    vals=[];energies=[]
    for sb in range(min(x.max_sfb,y.max_sfb)):
     parts=[]
     for w in range(x.group_len[g]):
      base=(absw+w)*128 if x.win_seq==2 else 0
      parts.append(sp[base+off[sb]:base+off[sb+1]])
     v=np.concatenate(parts).astype(np.float64);vals.append(v);energies.append(float(np.mean(v*v)) if len(v) else 0.0)
    raw=[]
    for sb,v in enumerate(vals):
     b=g*x.max_sfb+sb;fb=g*y.max_sfb+sb
     if not (1<=x.band_cb[b]<=11 and 1<=y.band_cb[fb]<=11):continue
     hz=(off[sb]+off[sb+1])*0.5*24000/nwin;r=region(hz);raw.append((sb,b,fb,r,v))
    offsets={r:round(np.mean([y.band_sf[fb]-x.band_sf[b] for sb,b,fb,rr,v in raw if rr==r])) if any(rr==r for _,_,_,rr,_ in raw) else 0 for r in ('0-2k','2-6k','6-12k','>12k')}
    for sb,b,fb,r,v in raw:
     residual=y.band_sf[fb]-x.band_sf[b]-offsets[r];power=v*v;energy=energies[sb];am=np.abs(v);flat=math.exp(float(np.mean(np.log(power+1e-12))))/(energy+1e-12);peak=float(np.max(am))/(float(np.mean(am))+1e-12);neighbors=[]
     if sb>0:neighbors.append(energies[sb-1])
     if sb+1<len(energies):neighbors.append(energies[sb+1])
     rel=math.log10((energy+1e-12)/(float(np.mean(neighbors))+1e-12)) if neighbors else 0.0
     target,normal_energy,normal_peak=mask.get((fr-1,ch,g,sb),(float('nan'),)*3)
     rows.append({'clip':name,'region':r,'residual':residual,'log_energy':math.log10(energy+1e-12),'flatness':flat,'tonality':1-flat,'peak_avg':peak,'relative_neighbor_energy':rel,'log_mask_target':math.log10(target+1e-12) if math.isfinite(target) else float('nan'),'log_normal_band_energy':math.log10(normal_energy+1e-12) if math.isfinite(normal_energy) else float('nan'),'log_normal_peak_energy':math.log10(normal_peak+1e-12) if math.isfinite(normal_peak) else float('nan')});count+=1
    absw+=x.group_len[g]
 print(name,'bands',count,flush=True)
features=('log_energy','flatness','peak_avg','relative_neighbor_energy','log_mask_target','log_normal_band_energy','log_normal_peak_energy')
for group,sel in [('All 49',set(clips)),('Clean', {k for k,v in clips.items() if v in ('Severance__1.31-1.51_.16b48k','21-classic.441.16b48k')})]:
 print('GROUP',group)
 out=[]
 for feat in features:
  pairs=[(r['residual'],r[feat]) for r in rows if r['clip'] in sel and math.isfinite(r[feat])]
  y=np.array([p[0] for p in pairs]);x=np.array([p[1] for p in pairs]);pear=float(np.corrcoef(x,y)[0,1]);spear=float(spearmanr(x,y).statistic);out.append((feat,len(pairs),pear,spear))
 for feat,n,p,s in sorted(out,key=lambda z:abs(z[2]),reverse=True):print(feat,n,round(p,4),round(s,4))
(root/'g3_wide_feature_count.txt').write_text(str(len(rows)))
