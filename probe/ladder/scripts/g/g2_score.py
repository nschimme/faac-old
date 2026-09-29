import sys
import os,subprocess,pathlib,re,json,numpy as np,soundfile as sf
root=pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path(os.environ.get('LADDER_WORK', './ladder_work'));old=pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path(os.environ.get('LADDER_WORK', './ladder_work'));sc=os.environ.get('SCORE_CLIP', '/opt/faac-benchmark/scripts/score_clip.py');py=os.environ.get('PYTHON_BIN', sys.executable);index=json.loads((root/'g2_index.json').read_text());status=json.loads((root/'g2_encode.json').read_text());results={}
def score(src,deg):
 env=os.environ.copy();env['PYTHONPATH']=str(old);p=subprocess.run([py,sc,str(src),str(deg)],env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,check=True);m=re.search(r'MOS: ([\d.]+)',p.stdout)
 if not m:raise RuntimeError(p.stdout)
 return float(m.group(1))
for item in index:
 k=item['id'];stem=item['stem']
 if not status[k].get('scorable',False):print(k,stem,'SKIP',status[k]['reason'],flush=True);continue
 src=pathlib.Path(item['src']);n=sf.info(src).frames;results[k]={'stem':stem}
 for arm in ('A','F','fSF','fWIN','rSF','rWIN'):
  raw=root/(k+('_'+arm+'_ffmpeg.f32' if arm in ('A','F') else '_G_'+arm+'.f32'))
  if arm in ('A','F'):raw=root/(k+'_'+arm+'.f32')
  enc=root/(k+'_G_'+arm+'.m4a');wav=root/(k+'_G_'+arm+'_aligned.wav')
  x=np.fromfile(raw,dtype='<f4').reshape(-1,2);assert len(x)>=n+64,(k,arm,len(x),n);sf.write(wav,x[64:64+n],48000,subtype='FLOAT')
  mos=score(src,wav);results[k][arm]={'mos':mos,'bytes':enc.stat().st_size};print(k,arm,mos,enc.stat().st_size,flush=True)
 for rate in (112,128,144):
  enc=root/(k+f'_base{rate}.m4a');mos=score(src,enc);results[k][f'base{rate}']={'mos':mos,'bytes':enc.stat().st_size};print(k,f'base{rate}',mos,enc.stat().st_size,flush=True)
 enc=pathlib.Path(item['ref']);mos=score(src,enc);results[k]['apple']={'mos':mos,'bytes':enc.stat().st_size};print(k,'apple',mos,enc.stat().st_size,flush=True)
 (root/'g2_scores.json').write_text(json.dumps(results,indent=2))
