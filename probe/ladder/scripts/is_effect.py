import os
import sys,collections,pathlib
sys.path.insert(0,'probe/ladder');import parse_dump as pd,hybrid_merge as hm
root=pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path(os.environ.get('LADDER_WORK', './ladder_work'))
for name in ('Severance','21classic','velvet','Greensleeves','German'):
 old=pd.parse(str(root/(name+'_KF.dump')));new=pd.parse(str(root/(name+'_KF_IS.dump')));norm=pd.parse(str(root/(name+'_F1_normal.dump')));c=collections.Counter()
 for fr in norm:
  for ch in (0,1):
   a=old[fr][ch];b=new[fr][ch];r=norm[fr][ch];other=norm[fr][1-ch]
   for pos,hz,band in hm.band_major_positions(r):
    if band>=min(a.num_bands,b.num_bands,r.num_bands) or not(1<=r.band_cb[band]<=11):continue
    typ='IS' if ch==0 and other.band_cb[band] in (14,15) else 'other'
    c[typ+'_old']+=int(a.quantized[pos]!=r.quantized[pos]);c[typ+'_new']+=int(b.quantized[pos]!=r.quantized[pos]);c[typ+'_changed']+=int(a.quantized[pos]!=b.quantized[pos])
 print(name,dict(c),flush=True)
