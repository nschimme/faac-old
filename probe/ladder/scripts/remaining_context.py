import os
import sys,collections,pathlib
sys.path.insert(0,'probe/ladder');import parse_dump as pd,hybrid_merge as hm
root=pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path(os.environ.get('LADDER_WORK', './ladder_work'))
for name in ('Severance','21classic','velvet','Greensleeves','German'):
 a=pd.parse(str(root/(name+'_KF_IS_BIAS.dump')));r=pd.parse(str(root/(name+'_F1_normal.dump')));c=collections.Counter();examples=[]
 for fr in r:
  for ch in (0,1):
   x=a[fr][ch];y=r[fr][ch];other=r[fr][1-ch]
   for pos,hz,band in hm.band_major_positions(y):
    if band>=min(x.num_bands,y.num_bands) or x.quantized[pos]==y.quantized[pos]:continue
    c['all']+=1;c['ch'+str(ch)]+=1;c['ms'+str(y.band_ms[band])]+=1;c['partner'+str(other.band_cb[band])]+=1
    c['0-2k' if hz<2000 else '2-6k' if hz<6000 else '6-12k' if hz<12000 else '>12k']+=1
    if len(examples)<5:examples.append((fr,ch,band,pos,x.quantized[pos],y.quantized[pos],x.band_cb[band],y.band_cb[band],y.band_sf[band],y.band_ms[band],other.band_cb[band]))
 print(name,dict(c),'examples',examples,flush=True)
