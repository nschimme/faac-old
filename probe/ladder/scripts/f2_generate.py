import os
import sys,copy,pathlib,json
sys.path.insert(0,'probe/ladder')
from parse_dump import parse,ICS
root=pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path(os.environ.get('LADDER_WORK', './ladder_work'))
clips={'Severance':'Severance__1.31-1.51_.16b48k','21classic':'21-classic.441.16b48k','velvet':'velvet.16b48k','Greensleeves':'24-Greensleeves-Korean-male-speech.441.16b48k','German':'12-German-male-speech.441.16b48k'}
for name,stem in clips.items():
 a=parse('probe/ladder/survey/apple/'+stem+'.dump');f=parse(str(root/(name+'_F1_normal.dump')))
 print(name,'frames',len(a),len(f),flush=True)
 for arm in ('A','ALLF','WIN','BW','CLS','SF','MS','TNS'):
  out=open(root/(name+'_F2_'+arm+'.bin'),'wb');orig=open(root/(name+'_F2_'+arm+'_origin.bin'),'wb')
  changes={'ics':0,'bands':0}
  for idx in range(max(a)+1):
   # Binary index idx: Apple frame idx+1. Step1's offset 1 selects
   # index ciFrame+1, while FAAC frame is ciFrame+1 = idx.
   aa=a.get(idx+1,{});ff=f.get(idx,{})
   pair_win=any((aa.get(ch,ICS()).win_seq,aa.get(ch,ICS()).window_shape,aa.get(ch,ICS()).num_groups,aa.get(ch,ICS()).group_len)!=(ff.get(ch,ICS()).win_seq,ff.get(ch,ICS()).window_shape,ff.get(ch,ICS()).num_groups,ff.get(ch,ICS()).group_len) for ch in (0,1))
   for ch in (0,1):
    av=aa.get(ch,ICS());fv=ff.get(ch,ICS())
    x=copy.deepcopy(av);o=ICS();o.present=x.present
    if arm=='ALLF' or (arm=='WIN' and pair_win and fv.present):
     x=copy.deepcopy(fv);o.present=x.present
     for b in range(128):o.band_cb[b]=1
     if arm=='WIN':changes['ics']+=1
    elif arm=='BW' and av.present and fv.present and not pair_win:
     old=copy.deepcopy(av);x.max_sfb=fv.max_sfb;x.num_bands=x.num_groups*x.max_sfb
     for g in range(x.num_groups):
      for sb in range(x.max_sfb):
       nb=g*x.max_sfb+sb
       if sb<old.max_sfb:
        ob=g*old.max_sfb+sb
        for attr in ('band_cb','band_sf','band_ms'):getattr(x,attr)[nb]=getattr(old,attr)[ob]
       else:
        fb=g*fv.max_sfb+sb
        for attr in ('band_cb','band_sf','band_ms'):getattr(x,attr)[nb]=getattr(fv,attr)[fb]
        o.band_cb[nb]=1;changes['bands']+=1
      for sb in range(x.max_sfb,old.max_sfb):changes['bands']+=1
     if x.max_sfb!=old.max_sfb:changes['ics']+=1
    elif arm in ('CLS','SF','MS') and av.present and fv.present and not pair_win:
     for g in range(av.num_groups):
      for sb in range(min(av.max_sfb,fv.max_sfb)):
       b=g*av.max_sfb+sb;fb=g*fv.max_sfb+sb
       if arm=='CLS':
        def cl(cb):return 0 if cb==0 else 2 if cb==13 else 3 if cb in (14,15) else 1
        if cl(av.band_cb[b])!=cl(fv.band_cb[fb]) or (cl(av.band_cb[b])==3 and av.band_cb[b]!=fv.band_cb[fb]):
         x.band_cb[b]=fv.band_cb[fb];x.band_sf[b]=fv.band_sf[fb];o.band_cb[b]=1;changes['bands']+=1
       elif arm=='SF':
        if 1<=av.band_cb[b]<=11 and 1<=fv.band_cb[fb]<=11 and av.band_sf[b]!=fv.band_sf[fb]:
         x.band_sf[b]=fv.band_sf[fb];o.band_cb[b]=1;changes['bands']+=1
       elif arm=='MS':
        if av.band_ms[b]!=fv.band_ms[fb]:x.band_ms[b]=fv.band_ms[fb];o.band_cb[b]=1;changes['bands']+=1
    elif arm=='TNS' and av.present and fv.present and not pair_win:
     for attr in ('tns_present','tns_num_filt','tns_coef_res','tns_length','tns_order','tns_direction','tns_compress','tns_coef'):
      setattr(x,attr,copy.deepcopy(getattr(fv,attr)))
     if any(getattr(av,attr)!=getattr(fv,attr) for attr in ('tns_present','tns_num_filt','tns_coef_res','tns_length','tns_order','tns_direction','tns_compress','tns_coef')):changes['ics']+=1
    out.write(x.pack());orig.write(o.pack())
  out.close();orig.close()
  print(name,arm,changes,flush=True)
