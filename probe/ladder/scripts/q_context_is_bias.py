import os
import sys,collections,pathlib
sys.path.insert(0,'probe/ladder');import parse_dump as pd,hybrid_merge as hm
root=pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path(os.environ.get('LADDER_WORK', './ladder_work'))
for name in ('Severance','21classic','velvet','Greensleeves','German'):
 a=pd.parse(str(root/(name+'_KF_IS_BIAS.dump')));b=pd.parse(str(root/(name+'_F1_normal.dump')));c=collections.Counter()
 for fr in a:
  for ch in (0,1):
   x=a[fr][ch];y=b[fr][ch];other=b[fr][1-ch]
   for pos,hz,band in hm.band_major_positions(x):
    if band>=min(x.num_bands,y.num_bands) or x.quantized[pos]==y.quantized[pos]:continue
    c['all']+=1
    if 1<=x.band_cb[band]<=11 and 1<=y.band_cb[band]<=11:
     c['regular']+=1
     if ch==0 and other.band_cb[band] in (14,15):c['left_partner_is']+=1
     if ch==1 and other.band_cb[band] in (14,15):c['right_partner_is']+=1
     if other.band_cb[band] in (14,15):c['either_partner_is']+=1
     if x.band_cb[band]==y.band_cb[band]:c['same_book']+=1
 print(name,dict(c),flush=True)
