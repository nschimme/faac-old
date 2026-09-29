import sys,collections
sys.path.insert(0,'probe/ladder');from parse_dump import parse
from line_level import long_off,short_off,region
clips={'Severance':'Severance__1.31-1.51_.16b48k','21classic':'21-classic.441.16b48k','velvet':'velvet.16b48k','Greensleeves':'24-Greensleeves-Korean-male-speech.441.16b48k','German':'12-German-male-speech.441.16b48k'}
def cl(cb):return 'ZERO' if cb==0 else 'PNS' if cb==13 else 'IS' if cb in (14,15) else 'REG'
for name,stem in clips.items():
 a=parse('probe/ladder/survey/apple/'+stem+'.dump');f=parse(str(root/(name+'_F1_normal.dump')));d=collections.defaultdict(lambda:[0,0,0,0]);ctx=collections.Counter()
 for fr in f:
  x=a.get(fr+1,{}).get(0);y=f.get(fr,{}).get(0)
  if not x or not y or x.win_seq!=y.win_seq or x.group_len!=y.group_len:continue
  xr=a.get(fr+1,{}).get(1);yr=f.get(fr,{}).get(1)
  for g in range(x.num_groups):
   for sb in range(min(x.max_sfb,y.max_sfb)):
    b=g*x.max_sfb+sb;fb=g*y.max_sfb+sb;off=short_off if x.win_seq==2 else long_off;hz=(off[sb]+off[sb+1])*0.5*24000/(128 if x.win_seq==2 else 1024);r=region(hz)
    z=d[r];z[0]+=1;z[1]+=bool(x.band_ms[b]);z[2]+=bool(y.band_ms[fb]);z[3]+=x.band_ms[b]!=y.band_ms[fb]
    if x.band_ms[b]!=y.band_ms[fb]:
     ctx[(r,'short' if x.win_seq==2 else 'long',cl(x.band_cb[b])+'/'+cl(xr.band_cb[b]),cl(y.band_cb[fb])+'/'+cl(yr.band_cb[fb]))]+=1
 print(name,'MS regions',{r:(v[0],round(100*v[1]/v[0],1),round(100*v[2]/v[0],1),round(100*v[3]/v[0],1)) for r,v in d.items()})
 print(name,'MS changed contexts',ctx.most_common(8))
