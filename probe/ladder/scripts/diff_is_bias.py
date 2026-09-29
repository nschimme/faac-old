import os
import sys,collections,pathlib
sys.path.insert(0,'probe/ladder');import parse_dump as pd,hybrid_merge as hm
root=pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path(os.environ.get('LADDER_WORK', './ladder_work'));names=['Severance','21classic','velvet','Greensleeves','German']
def pulse(path):
 d={}
 for line in open(path):
  if line.startswith('I '):
   h=line.split('|')[0].split();d[int(h[1]),int(h[2])]=int(h[8])
 return d
def cls(cb):return 'ZERO' if cb==0 else 'REG' if 1<=cb<=11 else 'PNS' if cb==13 else 'IS' if cb in (14,15) else str(cb)
for name in names:
 a=pd.parse(str(root/(name+'_KF_IS_BIAS.dump')));b=pd.parse(str(root/(name+'_F1_normal.dump')));pa=pulse(root/(name+'_KF_IS_BIAS.dump'));pb=pulse(root/(name+'_F1_normal.dump'))
 nics=0;nband=0;count=collections.Counter();den=collections.Counter();first={};linedec=linematch=0;linedecbook=linematchbook=0;lineall=linetotal=0
 def hit(k,frame,ch):
  count[k]+=1
  if k not in first:first[k]=(frame,ch)
 for frame in sorted(set(a)&set(b)):
  for ch in (0,1):
   x=a[frame].get(ch);y=b[frame].get(ch)
   if not x or not y:continue
   nics+=1
   decisions_match=(x.win_seq,x.window_shape,x.max_sfb,x.num_groups,x.group_len,x.global_gain)==(y.win_seq,y.window_shape,y.max_sfb,y.num_groups,y.group_len,y.global_gain)
   for key,v,w in [('win_seq',x.win_seq,y.win_seq),('shape',x.window_shape,y.window_shape),('grouping',(x.num_groups,x.group_len),(y.num_groups,y.group_len)),('max_sfb',x.max_sfb,y.max_sfb),('global_gain',x.global_gain,y.global_gain)]:
    den[key]+=1
    if v!=w:hit(key,frame,ch)
   tx=(x.tns_present,x.tns_num_filt,x.tns_coef_res,x.tns_length,x.tns_order,x.tns_direction,x.tns_compress,x.tns_coef);ty=(y.tns_present,y.tns_num_filt,y.tns_coef_res,y.tns_length,y.tns_order,y.tns_direction,y.tns_compress,y.tns_coef)
   den['TNS']+=1
   if tx!=ty:hit('TNS',frame,ch);decisions_match=False
   den['pulse']+=1
   if pa.get((frame,ch))!=pb.get((frame,ch)):hit('pulse',frame,ch);decisions_match=False
   n=min(x.num_bands,y.num_bands)
   if x.num_bands!=y.num_bands:decisions_match=False
   for i in range(n):
    nb=nband+1;nband=nb
    xc,yc=x.band_cb[i],y.band_cb[i];xs,ys=x.band_sf[i],y.band_sf[i];xm,ym=x.band_ms[i],y.band_ms[i]
    for key,cond in [('class',cls(xc)!=cls(yc)),('book',xc!=yc),('sf',xs!=ys),('ms_mask',xm!=ym),('PNS_energy',xc==yc==13 and xs!=ys),('IS_position',xc in (14,15) and yc in (14,15) and xs!=ys)]:
     if key in ('PNS_energy','IS_position'):
      if key=='PNS_energy' and xc==yc==13:den[key]+=1
      elif key=='IS_position' and xc in (14,15) and yc in (14,15):den[key]+=1
      else:continue
     else:den[key]+=1
     if cond:hit(key,frame,ch)
    if cls(xc)!=cls(yc) or xs!=ys or xm!=ym:decisions_match=False
   if (x.win_seq,x.max_sfb,x.num_groups,x.group_len)==(y.win_seq,y.max_sfb,y.num_groups,y.group_len):
    for pos,hz,band in hm.band_major_positions(x):
     if band>=n:continue
     if not(1<=x.band_cb[band]<=11 and 1<=y.band_cb[band]<=11):continue
     linetotal+=1
     diff=x.quantized[pos]!=y.quantized[pos]
     if diff:lineall+=1
     if (x.band_cb[band],x.band_sf[band],x.band_ms[band])==(y.band_cb[band],y.band_sf[band],y.band_ms[band]) and decisions_match:
      linematch+=1
      if diff:linedec+=1
     elif (x.band_cb[band],x.band_sf[band],x.band_ms[band])==(y.band_cb[band],y.band_sf[band],y.band_ms[band]) and (x.win_seq,x.window_shape,x.max_sfb,x.num_groups,x.group_len,x.global_gain)==(y.win_seq,y.window_shape,y.max_sfb,y.num_groups,y.group_len,y.global_gain) and tx==ty and pa.get((frame,ch))==pb.get((frame,ch)):
      linematch+=1
      if diff:linedec+=1
   den['quant_ics']+=1
   if any(x.quantized[i]!=y.quantized[i] for i in range(1024)):
    count['quant_ics']+=1
    if 'quant_ics' not in first:first['quant_ics']=(frame,ch)
 print(name,'ICS',nics,'bands',nband,'line_diff',lineall,linetotal,'all_syntax_matched_line_diff',linedec,linematch,flush=True)
 for k in ('win_seq','shape','grouping','max_sfb','class','book','sf','global_gain','ms_mask','TNS','PNS_energy','IS_position','pulse','quant_ics'):
  print(' ',k,count[k],den[k],f'{100*count[k]/den[k]:.2f}%' if den[k] else 'NA','first',first.get(k),flush=True)
