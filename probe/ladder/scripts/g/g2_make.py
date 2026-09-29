import os
import sys
import json,pathlib,sys,copy
root=pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path(os.environ.get('LADDER_WORK', './ladder_work'));index=json.loads((root/'g2_index.json').read_text())
sys.path.insert(0,str(root));from g_make import make_one
sys.path.insert(0,'probe/ladder');from parse_dump import parse,ICS
for item in index:
 k=item['id'];a_path=root/(k+'_apple.dump');f_path=root/(k+'_normal.dump')
 make_one(k,item['stem'],a_path,f_path,['A','F','rSF','rWIN','SFt','SFs'])
 a=parse(str(a_path));f=parse(str(f_path))
 for arm in ('SF','WIN'):
  bands=ics=0
  with (root/(k+'_G_f'+arm+'.bin')).open('wb') as out,(root/(k+'_G_f'+arm+'_origin.bin')).open('wb') as orig:
   for idx in range(max(a)+1):
    aa=a.get(idx+1,{});ff=f.get(idx,{})
    pairwin=any((aa.get(ch,ICS()).win_seq,aa.get(ch,ICS()).window_shape,aa.get(ch,ICS()).num_groups,aa.get(ch,ICS()).group_len)!=(ff.get(ch,ICS()).win_seq,ff.get(ch,ICS()).window_shape,ff.get(ch,ICS()).num_groups,ff.get(ch,ICS()).group_len) for ch in (0,1))
    for ch in (0,1):
     av=aa.get(ch,ICS());fv=ff.get(ch,ICS());x=copy.deepcopy(av);o=ICS();o.present=x.present
     if arm=='WIN' and pairwin and av.present and fv.present:
      x=copy.deepcopy(fv);o.present=x.present;o.band_cb=[1]*128;ics+=1
     elif arm=='SF' and av.present and fv.present and not pairwin:
      for g in range(av.num_groups):
       for sb in range(min(av.max_sfb,fv.max_sfb)):
        b=g*av.max_sfb+sb;fb=g*fv.max_sfb+sb
        if 1<=av.band_cb[b]<=11 and 1<=fv.band_cb[fb]<=11 and av.band_sf[b]!=fv.band_sf[fb]:
         x.band_sf[b]=fv.band_sf[fb];o.band_cb[b]=1;bands+=1
     out.write(x.pack());orig.write(o.pack())
  print(k,'f'+arm,'ICS',ics,'bands',bands,flush=True)
