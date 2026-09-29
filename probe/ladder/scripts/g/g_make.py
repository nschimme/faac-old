import os
import sys,copy,pathlib,json,math
sys.path.insert(0,'probe/ladder')
from parse_dump import parse,ICS
from line_level import long_off,short_off,region
root=pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path(os.environ.get('LADDER_WORK', './ladder_work'))
clips={'Severance':'Severance__1.31-1.51_.16b48k','21classic':'21-classic.441.16b48k','velvet':'velvet.16b48k','Greensleeves':'24-Greensleeves-Korean-male-speech.441.16b48k','German':'12-German-male-speech.441.16b48k'}
base_arms=['F','A','rWIN','rBW','rCLS','rSF','rMS','rTNS','SFt','SFs']
pairs=['rSF+rWIN','rSF+rBW','rSF+rCLS','rSF+rMS','rSF+rTNS','rWIN+rBW','rWIN+rCLS','rWIN+rMS','rWIN+rTNS','rMS+rCLS']
TNS=('tns_present','tns_num_filt','tns_coef_res','tns_length','tns_order','tns_direction','tns_compress','tns_coef')
def cl(cb):return 0 if cb==0 else 2 if cb==13 else 3 if cb in (14,15) else 1
def fs(x,sb):
 off=short_off if x.win_seq==2 else long_off
 return (off[sb]+off[sb+1])*0.5*24000/(128 if x.win_seq==2 else 1024)
def make_one(name,stem,apple_dump,faac_dump,arms):
 a=parse(str(apple_dump));f=parse(str(faac_dump));print(name,'frames',len(a),len(f),flush=True)
 summary={}
 for arm in arms:
  binpath=root/(name+'_G_'+arm+'.bin');origpath=root/(name+'_G_'+arm+'_origin.bin');nics=nbands=0
  with binpath.open('wb') as out,origpath.open('wb') as orig:
   for idx in range(max(a)+1):
    aa=a.get(idx+1,{});ff=f.get(idx,{})
    pair_win=any((aa.get(ch,ICS()).win_seq,aa.get(ch,ICS()).window_shape,aa.get(ch,ICS()).num_groups,aa.get(ch,ICS()).group_len)!=(ff.get(ch,ICS()).win_seq,ff.get(ch,ICS()).window_shape,ff.get(ch,ICS()).num_groups,ff.get(ch,ICS()).group_len) for ch in (0,1))
    for ch in (0,1):
     av=aa.get(ch,ICS());fv=ff.get(ch,ICS());x=copy.deepcopy(fv);o=ICS();o.present=x.present
     for b in range(128):o.band_cb[b]=1
     if arm=='A':x=copy.deepcopy(av);o.present=x.present;o.band_cb=[0]*128
     elif arm!='F':
      if arm in ('SFt','SFs'):x=copy.deepcopy(av);o.present=x.present;o.band_cb=[0]*128
      comps=arm.split('+')
      if ('rWIN' in comps) and pair_win and av.present and fv.present:
       x=copy.deepcopy(av);o.present=x.present;o.band_cb=[0]*128;nics+=1
      elif av.present and fv.present and not pair_win:
       if 'rBW' in comps:
        old=copy.deepcopy(x);oldorigin=o.band_cb[:];x.max_sfb=av.max_sfb;x.num_bands=x.num_groups*x.max_sfb
        for g in range(x.num_groups):
         for sb in range(x.max_sfb):
          b=g*x.max_sfb+sb
          if sb<old.max_sfb:
           ob=g*old.max_sfb+sb
           for attr in ('band_cb','band_sf','band_ms'):getattr(x,attr)[b]=getattr(old,attr)[ob]
           o.band_cb[b]=oldorigin[ob]
          else:
           ab=g*av.max_sfb+sb
           for attr in ('band_cb','band_sf','band_ms'):getattr(x,attr)[b]=getattr(av,attr)[ab]
           o.band_cb[b]=0;nbands+=1
         for sb in range(x.max_sfb,old.max_sfb):nbands+=1
        if old.max_sfb!=x.max_sfb:nics+=1
       for g in range(x.num_groups):
        for sb in range(min(x.max_sfb,av.max_sfb)):
         b=g*x.max_sfb+sb;ab=g*av.max_sfb+sb
         if 'rCLS' in comps and (cl(x.band_cb[b])!=cl(av.band_cb[ab]) or (cl(x.band_cb[b])==3 and x.band_cb[b]!=av.band_cb[ab])):
          x.band_cb[b]=av.band_cb[ab];x.band_sf[b]=av.band_sf[ab];o.band_cb[b]=0;nbands+=1
         if 'rSF' in comps and 1<=x.band_cb[b]<=11 and 1<=av.band_cb[ab]<=11 and x.band_sf[b]!=av.band_sf[ab]:
          x.band_sf[b]=av.band_sf[ab];o.band_cb[b]=0;nbands+=1
         if 'rMS' in comps and x.band_ms[b]!=av.band_ms[ab]:
          x.band_ms[b]=av.band_ms[ab];o.band_cb[b]=0;nbands+=1
       if 'rTNS' in comps:
        if any(getattr(x,z)!=getattr(av,z) for z in TNS):nics+=1
        for z in TNS:setattr(x,z,copy.deepcopy(getattr(av,z)))
       if arm in ('SFt','SFs'):
        # Per frame/channel/region mean of FAAC-Apple on bands regular
        # in both layouts. The means are computed before changing any SF.
        diffs={r:[] for r in ('0-2k','2-6k','6-12k','>12k')}
        for g in range(min(av.num_groups,fv.num_groups)):
         for sb in range(min(av.max_sfb,fv.max_sfb)):
          ab=g*av.max_sfb+sb;fb=g*fv.max_sfb+sb
          if 1<=av.band_cb[ab]<=11 and 1<=fv.band_cb[fb]<=11:
           diffs[region(fs(av,sb))].append(fv.band_sf[fb]-av.band_sf[ab])
        offset={r:round(sum(v)/len(v)) if v else 0 for r,v in diffs.items()}
        x=copy.deepcopy(av);o.band_cb=[0]*128
        for g in range(av.num_groups):
         for sb in range(min(av.max_sfb,fv.max_sfb)):
          ab=g*av.max_sfb+sb;fb=g*fv.max_sfb+sb
          if 1<=av.band_cb[ab]<=11 and 1<=fv.band_cb[fb]<=11:
           r=region(fs(av,sb));val=av.band_sf[ab]+offset[r] if arm=='SFt' else fv.band_sf[fb]-offset[r]
           val=max(0,min(255,val))
           if val!=av.band_sf[ab]:x.band_sf[ab]=val;o.band_cb[ab]=1;nbands+=1
     out.write(x.pack());orig.write(o.pack())
  summary[arm]={'ics':nics,'bands':nbands};print(name,arm,nics,nbands,flush=True)
 (root/(name+'_G_units.json')).write_text(json.dumps(summary,indent=2))
if __name__=='__main__':
 for name in sys.argv[1:] or clips:
  make_one(name,clips[name],pathlib.Path('probe/ladder/survey/apple')/(clips[name]+'.dump'),pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path(os.environ.get('LADDER_WORK', './ladder_work'))/(name+'_F1_normal.dump'),base_arms+pairs)
