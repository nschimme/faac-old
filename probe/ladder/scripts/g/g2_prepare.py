import sys
import os,subprocess,pathlib,wave,json,sys
root=pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path(os.environ.get('LADDER_WORK', './ladder_work'));repo=pathlib.Path(__file__).resolve().parents[4];data=pathlib.Path(os.environ.get('FAAC_BENCHMARK_DATA', '/opt/faac-benchmark/data/external/audio'));refs=sorted((repo/'probe/ladder/ref/apple').glob('*.m4a'));faac=os.environ.get('FAAC_BIN', str(repo / 'build_ladder/frontend/faac'));faad=os.environ.get('FAAD_BIN', '/tmp/faad-ladder-dump/build_faad/frontend/faad')
index=[{'id':f'g2_{i:02d}','stem':p.stem,'ref':str(p),'src':str(data/(p.stem+'.wav'))} for i,p in enumerate(refs)];(root/'g2_index.json').write_text(json.dumps(index,indent=2))
for item in index:
 k=item['id'];src=pathlib.Path(item['src']);plus=root/(k+'_plus.wav')
 if not plus.exists():
  with wave.open(str(src),'rb') as w:params=w.getparams();pcm=w.readframes(w.getnframes())
  with wave.open(str(plus),'wb') as w:w.setparams(params);w.writeframes(b'\0'*64*params.nchannels*params.sampwidth+pcm)
 for tag,enc in [('apple',pathlib.Path(item['ref']))]:
  dump=root/(k+'_'+tag+'.dump');dump.unlink(missing_ok=True);env=os.environ.copy();env.update(FAAD_DUMP=str(dump),FAAD_LADDER_DUMP='1')
  with open(root/(k+'_'+tag+'_decode.log'),'w') as log:p=subprocess.run([faad,'-q','-o',str(root/(k+'_'+tag+'.wav')),str(enc)],env=env,stdout=log,stderr=subprocess.STDOUT)
  if p.returncode or not dump.exists():raise RuntimeError(k+' '+tag+' dump failure')
 for rate in (112,128,144):
  enc=root/(k+f'_base{rate}.m4a')
  if not enc.exists():
   with open(root/(k+f'_base{rate}.log'),'w') as log:p=subprocess.run([faac,'-b',str(rate),'-o',str(enc),str(src)],stdout=log,stderr=subprocess.STDOUT)
   if p.returncode:raise RuntimeError(k+' baseline '+str(rate))
 normal=root/(k+'_normal.m4a')
 if not normal.exists():
  with open(root/(k+'_normal.log'),'w') as log:p=subprocess.run([faac,'-b','128','-o',str(normal),str(plus)],stdout=log,stderr=subprocess.STDOUT)
  if p.returncode:raise RuntimeError(k+' normal')
 dump=root/(k+'_normal.dump');dump.unlink(missing_ok=True);env=os.environ.copy();env.update(FAAD_DUMP=str(dump),FAAD_LADDER_DUMP='1')
 with open(root/(k+'_normal_decode.log'),'w') as log:p=subprocess.run([faad,'-q','-o',str(root/(k+'_normal.wav')),str(normal)],env=env,stdout=log,stderr=subprocess.STDOUT)
 if p.returncode or not dump.exists():raise RuntimeError(k+' normal dump failure')
 print(k,item['stem'],'prepared',flush=True)
