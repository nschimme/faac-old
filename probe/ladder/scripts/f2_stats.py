import sys,collections,statistics
sys.path.insert(0,'probe/ladder')
from parse_dump import parse,ICS
from line_level import long_off,short_off,region
clips={'Severance':'Severance__1.31-1.51_.16b48k','21classic':'21-classic.441.16b48k','velvet':'velvet.16b48k','Greensleeves':'24-Greensleeves-Korean-male-speech.441.16b48k','German':'12-German-male-speech.441.16b48k'}
def cls(cb):return 'ZERO' if cb==0 else 'PNS' if cb==13 else 'IS' if cb in (14,15) else 'REG'
def freq(x,sb):
 off=short_off if x.win_seq==2 else long_off
 return (off[sb]+off[sb+1])*0.5*24000/(128 if x.win_seq==2 else 1024)
for name,stem in clips.items():
 a=parse('probe/ladder/survey/apple/'+stem+'.dump');f=parse(str(root/(name+'_F1_normal.dump')))
 for label,d,delta in [('Apple',a,1),('FAAC',f,0)]:
  n=short=tns=0;maxs=[];bw=[];classes=collections.Counter();sf=collections.defaultdict(list);ms=nb=0
  for fr in f:
   for ch in (0,1):
    x=d.get(fr+delta,{}).get(ch)
    if not x or not x.present:continue
    n+=1;short+=x.win_seq==2;tns+=bool(x.tns_present);maxs.append(x.max_sfb)
    off=short_off if x.win_seq==2 else long_off
    bw.append(off[min(x.max_sfb,len(off)-1)]*24000/(128 if x.win_seq==2 else 1024))
    for g in range(x.num_groups):
     for sb in range(x.max_sfb):
      b=g*x.max_sfb+sb;cl=cls(x.band_cb[b]);classes[cl]+=1;nb+=1;ms+=bool(x.band_ms[b])
      if cl=='REG':sf[region(freq(x,sb))].append(x.band_sf[b])
  print(name,label,'ICS',n,'short%',round(100*short/n,1),'max_sfb',round(statistics.mean(maxs),1),'bandwidth_Hz',round(statistics.mean(bw)),'class%',{k:round(100*classes[k]/nb,1) for k in ('ZERO','REG','PNS','IS')},'sf', {k:round(statistics.mean(sf[k]),1) if sf[k] else None for k in ('0-2k','2-6k','6-12k','>12k')},'MS%',round(100*ms/nb,1),'TNS%',round(100*tns/n,1))
 # SF comparison, same-layout regular bands only
 by=collections.defaultdict(list)
 for fr in f:
  for ch in (0,1):
   x=a.get(fr+1,{}).get(ch);y=f.get(fr,{}).get(ch)
   if not x or not y or x.win_seq!=y.win_seq or x.group_len!=y.group_len:continue
   for g in range(x.num_groups):
    for sb in range(min(x.max_sfb,y.max_sfb)):
     b=g*x.max_sfb+sb;fb=g*y.max_sfb+sb
     if cls(x.band_cb[b])!='REG' or cls(y.band_cb[fb])!='REG':continue
     by[region(freq(x,sb))].append(y.band_sf[fb]-x.band_sf[b])
 print(name,'SF-diff',{r:(len(v),round(100*sum(z!=0 for z in v)/len(v),1),round(statistics.mean(v),2),round(statistics.mean(abs(z) for z in v),2)) for r,v in by.items()})
