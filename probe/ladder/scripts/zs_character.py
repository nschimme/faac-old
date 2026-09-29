import os
import sys,collections,statistics,pathlib
sys.path.insert(0,'probe/ladder');import parse_dump as pd,hybrid_merge as hm,prequant_check as pq
root=pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path(os.environ.get('LADDER_WORK', './ladder_work')); clips={'Severance':'Severance__1.31-1.51_.16b48k','21classic':'21-classic.441.16b48k','velvet':'velvet.16b48k','Greensleeves':'24-Greensleeves-Korean-male-speech.441.16b48k','German':'12-German-male-speech.441.16b48k'}
for ref in ('apple','fdk'):
 for name,stem in clips.items():
  s=pd.parse(str(root/(name+('_aligned' if ref=='apple' else '_fdk')+'.dump')));r=pd.parse('probe/ladder/survey/'+ref+'/'+stem+'.dump');spec=pq.load_spec(str(root/(name+('_aligned' if ref=='apple' else '_fdk')+'.spec')))
  for arm in ('Z','S'):
   freq=collections.Counter();ratios=[];isolated=whole=near=changed=0;bands_n=0
   for rf,chs in r.items():
    for ch,ri in chs.items():
     si=s.get(rf-1,{}).get(ch)
     if si is None or (si.win_seq,si.max_sfb,si.num_groups,si.group_len)!=(ri.win_seq,ri.max_sfb,ri.num_groups,ri.group_len):continue
     sp=spec.get((rf-2,ch)); byband=collections.defaultdict(list)
     for pos,hz,band in hm.band_major_positions(ri):
      if not 1<=ri.band_cb[band]<=11:continue
      qf=ri.quantized[pos];qs=si.quantized[pos];hit=(qf==0 and qs!=0) if arm=='Z' else (qs==0 and qf!=0)
      byband[band].append((pos,hz,hit))
     for band,lines in byband.items():
      hits=[v for v in lines if v[2]]
      if not hits:continue
      bands_n+=1;changed+=len(hits)
      if len(hits)==1:isolated+=len(hits)
      if len(hits)==len(lines):whole+=len(hits)
      if len(hits)/len(lines)>=.8:near+=len(hits)
      if sp:
       vals=[abs(sp[v[0]]) for v in lines];avg=sum(vals)/len(vals)
       if avg:ratios.extend([max(vals)/avg]*len(hits))
      for _,hz,_ in hits:
       freq['0-2k' if hz<2000 else '2-6k' if hz<6000 else '6-12k' if hz<12000 else '>12k']+=1
   print(name,ref,arm,'lines',changed,'freq',dict(freq),'peak_avg_med',round(statistics.median(ratios),2) if ratios else None,'isolated%',round(100*isolated/changed,1) if changed else 0,'whole%',round(100*whole/changed,1) if changed else 0,'near80%',round(100*near/changed,1) if changed else 0,'bands',bands_n,flush=True)
