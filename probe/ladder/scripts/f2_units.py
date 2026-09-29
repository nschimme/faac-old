import sys,ast,json,collections
sys.path.insert(0,'probe/ladder');from parse_dump import parse,ICS
root=pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path(os.environ.get('LADDER_WORK', './ladder_work'))
clips={'Severance':'Severance__1.31-1.51_.16b48k','21classic':'21-classic.441.16b48k','velvet':'velvet.16b48k','Greensleeves':'24-Greensleeves-Korean-male-speech.441.16b48k','German':'12-German-male-speech.441.16b48k'}
out=collections.defaultdict(dict)
for line in open(root+'f2_generate.log'):
 tok=line.strip().split(' ',2)
 if len(tok)==3 and tok[1] in ('A','ALLF','WIN','BW','CLS','SF','MS','TNS'):out[tok[0]][tok[1]]=ast.literal_eval(tok[2])
for name,stem in clips.items():
 a=parse('probe/ladder/survey/apple/'+stem+'.dump');f=parse(str(root/(name+'_F1_normal.dump')));ic=ba=0
 for fr in f:
  for ch in (0,1):
   x=a.get(fr+1,{}).get(ch);y=f.get(fr,{}).get(ch)
   if not x or not y:continue
   head=('win_seq','window_shape','num_groups','group_len','max_sfb','global_gain','tns_present','tns_num_filt','tns_coef_res','tns_length','tns_order','tns_direction','tns_compress','tns_coef')
   diff=any(getattr(x,z)!=getattr(y,z) for z in head)
   if x.win_seq==y.win_seq and x.group_len==y.group_len:
    for g in range(x.num_groups):
     for sb in range(max(x.max_sfb,y.max_sfb)):
      if sb>=x.max_sfb or sb>=y.max_sfb:ba+=1;diff=True;continue
      b=g*x.max_sfb+sb;bb=g*y.max_sfb+sb
      if (x.band_cb[b],x.band_sf[b],x.band_ms[b])!=(y.band_cb[bb],y.band_sf[bb],y.band_ms[bb]):ba+=1;diff=True
   else:ba+=max(x.num_bands,y.num_bands)
   ic+=diff
 out[name]['ALLF']={'ics':ic,'bands':ba}
print(json.dumps(out,indent=2));open(root+'f2_units.json','w').write(json.dumps(out,indent=2))
